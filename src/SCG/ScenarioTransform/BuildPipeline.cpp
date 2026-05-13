#include <map>
#include <vector>
#include <format>
#include <algorithm>
#include <filesystem>

#include "Common/Graph.hpp"
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "Pipeline/Pipeline.hpp"
#include "SSG/Modules/Resampler.hpp"

#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Modules/Source.hpp"
#include "SCG/Modules/Merger.hpp"
#include "SCG/Modules/Tuner.hpp"
#include "SCG/Modules/Sink.hpp"
#include "RecordingDirectoryResource.hpp"
#include "Transform.hpp"
#include "BuildPipeline.hpp"

namespace Sagax::SCG {

    using Sagax::Pipeline::SimplePipelineBuilder;
    using Pipeline::InPort;
    using Pipeline::OutPort;

    namespace {
        struct EventCtx {
            Scenario::Event event;
            std::size_t iOutput;
            std::size_t iSignal;
            std::size_t iChannel;
        };

        struct LineCtx {
            double end;
            std::size_t iOutput;
            std::size_t iChannel;
            std::size_t iLine; // index in channel
            std::vector<std::size_t> events;
        };

        struct OutputCtx {
            Scenario::Output output;
            OutputVariant::CommonParams params;
            std::vector<std::vector<std::size_t>> lines;
        };

        struct ScenarioCtx {
            Scenario scen;
            std::vector<OutputCtx> outputs;
            std::vector<EventCtx> events;
            std::vector<LineCtx> lines;
        };

        ScenarioCtx SortScenario(const Scenario &scen) {
            std::vector<OutputCtx> outputs;
            for(auto &o : scen.outputs) {
                auto params = o.variant.GetCommonParams();
                outputs.push_back({ o, params, std::vector<std::vector<std::size_t>>(params.nChannels) });
            }

            std::vector<EventCtx> events;
            for(std::size_t ie = 0; ie < scen.events.size(); ++ie) {
                auto &e = scen.events[ie];
                std::size_t iOutput = std::find_if(scen.outputs.begin(), scen.outputs.end(), [&](const Scenario::Output &o) { return o.name == e.outputName; }) - scen.outputs.begin();
                std::size_t iSignal = std::find_if(scen.signals.begin(), scen.signals.end(), [&](const Scenario::Signal &s) { return s.name == e.signalName; }) - scen.signals.begin();
                std::size_t iChannel = e.iChannel;
                Common::Assert(iOutput < scen.outputs.size(), "missing output");
                Common::Assert(iSignal < scen.signals.size(), "missing signal");
                Common::Assert(iChannel < scen.outputs[iOutput].variant.GetCommonParams().nChannels, "channel index out of range");
                events.push_back({ e, iOutput, iSignal, iChannel });
            }
            std::sort(events.begin(), events.end(), [&](const EventCtx &e1, const EventCtx &e2) { return e1.event.start < e2.event.start; });

            std::vector<LineCtx> lines;
            for(std::size_t ie = 0; ie < events.size(); ++ie) {
                auto &e = events[ie];
                auto &oLines = outputs[e.iOutput].lines[e.iChannel];
                std::size_t i = 0;
                while(i < oLines.size() && lines[oLines[i]].end > e.event.start) {
                    ++i;
                }
                std::size_t j;
                if(i < oLines.size()) {
                    j = oLines[i];
                }
                else {
                    j = lines.size();
                    oLines.push_back(j);
                    lines.push_back({ });
                    lines[j].iOutput = e.iOutput;
                    lines[j].iChannel = e.iChannel;
                }
                lines[j].iLine = i;
                lines[j].events.push_back(ie);
                lines[j].end = e.event.end;
            }

            for(std::size_t io = 0; io < scen.outputs.size(); ++io) {
                auto &output = outputs[io];
                for(std::size_t ic = 0; ic < output.lines.size(); ++ic) {
                    if(output.lines[ic].empty()) {
                        std::size_t j = lines.size();
                        output.lines[ic].push_back(j);
                        lines.push_back({ });
                        lines[j].iOutput = io;
                        lines[j].iChannel = ic;
                        lines[j].iLine = 0;
                        lines[j].end = 0.0;
                    }
                }
            }

            return { scen, outputs, events, lines };
        }

