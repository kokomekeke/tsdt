#include <cstdint>
#include <utility>
#include <format>
#include <variant>
#include <map>
#include "Validate.hpp"

namespace Sagax::SCG {


    namespace {
        void ValidateSignal(ValidateScenarioResult &res, Scenario &scen, std::size_t i) {
            auto &sig = scen.signals[i];
            if(std::holds_alternative<SignalVariant::Null>(sig.variant.variant)) {
                res.errors.push_back(std::format("signal[{}]: null signal variant", i));
            }
            // else if(std::holds_alternative<) ...
        }

        void ValidateOutput(ValidateScenarioResult &res, Scenario &scen, std::size_t i) {
            auto &out = scen.outputs[i];
            if(std::holds_alternative<OutputVariant::Null>(out.variant.variant)) {
                res.errors.push_back(std::format("output[{}]: null output variant", i));
            }
        }
    }


    ValidateScenarioResult ValidateScenario(Scenario &scen) {
        ValidateScenarioResult res;

        bool doCheckEvents = true;

        std::map<std::string, std::size_t> idxOut;
        std::map<std::string, std::size_t> idxSig;

        {
            for(std::size_t i = 0; i < scen.outputs.size(); ++i) {
                auto &v = scen.outputs[i];
                if(idxOut.count(v.name)) {
                    res.errors.push_back(std::format("output[{}]: name already used: {}", i, v.name.c_str()));
                    doCheckEvents = false;
                    continue;
                }
                idxOut[v.name] = i;
            }
            for(std::size_t i = 0; i < scen.signals.size(); ++i) {
                auto &v = scen.signals[i];
                if(idxOut.count(v.name)) {
                    res.errors.push_back(std::format("signal[{}]: name already used: {}", i, v.name.c_str()));
                    doCheckEvents = false;
                    continue;
                }
                idxSig[v.name] = i;
            }
        }

        {
            for(std::size_t i = 0; i < scen.signals.size(); ++i) {
                ValidateSignal(res, scen, i);
            }
            for(std::size_t i = 0; i < scen.outputs.size(); ++i) {
                ValidateOutput(res, scen, i);
            }
        }

        if(doCheckEvents) {
            for(std::size_t i = 0; i < scen.events.size(); ++i) {
                auto &v = scen.events[i];
                bool ok = true;
                if(idxOut.count(v.outputName) == 0) {
                    res.errors.push_back(std::format("event[{}]: missing output: {}", i, v.outputName.c_str()));
                    ok = false;
                }
                if(idxSig.count(v.signalName) == 0) {
                    res.errors.push_back(std::format("event[{}]: missing signal: {}", i, v.signalName.c_str()));
                    ok = false;
                }
                if(!ok) {
                    continue;
                }

                auto &out = scen.outputs[idxOut[v.outputName]];
                // auto &sig = scen.signals[idxSig[v.signalName]];

                auto outP = out.variant.GetCommonParams();

                if(v.iChannel >= outP.nChannels) {
                    res.errors.push_back(std::format("events[{}]: channel index out of range", i));
                }
                else {
                    if(v.frequency < outP.centers[v.iChannel] - outP.sampleRate / 2.0 || v.frequency > outP.centers[v.iChannel] + outP.sampleRate / 2.0) {
                        res.errors.push_back(std::format("events[{}]: frequency is out of range for this channel", i));
                    }
                }

                if(v.start > v.end) {
                    res.errors.push_back(std::format("event[{}]: start time > end time", i));
                }
                if(v.start < 0.0) {
                    res.errors.push_back(std::format("event[{}]: start time < 0", i));
                }
                if(v.end > scen.duration) {
                    res.errors.push_back(std::format("event[{}]: end time > duration", i));
                }

                // TODO check amplitude range
            }
        }

        // TODO check for all outputs having some events (warning)


        return res;
    }
}

