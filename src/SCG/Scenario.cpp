#include <map>
#include "Common/Graph.hpp"
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "Pipeline/Pipeline.hpp"
#include "SSG/Modules/Resampler.hpp"
#include "SSG/Modules/Tuner.hpp"
#include "SSG/Modules/IQFileSource.hpp"
#include "SSG/Modules/IQFileSink.hpp"
#include "SSG/Modules/PacketBuffer.hpp"
#include "SSG/Modules/PacketSource.hpp"
#include "SSG/Modules/IQMerger.hpp"
#include "SCG/Scenario.hpp"
#include "SCG/Parsers.hpp"
#include "SCG/Modules/EventDelay.hpp"

namespace Sagax::SCG {
    using namespace Sagax::Pipeline;
    using namespace Sagax::Common;
    using namespace Sagax::SSG::IO;
    using namespace Sagax::SSG::Modules;
    using namespace Sagax::SCG::Modules;

    namespace {
        constexpr std::size_t packetSize = (1<<16);
    }

    struct ScenarioTransformer::Impl {
        Scenario &scen;

        Impl(Scenario& scen_) : scen(scen_) {}
        ~Impl() {}

        std::string GetOutputFileName(std::size_t iOutput) const {
            return std::format("{}_{:02}.wav", scen.name, iOutput);
        }


        // creates the signal generators and transformation modules
        // result will have one open output endpoint for each event
        PipelineBuilder ToGeneratorPipeline() {
            PipelineBuilder b;

            struct ChannelContext {
                std::size_t cEvents = 0; // count of incoming events
                std::size_t iMerger; // vertex id of merger module for the builder
                std::size_t iMergerPort = 0; // next port to connect to
            };
            struct OutputContext {
                std::size_t iMerger;
                std::vector<ChannelContext> channels;
            };
            std::vector<OutputContext> ctxOut(scen.outputs.size()); // count of events for each output
            for(std::size_t i = 0; i < scen.outputs.size(); ++i) {
                ctxOut[i].channels.resize(scen.outputs[i].nChannels);
            }

            for(std::size_t i = 0; i < scen.events.size(); ++i) {
                auto &e = scen.events[i];
                ctxOut[e.iOutput].channels[e.iChannel].cEvents += 1;
            }

            // create merger modules
            for(std::size_t iOutput = 0; iOutput < scen.outputs.size(); ++iOutput) {
                auto &ctxO = ctxOut[iOutput];
                auto nChannels = ctxO.channels.size();
                ctxO.iMerger = b.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<IQMerger>(nChannels);
                        mdl->policy = IQMerger::MergePolicy::channels;
                        return mdl;
                    },
                    nChannels,
                    1
                );
                for(std::size_t iChannel = 0; iChannel < nChannels; ++iChannel) {
                    auto &ctxC = ctxO.channels[iChannel];
                    auto cEvents = ctxC.cEvents;
                    if(cEvents > 0) {
                        ctxC.iMerger = b.AddVertex(
                            [=, *this](){
                                auto mdl = std::make_unique<IQMerger>(cEvents);
                                mdl->policy = IQMerger::MergePolicy::sum;
                                return mdl;
                            },
                            cEvents,
                            1
                        );
                    }
                    else { // if no events, we stream null packets into that channel
                        auto sampleRate = scen.outputs[iOutput].sampleRate;
                        auto iPs = b.AddVertex(
                            [=, *this](){
                                auto mdl = std::make_unique<IQPacketSource>();
                                mdl->nChannels = 1;
                                mdl->nSamples = packetSize;
                                mdl->sampleRate = sampleRate;
                                mdl->infinite = true;
                                return mdl;
                            },
                            0,
                            1
                        );
                        auto iEventDelay = b.AddVertex(
                            [=, *this](){
                                auto mdl = std::make_unique<EventDelay>();
                                mdl->nSamples = packetSize;
                                mdl->sampleRate = sampleRate;
                                mdl->tStart = 0.0;
                                mdl->tEnd = 0.0;
                                mdl->tStop = scen.duration;
                                return mdl;
                            },
                            1,
                            1
                        );
                        b.AddEdge({ iPs, 0 }, { iEventDelay, 0 });
                        ctxC.iMerger = iEventDelay;
                    }
                    b.AddEdge({ ctxC.iMerger, 0 }, { ctxO.iMerger, iChannel });
                }
            }