        SampleIndexedEventList GetSampleIndexed(ScenarioCtx &scen, std::size_t iOutput, std::vector<std::size_t> events) {
            auto &output = scen.outputs[iOutput];
            SampleIndexedEventList res;
            res.nSamples = std::size_t(std::max(std::int64_t(0), std::int64_t(scen.scen.duration * output.params.sampleRate)));
            for(std::size_t ie : events) {
                auto &e = scen.events[ie];
                res.events.push_back({
                    std::size_t(std::clamp(std::int64_t(e.event.start * output.params.sampleRate), std::int64_t(0), std::int64_t(res.nSamples))),
                    std::size_t(std::clamp(std::int64_t(e.event.end * output.params.sampleRate), std::int64_t(0), std::int64_t(res.nSamples))),
                    e.event
                });
            }

            std::sort(res.events.begin(), res.events.end(), [](const SampleIndexedEventList::EventCtx &e1, const SampleIndexedEventList::EventCtx &e2) {
                return e1.iStart < e2.iStart;
            });
            return res;
        }




        struct OutputPipeline {
            SimplePipelineBuilder b;
            InPort<SCGIQPacket>* port;
            std::vector<Resource*> resources;
        };
        OutputPipeline BuildOutputStream(ScenarioCtx &scen, std::size_t iOutput) {
            auto &output = scen.outputs[iOutput];
            SimplePipelineBuilder b;
            auto &sink = b.AddVertex<StreamSink>();

            auto [dev, rsc] = GetOutputDevice(output.output.variant);
            sink.dev = std::move(dev);

            return { std::move(b), &sink.inP, rsc };
        }
        OutputPipeline BuildOutputRecord(ScenarioCtx &scen, const RecordingDirectoryResource &recordingDirectory, std::size_t iOutput, std::size_t iChannel) {
            auto &output = scen.outputs[iOutput];
            SimplePipelineBuilder b;
            auto &sink = b.AddVertex<RecordSink>();

            sink.fileName = recordingDirectory.GetOutputFileName(iOutput, iChannel);
            sink.sampleRate = output.params.sampleRate;
            sink.nChannels = 1;
            sink.center = output.params.centers[0];

            return { std::move(b), &sink.inP, { } };
        }


        struct SignalPipeline {
            SimplePipelineBuilder b;
            OutPort<SCGIQPacket> *port;
            std::vector<Resource*> resources;
        };
        SignalPipeline BuildSignalStream(ScenarioCtx &scen, std::size_t iLine) {
            auto &line = scen.lines[iLine];
            auto &output = scen.outputs[line.iOutput];
            std::vector<Resource*> resources;
            SimplePipelineBuilder b;
            auto &src = b.AddVertex<StreamSource>();
            auto &tun = b.AddVertex<Tuner>();
            b.AddEdge(src.outP, tun.inP);

            src.nSamples = scgPacketSize;
            src.sampleRate = output.params.sampleRate;
            src.events = GetSampleIndexed(scen, line.iOutput, line.events);
            src.generators.clear();
            for(auto &ie : line.events) {
                auto &e = scen.events[ie];
                auto [gen, rsc] = GetSignalGenerator(scen.scen.signals[e.iSignal].variant);
                gen->SetSampleRate(output.params.sampleRate);
                src.generators.push_back(std::move(gen));
                resources.insert(resources.end(), rsc.begin(), rsc.end());
            }

            tun.sampleRate = output.params.sampleRate;
            tun.freqOut = output.params.centers[line.iChannel];

            return { std::move(b), &tun.outP, resources };
        }
        SignalPipeline BuildSignalPlayback(ScenarioCtx &scen, const RecordingDirectoryResource &recordingDirectory, std::size_t iOutput, std::size_t iChannel) {
            auto &output = scen.outputs[iOutput];
            SimplePipelineBuilder b;
            auto &src = b.AddVertex<PlaybackSource>();

            src.fileName = recordingDirectory.GetOutputFileName(iOutput, iChannel);
            src.nSamples = scgPacketSize;

            std::vector<std::size_t> events;
            for(std::size_t il : output.lines[iChannel]) {
                events.insert(events.end(), scen.lines[il].events.begin(), scen.lines[il].events.end());
            }

            src.events = GetSampleIndexed(scen, iOutput, events);

            return { std::move(b), &src.outP, { } };
        }

    }




