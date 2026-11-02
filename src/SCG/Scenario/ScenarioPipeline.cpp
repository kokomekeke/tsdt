#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cstdlib>
#include <format>
#include <limits>
#include <map>
#include <mutex>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <thread>
#include "Common/Assert.hpp"
#include "Common/Ratio.hpp"
#include "Pipeline3/PipelinePlan.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SSG/Math/Math.hpp"
#include "SSG/Resampler/Resampler.hpp"
#include "ScenarioPipeline.hpp"

namespace Sagax::SCG {

    using Pipeline3::TaskContext;
    using Pipeline3::ExecutionParams;

    namespace {

        constexpr float resamplerBandwidth = 0.9f;
        constexpr float resamplerAttenuation = 150.f;

        const SCGSample zero{ SCGSampleBase(0.0), SCGSampleBase(0.0) };

        // a piece of the grid of a block that is either silence or a part of one event
        struct Segment {
            std::int32_t iEvent; // index into ScenarioLayout::events, -1 for silence
            std::int64_t iStart; // in samples of the output
            std::size_t count;
            std::size_t inOffset; // in LinePacket::in
            std::size_t nIn; // samples at the rate of the generator
            bool first; // the first samples of the event
            bool last; // the last samples of the event
        };

        // samples of an event the line wrote into LinePacket::block
        struct Piece {
            std::int32_t iEvent;
            std::int64_t iSample; // in samples of the output
            std::size_t offset;
            std::size_t count;
        };

        struct LinePacket {
            std::vector<Segment> segments;
            std::vector<SCGSample> in;
            std::vector<SCGSample> block;
            std::vector<Piece> pieces;
            std::int64_t iEnd; // the line has written every sample before this
        };

        // the samples [iSample, iSample + nSamples) of every channel. the grid is where the block should
        // end, the resamplers can leave it a few samples short or long
        struct OutputBlock {
            std::int64_t gridStart;
            std::int64_t gridEnd;
            std::int64_t iSample;
            std::size_t nSamples;
            std::vector<SCGSample> data;
            std::vector<std::span<const SCGSample>> channels;
        };

        struct Packet {
            std::vector<LinePacket> lines;
            std::vector<OutputBlock> outputs;
        };

        // the generator of an event gives ceil(nOut / ratio) samples for nOut output samples, ratio = num / den.
        // the output of the resampler stays within bound samples of the grid
        struct EventRates {
            std::uint64_t num;
            std::uint64_t den;
            bool resample;
            std::size_t warmup;
            std::int64_t bound;
        };

        struct EventState {
            bool initialized = false;
            bool started = false;
        };

        // nOut * den = q * num + r with r < num
        struct LineClock {
            std::size_t iNext;
            std::uint64_t q;
            std::uint64_t r;
            std::uint64_t nInTotal;
        };

        struct OutputClock {
            std::uint64_t rate; // in millihertz
            std::uint64_t remainder;
            std::int64_t iSample;
        };

        struct LineState {
            // used by Resample
            std::int64_t iSample; // the next sample the line writes
            std::unique_ptr<SSG::Resampler::Resampler<SCGSample>> resampler;
            std::vector<SCGSample> zeros;

            // used by Tune
            double phase;
            double omega;
            std::vector<SCGSample> tone;
        };

        struct OutputState {
            // used by Merge. the lines add into sum, channel ic is at ic * capacity. sum[0] is sample iSum,
            // the samples from sumEnd on are zero
            std::vector<SCGSample> sum;
            std::size_t capacity;
            std::int64_t iSum;
            std::int64_t sumEnd;

            // used by Sink
            bool transmitting;
            std::vector<std::size_t> eventsByEnd; // into ScenarioLayout::events, ordered by iEnd
            std::size_t iNextStart; // into ScenarioLayout::Output::events
            std::size_t iNextEnd; // into eventsByEnd
            std::vector<std::unique_ptr<SSG::IO::IQFileOut<SCGSampleBase>>> recordFiles;
            SSG::Packets::SamplePacket<SCGSample> channelPacket;

            // used by ReadPlayback, one per channel
            std::vector<std::unique_ptr<IQFileGenerator>> recordings;
        };

        // a timed device gets its first block this long before the start time
        constexpr std::chrono::seconds handOverLead(1);

        // calls the event callbacks at their times on a thread of its own. Stop calls the
        // ones whose time has come, and the ends of the events whose start was called, the
        // rest are dropped
        struct CallbackTimer {
            enum class Kind { transmissionStart, eventStart, eventEnd };

            struct Item {
                Kind kind;
                std::int32_t eventId;
            };