            // create signal + transform pipelines
            for(std::size_t i = 0; i < scen.events.size(); ++i) {
                auto &event = scen.events[i];
                auto &signal = scen.signals[event.iSignal];
                auto &output = scen.outputs[event.iOutput];

                PipelineBuilder bTransform;
                auto iEventDelay = bTransform.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<EventDelay>();
                        mdl->nSamples = packetSize;
                        mdl->sampleRate = 100000.0; // dummy value for null packets
                        mdl->tStart = event.start;
                        mdl->tEnd = event.end;
                        mdl->tStop = scen.duration;
                        return mdl;
                    },
                    1,
                    1
                );
                auto iResampler = bTransform.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<Resampler>();
                        mdl->sampleRate = output.sampleRate;
                        return mdl;
                    },
                    1,
                    1
                );
                auto ampl = event.amplitude;
                auto freq = event.frequency - output.frequencies[event.iChannel];
                auto iTuner = bTransform.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<Tuner>();
                        mdl->ampl = ampl;
                        mdl->frequency = freq;
                        return mdl;
                    },
                    1,
                    1
                );
                auto iBuffer = bTransform.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<PacketBuffer<std::complex<double>>>();
                        mdl->nSamples = packetSize;
                        mdl->stride = StridePolicy::NoStride{};
                        mdl->ignoreMetadata = true;
                        return mdl;
                    },
                    1,
                    1
                );
                bTransform.AddEdge({iEventDelay, 0}, {iResampler, 0});
                bTransform.AddEdge({iResampler, 0}, {iTuner, 0});
                bTransform.AddEdge({iTuner, 0}, {iBuffer, 0});

                PipelineBuilder &bSignal = signal.builder;
                Common::Assert(bSignal.openOut.size() == 1, "signal pipeline must have 1 open output endpoint");
                Common::Assert(bSignal.openIn.size() == 0, "signal pipeline must have no open input endpoint");

                bTransform.Merge(true, bSignal, bSignal.openOut, bTransform.openIn);

                auto &ctx = ctxOut[event.iOutput].channels[event.iChannel];
                b.Merge(true, bTransform, bTransform.openOut, {{ctx.iMerger, ctx.iMergerPort}});
                ++ctx.iMergerPort;
            }

            Common::Assert(b.openOut.size() == scen.outputs.size(), "pipeline builder output count mismatch");
            Common::Assert(b.openIn.size() == 0, "pipeline builder input count mismatch");

            return b;
        }

        // creates only the output transmitter modules
        // leaves an input endpoint open for each
        PipelineBuilder ToTransmitterPipeline() {
            PipelineBuilder b;
            for(std::size_t i = 0; i < scen.outputs.size(); ++i) {
                PipelineBuilder &bOut = scen.outputs[i].builder;
                Common::Assert(bOut.openIn.size() == 1, "output builder must have 1 open input endpoint");
                Common::Assert(bOut.openOut.size() == 0, "output builder cannot have open output endpoints");
                b.Merge(false, bOut, {}, {}); // keep endpoints open
            }
            return b;
        }

        // creates only the file reader modules
        // leaves an output endpoint open for each
        PipelineBuilder ToReaderPipeline() {
            PipelineBuilder b;
            for(std::size_t i = 0; i < scen.outputs.size(); ++i) {
                b.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<IQFileSource>();
                        mdl->filename = GetOutputFileName(i);
                        mdl->mode = FilePlaybackMode::File{};
                        mdl->nSamples = (1<<12);
                        return mdl;
                    },
                    0,
                    1
                );
            }
            return b;
        }

        // creates only the file writer modules
        // leaves an input endpoint open for each
        PipelineBuilder ToWriterPipeline() {
            PipelineBuilder b;
            for(std::size_t i = 0; i < scen.outputs.size(); ++i) {
                b.AddVertex(
                    [=, *this](){
                        auto mdl = std::make_unique<IQFileSink>();
                        mdl->filename = GetOutputFileName(i);
                        return mdl;
                    },
                    1,
                    0
                );
            }
            return b;
        }

        PipelineBuilder ToPlaybackPipeline() {
            PipelineBuilder br = ToReaderPipeline();
            PipelineBuilder bt = ToTransmitterPipeline();
            br.Merge(false, bt, br.openOut, bt.openIn);
            return br;
        }

        PipelineBuilder ToRecordingPipeline() {
            PipelineBuilder bg = ToGeneratorPipeline();
            PipelineBuilder bw = ToWriterPipeline();
            bg.Merge(false, bw, bg.openOut, bw.openIn);
            return bg;
        }

        PipelineBuilder ToStreamingPipeline() {
            PipelineBuilder bg = ToGeneratorPipeline();
            PipelineBuilder bt = ToTransmitterPipeline();
            bg.Merge(false, bt, bg.openOut, bt.openIn);
            return bg;
        }

    };


    ScenarioTransformer::ScenarioTransformer(Scenario& scen_): impl(std::make_unique<Impl>(scen_)) {
    }

    ScenarioTransformer::~ScenarioTransformer() = default;

    PipelineBuilder ScenarioTransformer::ToPlaybackPipeline() { return impl->ToPlaybackPipeline(); }
    PipelineBuilder ScenarioTransformer::ToRecordingPipeline() { return impl->ToRecordingPipeline(); }
    PipelineBuilder ScenarioTransformer::ToStreamingPipeline() { return impl->ToStreamingPipeline(); }

}