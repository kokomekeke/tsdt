#include <vector>
#include <algorithm>
#include <utility>

#include "Common/Assert.hpp"

#include "SCG/Scenario/Scenario.hpp"
#include "RecordingDirectoryResource.hpp"
#include "SCG/Output/OutputTransform.hpp"
#include "SCG/Signal/SignalTransform.hpp"
#include "BuildPipeline.hpp"

namespace Sagax::SCG {

    namespace {

        // the outputs and the events of the scenario in samples of the outputs, the events ordered by
        // their start. scenEvents gets the index in the scenario of every event of the layout
        ScenarioLayout SortScenario(const Scenario &scen, ScenarioLayout::Mode mode, std::vector<std::int32_t> &scenEvents) {
            ScenarioLayout layout;
            layout.mode = mode;

            for(auto &o : scen.outputs) {
                auto params = o.variant.GetCommonParams();
                ScenarioLayout::Output output;
                output.sampleRate = params.sampleRate;
                output.nChannels = params.nChannels;
                output.centers = params.centers;
                output.nSamples = std::max(std::int64_t(0), std::int64_t(scen.duration * params.sampleRate));
                layout.outputs.push_back(std::move(output));
            }

            std::vector<std::pair<std::int32_t, ScenarioLayout::Event>> events;
            for(std::size_t ie = 0; ie < scen.events.size(); ++ie) {
                auto &e = scen.events[ie];
                std::size_t iOutput = std::size_t(std::find_if(scen.outputs.begin(), scen.outputs.end(), [&](const Scenario::Output &o) { return o.name == e.outputName; }) - scen.outputs.begin());
                Common::Assert(iOutput < scen.outputs.size(), "missing output");
                auto &output = layout.outputs[iOutput];
                Common::Assert(e.iChannel < output.nChannels, "channel index out of range");

                std::int64_t iStart = std::clamp(std::int64_t(e.start * output.sampleRate), std::int64_t(0), output.nSamples);
                std::int64_t iEnd = std::clamp(std::int64_t(e.end * output.sampleRate), std::int64_t(0), output.nSamples);
                if(iStart >= iEnd) {
                    continue;
                }
                events.push_back({ std::int32_t(ie), ScenarioLayout::Event{
                    iOutput,
                    e.iChannel,
                    iStart,
                    iEnd,
                    e.frequency,
                    e.amplitude,
                    nullptr
                } });
            }
            std::stable_sort(events.begin(), events.end(), [](const auto &e1, const auto &e2) {
                return e1.second.iStart < e2.second.iStart;
            });
            for(auto &[ie, e] : events) {
                scenEvents.push_back(ie);
                layout.events.push_back(std::move(e));
            }
            for(std::size_t ie = 0; ie < layout.events.size(); ++ie) {
                layout.outputs[layout.events[ie].iOutput].events.push_back(ie);
            }
            return layout;
        }

        std::vector<Resource*> AddGenerators(const PipelineConfig &config, const Scenario &scen, const std::vector<std::int32_t> &scenEvents, ScenarioLayout &layout) {
            std::vector<Resource*> resources;
            for(std::size_t ie = 0; ie < layout.events.size(); ++ie) {
                auto &e = layout.events[ie];
                auto &event = scen.events[std::size_t(scenEvents[ie])];
                auto it = std::find_if(scen.signals.begin(), scen.signals.end(), [&](const Scenario::Signal &s) { return s.name == event.signalName; });
                Common::Assert(it != scen.signals.end(), "missing signal");
                auto [gen, rsc] = GetSignalGenerator(config, it->variant, event.end - event.start);
                gen->SetSampleRate(layout.outputs[e.iOutput].sampleRate);
                e.generator = std::move(gen);
                resources.insert(resources.end(), rsc.begin(), rsc.end());
            }
            return resources;
        }

        std::vector<Resource*> AddDevices(const PipelineConfig &config, const Scenario &scen, ScenarioLayout &layout) {
            std::vector<Resource*> resources;
            for(std::size_t io = 0; io < scen.outputs.size(); ++io) {
                auto [dev, rsc] = GetOutputDevice(config, scen.outputs[io].variant);
                layout.outputs[io].device = std::move(dev);
                resources.insert(resources.end(), rsc.begin(), rsc.end());
            }
            return resources;
        }

        void AddFiles(const RecordingDirectoryResource &dir, ScenarioLayout &layout) {
            for(std::size_t io = 0; io < layout.outputs.size(); ++io) {
                auto &output = layout.outputs[io];
                for(std::size_t ic = 0; ic < output.nChannels; ++ic) {
                    output.files.push_back(dir.GetOutputFileName(io, ic));
                }
            }
        }

    }



    ScenarioPipeline BuildPlaybackPipeline(
        const PipelineConfig &config,
        const std::string &recordingDirectory
    ) {
        auto dir = std::make_unique<RecordingDirectoryResource>();
        dir->SetupRead(recordingDirectory);
        const Scenario &scen = dir->GetScenario();

        ScenarioPipeline ctx;
        std::vector<std::int32_t> scenEvents;
        ScenarioLayout layout = SortScenario(scen, ScenarioLayout::Mode::playback, scenEvents);
        ctx.resources = AddDevices(config, scen, layout);
        AddFiles(*dir, layout);
        layout.callbacks = config.events;
        ctx.SetLayout(std::move(layout));
        ctx.ownedResources.push_back(std::move(dir));
        return ctx;
    }

    ScenarioPipeline BuildRecordingPipeline(
        const PipelineConfig &config,
        const Scenario &scen,
        const std::string &recordingDirectory
    ) {
        auto dir = std::make_unique<RecordingDirectoryResource>();
        dir->SetupWrite(scen, ".", recordingDirectory);

        ScenarioPipeline ctx;
        std::vector<std::int32_t> scenEvents;
        ScenarioLayout layout = SortScenario(scen, ScenarioLayout::Mode::record, scenEvents);
        ctx.resources = AddGenerators(config, scen, scenEvents, layout);
        AddLines(layout);
        AddFiles(*dir, layout);
        ctx.SetLayout(std::move(layout));
        ctx.ownedResources.push_back(std::move(dir));
        return ctx;
    }

    ScenarioPipeline BuildStreamingPipeline(
        const PipelineConfig &config,
        const Scenario &scen
    ) {
        ScenarioPipeline ctx;
        std::vector<std::int32_t> scenEvents;
        ScenarioLayout layout = SortScenario(scen, ScenarioLayout::Mode::stream, scenEvents);
        ctx.resources = AddGenerators(config, scen, scenEvents, layout);
        AddLines(layout);
        std::vector<Resource*> devices = AddDevices(config, scen, layout);
        ctx.resources.insert(ctx.resources.end(), devices.begin(), devices.end());
        layout.callbacks = config.events;
        ctx.SetLayout(std::move(layout));
        return ctx;
    }

}