            PipelineConfig::EventLogConfig *callbacks = nullptr;
            std::mutex mtx;
            std::condition_variable cv;
            std::multimap<std::chrono::system_clock::time_point, Item> items;
            std::set<std::int32_t> started;
            bool stopRequested = false;
            std::thread thread;

            // ends the thread without calling the callbacks that are left
            ~CallbackTimer() {
                if(!thread.joinable()) {
                    return;
                }
                {
                    std::unique_lock lck(mtx);
                    stopRequested = true;
                }
                cv.notify_all();
                thread.join();
            }

            void Start(PipelineConfig::EventLogConfig &callbacks_) {
                callbacks = &callbacks_;
                items.clear();
                started.clear();
                stopRequested = false;
                thread = std::thread([this]() { Run(); });
            }

            void Add(std::chrono::system_clock::time_point time, Kind kind, std::int32_t eventId) {
                {
                    std::unique_lock lck(mtx);
                    items.insert({ time, Item{ kind, eventId } });
                }
                cv.notify_all();
            }

            // the started set is updated before, the callback is called without the lock
            void Call(std::chrono::system_clock::time_point time, const Item &item) {
                switch(item.kind) {
                    case Kind::transmissionStart:
                        if(callbacks->transmissionStart) {
                            callbacks->transmissionStart(time);
                        }
                        break;
                    case Kind::eventStart:
                        if(callbacks->eventStart) {
                            callbacks->eventStart(item.eventId);
                        }
                        break;
                    case Kind::eventEnd:
                        if(callbacks->eventEnd) {
                            callbacks->eventEnd(item.eventId);
                        }
                        break;
                }
            }

            void Track(const Item &item) {
                if(item.kind == Kind::eventStart) {
                    started.insert(item.eventId);
                }
                if(item.kind == Kind::eventEnd) {
                    started.erase(item.eventId);
                }
            }

            void Run() {
                std::unique_lock lck(mtx);
                while(!stopRequested) {
                    if(items.empty()) {
                        cv.wait(lck);
                        continue;
                    }
                    auto it = items.begin();
                    if(std::chrono::system_clock::now() < it->first) {
                        cv.wait_until(lck, it->first);
                        continue;
                    }
                    auto time = it->first;
                    Item item = it->second;
                    items.erase(it);
                    Track(item);
                    lck.unlock();
                    Call(time, item);
                    lck.lock();
                }
            }

            void Stop() {
                if(!thread.joinable()) {
                    return;
                }
                {
                    std::unique_lock lck(mtx);
                    stopRequested = true;
                }
                cv.notify_all();
                thread.join();
                auto now = std::chrono::system_clock::now();
                for(auto &[time, item] : items) {
                    if(time <= now) {
                        Track(item);
                        Call(time, item);
                    }
                }
                for(std::int32_t eventId : started) {
                    Call(now, Item{ Kind::eventEnd, eventId });
                }
                items.clear();
                started.clear();
            }
        };

        std::uint64_t ToMillihertz(double sampleRate) {
            Common::Assert(sampleRate > 0.0, "sample rate has to be positive");
            return std::uint64_t(std::llround(sampleRate * 1000.0));
        }
    }

    std::int32_t GetDefaultCpuThreadCount() {
        return std::max(std::int32_t(1), std::int32_t(std::thread::hardware_concurrency()));
    }

    struct ScenarioPipeline::Impl {
        ScenarioLayout layout;
        std::vector<EventRates> eventRates;
        std::vector<EventState> eventStates;

        std::vector<Packet> packets;
        std::int32_t nPackets = 0;

        // used by Clock
        std::uint64_t maxRate;
        std::vector<OutputClock> outputClocks;
        std::vector<LineClock> lineClocks;

        std::vector<std::unique_ptr<LineState>> lineStates;
        std::vector<std::unique_ptr<OutputState>> outputStates;

        CallbackTimer callbackTimer;

        bool isSetup = false;
        // destroyed before the members above, which its stages use
        Pipeline3::PipelineEngine engine;

        ~Impl() {
            if(isSetup) {
                engine.Stop(false);
                engine.Join();
            }
        }

        Packet &PacketOf(const TaskContext &ctx) {
            return packets[std::size_t(ctx.seq) % packets.size()];
        }

        SignalGenerator &GeneratorOf(std::int32_t iEvent) {
            return *layout.events[std::size_t(iEvent)].generator;
        }

        bool AllOutputsDone() {
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                if(outputClocks[io].iSample < layout.outputs[io].nSamples) {
                    return false;
                }
            }
            return true;
        }


        // init