    PlaybackPipelineBuild BuildPlaybackPipeline(const std::string &recordingDirectory) {
        auto dir = std::make_unique<RecordingDirectoryResource>();
        dir->SetupRead(recordingDirectory);

        ScenarioCtx ctx = SortScenario(dir->GetScenario());
        std::vector<Resource*> resources;
        SimplePipelineBuilder b;
        std::vector<OutputPipeline> outputPipelines;
        std::vector<std::vector<SignalPipeline>> signalPipelines;

        for(std::size_t io = 0; io < ctx.outputs.size(); ++io) {
            auto p = BuildOutputStream(ctx, io);
            b = SimplePipelineBuilder::Merge(std::move(b), std::move(p.b));
            resources.insert(resources.end(), p.resources.begin(), p.resources.end());
            outputPipelines.push_back(std::move(p));
        }
        for(std::size_t io = 0; io < ctx.outputs.size(); ++io) {
            auto &output = ctx.outputs[io];
            signalPipelines.push_back({ });
            for(std::size_t ic = 0; ic < output.params.nChannels; ++ic) {
                auto p = BuildSignalPlayback(ctx, *dir, io, ic);
                b = SimplePipelineBuilder::Merge(std::move(b), std::move(p.b));
                resources.insert(resources.end(), p.resources.begin(), p.resources.end());
                signalPipelines.back().push_back(std::move(p));
            }
        }
        for(std::size_t io = 0; io < ctx.outputs.size(); ++io) {
            auto &output = ctx.outputs[io];

            std::vector<std::vector<std::size_t>> channelMap;
            for(std::size_t ic = 0; ic < output.params.nChannels; ++ic) {
                channelMap.push_back({ ic });
            }
            auto &merger = b.AddVertex<Merger>(output.params.nChannels, channelMap);
            merger.sampleRate = output.params.sampleRate;

            for(std::size_t ic = 0; ic < output.params.nChannels; ++ic) {
                b.AddEdge(*signalPipelines[io][ic].port, merger.inP[ic]);
            }
            b.AddEdge(merger.outP, *outputPipelines[io].port);
        }
        return { std::move(b), resources, std::move(dir) };
    }