        void InitState() {
            maxRate = 0;
            outputClocks.assign(layout.outputs.size(), OutputClock{});
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                outputClocks[io].rate = ToMillihertz(layout.outputs[io].sampleRate);
                maxRate = std::max(maxRate, outputClocks[io].rate);
            }
            lineClocks.assign(layout.lines.size(), LineClock{ 0, 0, 0, 0 });

            lineStates.clear();
            for(std::size_t il = 0; il < layout.lines.size(); ++il) {
                auto s = std::make_unique<LineState>();
                s->iSample = 0;
                s->phase = 0.0;
                s->omega = 0.0;
                lineStates.push_back(std::move(s));
            }
            outputStates.clear();
            for(auto &output : layout.outputs) {
                auto s = std::make_unique<OutputState>();
                s->iSum = 0;
                s->sumEnd = 0;
                s->transmitting = false;
                s->eventsByEnd = output.events;
                std::stable_sort(s->eventsByEnd.begin(), s->eventsByEnd.end(), [&](std::size_t e1, std::size_t e2) {
                    return layout.events[e1].iEnd < layout.events[e2].iEnd;
                });
                s->iNextStart = 0;
                s->iNextEnd = 0;
                outputStates.push_back(std::move(s));
            }
        }

        void InitEvents() {
            eventRates.assign(layout.events.size(), EventRates{ 1, 1, false, 0, 0 });
            eventStates = std::vector<EventState>(layout.events.size());
            if(layout.mode == ScenarioLayout::Mode::playback) {
                for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                    for(auto &file : layout.outputs[io].files) {
                        auto gen = std::make_unique<IQFileGenerator>();
                        gen->fileName = file;
                        gen->loop = false;
                        gen->Init();
                        outputStates[io]->recordings.push_back(std::move(gen));
                    }
                }
                return;
            }
            std::vector<std::pair<Common::Ratio, std::size_t>> warmups;
            for(std::size_t ie = 0; ie < layout.events.size(); ++ie) {
                auto &e = layout.events[ie];
                e.generator->Init();
                eventStates[ie].initialized = true;

                std::uint64_t rateOut = ToMillihertz(layout.outputs[e.iOutput].sampleRate);
                std::uint64_t rateIn = ToMillihertz(e.generator->GetSampleRate());
                Common::Ratio ratio(rateOut, rateIn);
                EventRates &er = eventRates[ie];
                er.num = ratio.GetNumerator();
                er.den = ratio.GetDenominator();
                er.resample = !(ratio == Common::Ratio(1, 1));
                if(!er.resample) {
                    continue;
                }
                auto it = std::find_if(warmups.begin(), warmups.end(), [&](const auto &w) { return w.first == ratio; });
                if(it == warmups.end()) {
                    SSG::Resampler::Resampler<SCGSample> resampler(ratio, resamplerBandwidth, resamplerAttenuation);
                    warmups.push_back({ ratio, resampler.GetWarmup() });
                    it = warmups.end() - 1;
                }
                er.warmup = it->second;
                // the resampler holds at most what its filters span
                er.bound = std::int64_t(std::ceil((double(er.warmup) + 1.0) * ratio.As<double>())) + 1;
            }
        }

        // the largest block of each output, and the samples a line or an output can need for it
        void InitPackets() {
            std::vector<std::size_t> maxBlock(layout.outputs.size());
            std::vector<std::int64_t> maxBound(layout.outputs.size(), 0);
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                maxBlock[io] = std::size_t((scgBlockSize * outputClocks[io].rate + maxRate - 1) / maxRate);
                for(std::size_t ie : layout.outputs[io].events) {
                    maxBound[io] = std::max(maxBound[io], eventRates[ie].bound);
                }
            }
            std::vector<std::size_t> maxIn(layout.lines.size(), 0);
            for(std::size_t il = 0; il < layout.lines.size(); ++il) {
                auto &line = layout.lines[il];
                std::size_t maxWarmup = 0;
                for(std::size_t ie : line.events) {
                    EventRates &er = eventRates[ie];
                    std::size_t nIn = std::size_t((maxBlock[line.iOutput] * er.den + er.num - 1) / er.num) + 1;
                    maxIn[il] = std::max(maxIn[il], nIn);
                    maxWarmup = std::max(maxWarmup, er.warmup);
                }
                lineStates[il]->zeros.assign(maxWarmup, zero);
            }
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                auto &os = *outputStates[io];
                os.capacity = maxBlock[io] + 2 * std::size_t(maxBound[io]);
                os.sum.assign(layout.outputs[io].nChannels * os.capacity, zero);
            }

            packets = std::vector<Packet>(std::size_t(nPackets));
            for(auto &p : packets) {
                p.lines.resize(layout.lines.size());
                for(std::size_t il = 0; il < layout.lines.size(); ++il) {
                    p.lines[il].in.reserve(maxIn[il]);
                    p.lines[il].block.reserve(outputStates[layout.lines[il].iOutput]->capacity);
                }
                p.outputs.resize(layout.outputs.size());
                for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                    std::size_t nChannels = layout.outputs[io].nChannels;
                    p.outputs[io].data.reserve(nChannels * outputStates[io]->capacity);
                    p.outputs[io].channels.reserve(nChannels);
                }
            }
        }

        void StopGenerators() {
            for(std::size_t ie = 0; ie < eventStates.size(); ++ie) {
                if(eventStates[ie].started) {
                    layout.events[ie].generator->Stop();
                    eventStates[ie].started = false;
                }
            }
        }

        void DisposeGenerators() {
            StopGenerators();
            for(std::size_t ie = 0; ie < eventStates.size(); ++ie) {
                if(eventStates[ie].initialized) {
                    layout.events[ie].generator->Dispose();
                    eventStates[ie].initialized = false;
                }
            }
            for(auto &os : outputStates) {
                for(auto &gen : os->recordings) {
                    gen->Dispose();
                }
                os->recordings.clear();
            }
        }


        // start and stop of the engine

        void OnStart() {
            if(layout.mode != ScenarioLayout::Mode::record) {
                Common::Assert(layout.callbacks.startTime.has_value(), "streaming needs a start time");
            }
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                auto &output = layout.outputs[io];
                auto &os = *outputStates[io];
                switch(layout.mode) {
                    case ScenarioLayout::Mode::stream: {
                        output.device->Start();
                    } break;
                    case ScenarioLayout::Mode::record: {
                        os.recordFiles.clear();
                        for(std::size_t ic = 0; ic < output.nChannels; ++ic) {
                            auto file = std::make_unique<SSG::IO::IQFileOut<SCGSampleBase>>();
                            os.channelPacket.Reshape(output.sampleRate, 1, 0, true);
                            os.channelPacket.meta.AddEntry<SSG::Packets::MFrequency>().frequency = output.centers[ic];
                            file->Open(output.files[ic], os.channelPacket);
                            os.channelPacket.meta.Clear();
                            Common::Assert(file->IsOpen(), std::format("failed to open file: {}", output.files[ic]));
                            os.recordFiles.push_back(std::move(file));
                        }
                    } break;
                    case ScenarioLayout::Mode::playback: {
                        for(auto &gen : os.recordings) {
                            gen->Start();
                        }
                        output.device->Start();
                    } break;
                }
            }
            if(layout.mode != ScenarioLayout::Mode::record) {
                callbackTimer.Start(layout.callbacks);
            }
        }

        void OnStop() {
            callbackTimer.Stop();
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                auto &output = layout.outputs[io];
                auto &os = *outputStates[io];
                for(auto &file : os.recordFiles) {
                    if(file->IsOpen()) {
                        file->Close();
                    }
                }
                os.recordFiles.clear();
                for(auto &gen : os.recordings) {
                    gen->Stop();
                }
                if(output.device) {
                    output.device->Stop();
                }
            }
        }


        // stages

        // gives the grid of the next block of every output and the segments of the lines. every other
        // stage depends on this
        void Clock(TaskContext &ctx) {
            if(AllOutputsDone()) {
                ctx.drop = true;
                ctx.stop = true;
                return;
            }
            Packet &p = PacketOf(ctx);
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                auto &output = layout.outputs[io];
                auto &oc = outputClocks[io];
                auto &ob = p.outputs[io];

                // the passes are scgBlockSize samples of the fastest output, the others get the part of
                // that time that their rates give
                oc.remainder += scgBlockSize * oc.rate;
                std::uint64_t n = oc.remainder / maxRate;
                oc.remainder -= n * maxRate;
                n = std::min(n, std::uint64_t(output.nSamples - oc.iSample));

                ob.gridStart = oc.iSample;
                ob.gridEnd = oc.iSample + std::int64_t(n);
                oc.iSample = ob.gridEnd;
            }
            for(std::size_t il = 0; il < layout.lines.size(); ++il) {
                ClockLine(il, p.outputs[layout.lines[il].iOutput], p.lines[il]);
            }
        }

        // the input samples the generator of event ie gives for nOut more output samples
        std::size_t AddInput(LineClock &lc, std::size_t ie, std::uint64_t nOut) {
            EventRates &er = eventRates[ie];
            lc.r += nOut * er.den;
            lc.q += lc.r / er.num;
            lc.r %= er.num;
            std::uint64_t nInTotal = lc.q + (lc.r > 0 ? 1 : 0);
            std::size_t nIn = std::size_t(nInTotal - lc.nInTotal);
            lc.nInTotal = nInTotal;
            return nIn;
        }

        void ClockLine(std::size_t il, const OutputBlock &ob, LinePacket &lp) {
            auto &line = layout.lines[il];
            auto &lc = lineClocks[il];
            lp.segments.clear();
            std::size_t nIn = 0;
            std::int64_t i = ob.gridStart;
            while(i < ob.gridEnd) {
                if(lc.iNext >= line.events.size()) {
                    lp.segments.push_back(Segment{ -1, i, std::size_t(ob.gridEnd - i), nIn, 0, false, false });
                    break;
                }
                std::size_t ie = line.events[lc.iNext];
                auto &e = layout.events[ie];
                if(i < e.iStart) {
                    std::int64_t next = std::min(ob.gridEnd, e.iStart);
                    lp.segments.push_back(Segment{ -1, i, std::size_t(next - i), nIn, 0, false, false });
                    i = next;
                    continue;
                }
                std::int64_t next = std::min(ob.gridEnd, e.iEnd);
                std::size_t count = std::size_t(next - i);
                bool first = i == e.iStart;
                bool last = next == e.iEnd;
                if(first) {
                    lc.q = 0;
                    lc.r = 0;
                    lc.nInTotal = 0;
                }
                std::size_t nInEvent = AddInput(lc, ie, count);
                lp.segments.push_back(Segment{ std::int32_t(ie), i, count, nIn, nInEvent, first, last });
                nIn += nInEvent;
                i = next;
                if(last) {
                    ++lc.iNext;
                }
            }
            lp.in.resize(nIn);
        }

        // io lines only
        void Read(TaskContext &ctx, std::size_t il) {
            LinePacket &lp = PacketOf(ctx).lines[il];
            for(auto &s : lp.segments) {
                if(s.iEvent < 0) {
                    continue;
                }
                SignalGenerator &gen = GeneratorOf(s.iEvent);
                if(s.first) {
                    gen.Start();
                    eventStates[std::size_t(s.iEvent)].started = true;
                }
                gen.Read(std::span(lp.in).subspan(s.inOffset, s.nIn));
            }
        }

        void Generate(TaskContext &ctx, std::size_t il) {
            LinePacket &lp = PacketOf(ctx).lines[il];
            bool startHere = layout.lines[il].workType == SignalGenerator::WorkType::cpu;
            for(auto &s : lp.segments) {
                if(s.iEvent < 0) {
                    continue;
                }
                SignalGenerator &gen = GeneratorOf(s.iEvent);
                if(startHere && s.first) {
                    gen.Start();
                    eventStates[std::size_t(s.iEvent)].started = true;
                }
                gen.Generate(std::span(lp.in).subspan(s.inOffset, s.nIn));
                if(s.last) {
                    gen.Stop();
                    eventStates[std::size_t(s.iEvent)].started = false;
                }
            }
        }

        // brings the input to the rate of the output. the samples go on from where the line stopped, past
        // the end of the event they are dropped, and what the resampler did not fill of an event is zeros
        void Resample(TaskContext &ctx, std::size_t il) {
            LinePacket &lp = PacketOf(ctx).lines[il];
            LineState &ls = *lineStates[il];
            lp.block.clear();
            lp.pieces.clear();
            for(auto &s : lp.segments) {
                if(s.iEvent < 0) {
                    Common::Assert(ls.iSample == s.iStart, "silence does not start where the line stopped");
                    ls.iSample += std::int64_t(s.count);
                    continue;
                }
                auto &e = layout.events[std::size_t(s.iEvent)];
                EventRates &er = eventRates[std::size_t(s.iEvent)];
                std::span<const SCGSample> out = std::span<const SCGSample>(lp.in).subspan(s.inOffset, s.nIn);
                if(er.resample) {
                    if(s.first) {
                        ls.resampler = std::make_unique<SSG::Resampler::Resampler<SCGSample>>(Common::Ratio(er.num, er.den), resamplerBandwidth, resamplerAttenuation);
                        ls.resampler->Process(std::span<const SCGSample>(ls.zeros).first(er.warmup));
                    }
                    out = ls.resampler->Process(out);
                }
                std::size_t offset = lp.block.size();
                std::int64_t iSample = ls.iSample;
                std::size_t n = std::min(out.size(), std::size_t(e.iEnd - ls.iSample));
                lp.block.insert(lp.block.end(), out.begin(), out.begin() + std::ptrdiff_t(n));
                ls.iSample += std::int64_t(n);
                if(s.last) {
                    lp.block.resize(lp.block.size() + std::size_t(e.iEnd - ls.iSample), zero);
                    ls.iSample = e.iEnd;
                    ls.resampler.reset();
                }
                Common::Assert(std::abs(ls.iSample - (s.iStart + std::int64_t(s.count))) <= er.bound, "the resampler drifted from the grid");
                lp.pieces.push_back(Piece{ s.iEvent, iSample, offset, lp.block.size() - offset });
            }
            lp.iEnd = ls.iSample;
        }

        // moves the events to their frequencies and amplitudes, the phase of the line is continuous
        void Tune(TaskContext &ctx, std::size_t il) {
            LinePacket &lp = PacketOf(ctx).lines[il];
            LineState &ls = *lineStates[il];
            auto &line = layout.lines[il];
            auto &output = layout.outputs[line.iOutput];
            for(auto &piece : lp.pieces) {
                auto &e = layout.events[std::size_t(piece.iEvent)];
                double freqTune = e.frequency - output.centers[line.iChannel];
                double omega = 2.0 * std::numbers::pi * (freqTune / output.sampleRate);
                if(omega != ls.omega) {
                    ls.tone.clear();
                    ls.omega = omega;
                }
                if(ls.tone.size() < piece.count) {
                    std::size_t n = ls.tone.size();
                    ls.tone.resize(piece.count);
                    for(std::size_t i = n; i < piece.count; ++i) {
                        ls.tone[i] = SCGSample(std::polar(1.0, double(i) * omega));
                    }
                }
                auto out = std::span(lp.block).subspan(piece.offset, piece.count);
                SCGSample p = SCGSample(std::polar(e.amplitude, ls.phase));
                ls.phase = std::fmod(ls.phase + omega * double(piece.count), 2.0 * std::numbers::pi);
                SSG::Math::MulV<SCGSample>(out, std::span(ls.tone.begin(), ls.tone.begin() + std::ptrdiff_t(piece.count)));
                SSG::Math::MulC<SCGSample>(out, p);
            }
        }

        // adds the lines into the sum and gives the samples that every line has written
        void Merge(TaskContext &ctx, std::size_t io) {
            Packet &p = PacketOf(ctx);
            OutputBlock &ob = p.outputs[io];
            auto &output = layout.outputs[io];
            auto &os = *outputStates[io];
            std::int64_t iEnd = output.lines.empty() ? ob.gridEnd : std::numeric_limits<std::int64_t>::max();
            for(std::size_t il : output.lines) {
                LinePacket &lp = p.lines[il];
                std::size_t ic = layout.lines[il].iChannel;
                for(auto &piece : lp.pieces) {
                    std::size_t offset = std::size_t(piece.iSample - os.iSum);
                    Common::Assert(offset + piece.count <= os.capacity, "a line is too far ahead of the others");
                    auto sum = std::span(os.sum).subspan(ic * os.capacity + offset, piece.count);
                    auto block = std::span(lp.block).subspan(piece.offset, piece.count);
                    for(std::size_t i = 0; i < piece.count; ++i) {
                        sum[i] += block[i];
                    }
                    os.sumEnd = std::max(os.sumEnd, piece.iSample + std::int64_t(piece.count));
                }
                iEnd = std::min(iEnd, lp.iEnd);
            }

            std::size_t n = std::size_t(iEnd - os.iSum);
            ob.iSample = os.iSum;
            ob.nSamples = n;
            ob.data.resize(output.nChannels * n);
            ob.channels.clear();
            for(std::size_t ic = 0; ic < output.nChannels; ++ic) {
                ob.channels.push_back(std::span(ob.data).subspan(ic * n, n));
            }
            if(n == 0) {
                return;
            }
            // the samples after the block move to the front of the sum
            std::size_t nUsed = std::size_t(std::max(os.sumEnd, iEnd) - os.iSum);
            for(std::size_t ic = 0; ic < output.nChannels; ++ic) {
                auto sum = std::span(os.sum).subspan(ic * os.capacity, os.capacity);
                std::copy_n(sum.begin(), n, ob.data.begin() + std::ptrdiff_t(ic * n));
                std::copy(sum.begin() + std::ptrdiff_t(n), sum.begin() + std::ptrdiff_t(nUsed), sum.begin());
                std::fill(sum.begin() + std::ptrdiff_t(nUsed - n), sum.begin() + std::ptrdiff_t(nUsed), zero);
            }
            os.iSum = iEnd;
            os.sumEnd = std::max(os.sumEnd, iEnd);
        }

        // playback mode, reads the recording of every channel at the rate of the output
        void ReadPlayback(TaskContext &ctx, std::size_t io) {
            OutputBlock &ob = PacketOf(ctx).outputs[io];
            auto &output = layout.outputs[io];
            auto &os = *outputStates[io];
            std::size_t n = std::size_t(ob.gridEnd - ob.gridStart);
            ob.iSample = ob.gridStart;
            ob.nSamples = n;
            ob.data.resize(output.nChannels * n);
            ob.channels.clear();
            for(std::size_t ic = 0; ic < output.nChannels; ++ic) {
                auto out = std::span(ob.data).subspan(ic * n, n);
                if(n > 0) {
                    os.recordings[ic]->Read(out);
                }
                ob.channels.push_back(out);
            }
        }

        void SinkRecord(TaskContext &ctx, std::size_t io) {
            OutputBlock &ob = PacketOf(ctx).outputs[io];
            auto &output = layout.outputs[io];
            auto &os = *outputStates[io];
            if(ob.nSamples == 0) {
                return;
            }
            for(std::size_t ic = 0; ic < output.nChannels; ++ic) {
                os.channelPacket.Reshape(output.sampleRate, 1, ob.nSamples, false);
                std::copy(ob.channels[ic].begin(), ob.channels[ic].end(), os.channelPacket.data.begin());
                os.recordFiles[ic]->WritePacket(os.channelPacket);
            }
        }

        // the first block waits until handOverLead before the start time and is passed with the start
        // time, the device holds the samples until then. a stop without flush interrupts the wait,
        // and the call is repeated if the engine is started again.
        // the callbacks get the time of the samples they are about. after the last block the device
        // sends what it holds
        void SinkStream(TaskContext &ctx, std::size_t io) {
            OutputBlock &ob = PacketOf(ctx).outputs[io];
            auto &output = layout.outputs[io];
            auto &os = *outputStates[io];
            auto startTime = layout.callbacks.startTime.value();
            if(ob.nSamples == 0) {
                return;
            }
            std::optional<std::chrono::system_clock::time_point> blockTime;
            if(!os.transmitting) {
                auto handOver = startTime - handOverLead;
                while(!ctx.stopRequested && std::chrono::system_clock::now() < handOver) {
                    ctx.cv->wait_until(*ctx.lck, handOver);
                }
                if(ctx.stopRequested) {
                    ctx.interrupted = true;
                    return;
                }
                blockTime = startTime;
                os.transmitting = true;
                callbackTimer.Add(startTime, CallbackTimer::Kind::transmissionStart, -1);
            }

            auto timeOf = [&](std::int64_t iSample) {
                return startTime + std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    std::chrono::duration<double>(double(iSample) / output.sampleRate)
                );
            };

            std::int64_t iEnd = ob.iSample + std::int64_t(ob.nSamples);
            while(os.iNextStart < output.events.size() && layout.events[output.events[os.iNextStart]].iStart < iEnd) {
                auto &e = layout.events[output.events[os.iNextStart]];
                callbackTimer.Add(timeOf(e.iStart), CallbackTimer::Kind::eventStart, e.id);
                ++os.iNextStart;
            }
            output.device->Take(ob.channels, blockTime);
            while(os.iNextEnd < os.eventsByEnd.size() && layout.events[os.eventsByEnd[os.iNextEnd]].iEnd <= iEnd) {
                auto &e = layout.events[os.eventsByEnd[os.iNextEnd]];
                callbackTimer.Add(timeOf(e.iEnd), CallbackTimer::Kind::eventEnd, e.id);
                ++os.iNextEnd;
            }
            if(iEnd >= output.nSamples) {
                output.device->Flush();
            }
        }


        void BuildPlan(Pipeline3::PipelinePlan &plan) {
            ExecutionParams ioParams{ .io = true, .sequential = true };
            ExecutionParams cpuParams{ .io = false, .sequential = true };

            std::int32_t clock = Pipeline3::AddStage(plan, cpuParams, {}, [this](TaskContext &ctx) { Clock(ctx); });

            if(layout.mode == ScenarioLayout::Mode::playback) {
                for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                    std::int32_t read = Pipeline3::AddStage(plan, ioParams, { clock }, [this, io](TaskContext &ctx) { ReadPlayback(ctx, io); });
                    Pipeline3::AddStage(plan, ioParams, { read }, [this, io](TaskContext &ctx) { SinkStream(ctx, io); });
                }
            }
            else {
                std::vector<std::int32_t> tunes;
                for(std::size_t il = 0; il < layout.lines.size(); ++il) {
                    std::int32_t prev = clock;
                    if(layout.lines[il].workType == SignalGenerator::WorkType::io) {
                        prev = Pipeline3::AddStage(plan, ioParams, { prev }, [this, il](TaskContext &ctx) { Read(ctx, il); });
                    }
                    prev = Pipeline3::AddStage(plan, cpuParams, { prev }, [this, il](TaskContext &ctx) { Generate(ctx, il); });
                    prev = Pipeline3::AddStage(plan, cpuParams, { prev }, [this, il](TaskContext &ctx) { Resample(ctx, il); });
                    prev = Pipeline3::AddStage(plan, cpuParams, { prev }, [this, il](TaskContext &ctx) { Tune(ctx, il); });
                    tunes.push_back(prev);
                }
                for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                    std::vector<std::int32_t> deps = { clock };
                    for(std::size_t il : layout.outputs[io].lines) {
                        deps.push_back(tunes[il]);
                    }
                    std::int32_t merge = Pipeline3::AddStage(plan, cpuParams, deps, [this, io](TaskContext &ctx) { Merge(ctx, io); });
                    if(layout.mode == ScenarioLayout::Mode::record) {
                        Pipeline3::AddStage(plan, ioParams, { merge }, [this, io](TaskContext &ctx) { SinkRecord(ctx, io); });
                    }
                    else {
                        Pipeline3::AddStage(plan, ioParams, { merge }, [this, io](TaskContext &ctx) { SinkStream(ctx, io); });
                    }
                }
            }

            plan.startFn = [this]() { OnStart(); };
            plan.stopFn = [this]() { OnStop(); };
        }
    };


    ScenarioPipeline::ScenarioPipeline() : impl(std::make_unique<Impl>()) {
    }

    ScenarioPipeline::~ScenarioPipeline() {
        Dispose();
    }

    ScenarioPipeline::ScenarioPipeline(ScenarioPipeline &&other) = default;

    ScenarioPipeline &ScenarioPipeline::operator=(ScenarioPipeline &&other) = default;

    void ScenarioPipeline::SetLayout(ScenarioLayout &&layout) {
        Common::Assert(!impl->isSetup, "layout set after setup");
        impl->layout = std::move(layout);
    }

    void ScenarioPipeline::SetStartTime(std::chrono::system_clock::time_point startTime) {
        impl->layout.callbacks.startTime = startTime;
    }

    void ScenarioPipeline::Setup(Common::ThreadPool &threadPool, std::int32_t nCpuThreads) {
        Common::Assert(nCpuThreads >= 1, "at least one cpu thread is needed");
        Pipeline3::PipelinePlan plan;
        impl->BuildPlan(plan);
        impl->nPackets = std::max(std::int32_t(16), 2 * nCpuThreads);
        impl->engine.Setup(std::move(plan), threadPool, Pipeline3::PipelineEngine::Params{ nCpuThreads, impl->nPackets });
        impl->isSetup = true;
    }

    void ScenarioPipeline::Init_() {
        Common::Assert(impl->isSetup, "init before setup");
        // Init() marks the resource valid only once Init_() returns, so a throw here
        // would leave the ones that already came up initialized with no way left to
        // reach them. they are taken down before the exception leaves
        try {
            for(auto &p : ownedResources) {
                p->Init();
            }
            for(auto p : resources) {
                p->Init();
            }
            impl->InitState();
            impl->InitEvents();
            impl->InitPackets();
        }
        catch(...) {
            Dispose_();
            throw;
        }
    }

    void ScenarioPipeline::Dispose_() {
        if(!impl) {
            return;
        }
        if(impl->isSetup) {
            impl->engine.Stop(false);
            impl->engine.Join();
            impl->engine.Reset();
        }
        impl->DisposeGenerators();
        impl->packets = std::vector<Packet>();
        for(auto p : resources) {
            p->Dispose();
        }
        for(auto &p : ownedResources) {
            p->Dispose();
        }
    }

    void ScenarioPipeline::Start() {
        Common::Assert(Valid(), "start before init");
        impl->engine.Start();
    }

    void ScenarioPipeline::Stop(bool flush) {
        impl->engine.Stop(flush);
    }

    void ScenarioPipeline::Join() {
        impl->engine.Join();
    }

    Pipeline3::PipelineEngine::RunState ScenarioPipeline::GetState() {
        return impl->engine.GetState();
    }

    std::exception_ptr ScenarioPipeline::GetError() {
        return impl->engine.GetError().error;
    }

}