    RecordingPipelineBuild BuildRecordingPipeline(Scenario &scen, const std::string &recordingDirectory) {
        ScenarioCtx ctx = SortScenario(scen);
        std::vector<Resource*> resources;
        SimplePipelineBuilder b;
        std::vector<std::vector<OutputPipeline>> outputPipelines;
        std::vector<SignalPipeline> signalPipelines;

        auto dir = std::make_unique<RecordingDirectoryResource>();
        dir->SetupWrite(ctx.scen, ".", recordingDirectory);

        for(std::size_t io = 0; io < ctx.outputs.size(); ++io) {
            auto &output = ctx.outputs[io];
            outputPipelines.push_back({ });
            for(std::size_t ic = 0; ic < output.params.nChannels; ++ic) {
            auto p = BuildOutputRecord(ctx, *dir, io, ic);
                b = SimplePipelineBuilder::Merge(std::move(b), std::move(p.b));
                resources.insert(resources.end(), p.resources.begin(), p.resources.end());
                outputPipelines.back().push_back(std::move(p));
            }
        }
        for(std::size_t il = 0; il < ctx.lines.size(); ++il) {
            auto p = BuildSignalStream(ctx, il);
            b = SimplePipelineBuilder::Merge(std::move(b), std::move(p.b));
            resources.insert(resources.end(), p.resources.begin(), p.resources.end());
            signalPipelines.push_back(std::move(p));
        }
        for(std::size_t io = 0; io < ctx.outputs.size(); ++io) {
            auto &output = ctx.outputs[io];
            for(std::size_t ic = 0; ic < output.params.nChannels; ++ic) {

                std::vector<std::vector<std::size_t>> channelMap;
                for([[maybe_unused]] std::size_t il : output.lines[ic]) {
                    channelMap.push_back({ 0 });
                }
                auto &merger = b.AddVertex<Merger>(1, channelMap);
                merger.sampleRate = output.params.sampleRate;

                std::size_t iPort = 0;
                for(std::size_t il : output.lines[ic]) {
                    b.AddEdge(*signalPipelines[il].port, merger.inP[iPort]);
                    ++iPort;
                }
                b.AddEdge(merger.outP, *outputPipelines[io][ic].port);
            }
        }

        return { std::move(b), resources, std::move(dir) };
    }

    StreamingPipelineBuild BuildStreamingPipeline(Scenario &scen) {
        ScenarioCtx ctx = SortScenario(scen);
        std::vector<Resource*> resources;
        SimplePipelineBuilder b;
        std::vector<OutputPipeline> outputPipelines;
        std::vector<SignalPipeline> signalPipelines;
        for(std::size_t io = 0; io < ctx.outputs.size(); ++io) {
            auto p = BuildOutputStream(ctx, io);
            b = SimplePipelineBuilder::Merge(std::move(b), std::move(p.b));
            resources.insert(resources.end(), p.resources.begin(), p.resources.end());
            outputPipelines.push_back(std::move(p));
        }
        for(std::size_t il = 0; il < ctx.lines.size(); ++il) {
            auto p = BuildSignalStream(ctx, il);
            b = SimplePipelineBuilder::Merge(std::move(b), std::move(p.b));
            resources.insert(resources.end(), p.resources.begin(), p.resources.end());
            signalPipelines.push_back(std::move(p));
        }
        for(std::size_t io = 0; io < ctx.outputs.size(); ++io) {
            auto &output = ctx.outputs[io];

            std::vector<std::vector<std::size_t>> channelMap;
            for(std::size_t ic = 0; ic < output.lines.size(); ++ic) {
                for(std::size_t il = 0; il < output.lines[ic].size(); ++il) {
                    channelMap.push_back({ ic });
                }
            }
            auto &merger = b.AddVertex<Merger>(output.params.nChannels, channelMap);
            merger.sampleRate = output.params.sampleRate;

            std::size_t iPort = 0;
            std::vector<std::vector<InPort<SCGIQPacket>*>> portsIn(output.lines.size());
            for(std::size_t ic = 0; ic < output.lines.size(); ++ic) {
                portsIn[ic].resize(output.lines[ic].size());
                for(std::size_t il = 0; il < output.lines[ic].size(); ++il) {
                    portsIn[ic][il] = &merger.inP[iPort];
                    ++iPort;
                }
            }

            for(std::size_t ic = 0; ic < output.lines.size(); ++ic) {
                for(std::size_t il = 0; il < output.lines[ic].size(); ++il) {
                    b.AddEdge(*signalPipelines[output.lines[ic][il]].port, *portsIn[ic][il]);
                }
            }
            b.AddEdge(merger.outP, *outputPipelines[io].port);
        }
        return { std::move(b), resources };
    }



}


