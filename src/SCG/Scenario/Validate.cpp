#include <cmath>
#include <cstdint>
#include <utility>
#include <format>
#include <variant>
#include <map>
#include "Validate.hpp"

namespace Sagax::SCG {


    namespace {
        void ValidateSignal(ValidationResult &res, Scenario &scen, std::size_t i) {
            auto &sig = scen.signals[i];
            if(std::holds_alternative<SignalVariant::Null>(sig.variant.variant)) {
                res.errors.push_back(std::format("signal[{}]: null signal variant", i));
            }
            else if(auto *v = std::get_if<SignalVariant::GnssSpoofing>(&sig.variant.variant)) {
                if(v->ephemerisFile.empty()) {
                    res.errors.push_back(std::format("signal[{}]: gnss spoofing without an ephemeris file", i));
                }
                if(v->trajectory.empty()) {
                    res.errors.push_back(std::format("signal[{}]: gnss spoofing without a trajectory", i));
                }
                else if(v->trajectory.front().t != 0.0) {
                    res.errors.push_back(std::format("signal[{}]: the first waypoint has to be at t = 0", i));
                }
                for(std::size_t iw = 0; iw < v->trajectory.size(); ++iw) {
                    const auto &w = v->trajectory[iw];
                    if(w.latitude < -90.0 || w.latitude > 90.0) {
                        res.errors.push_back(std::format("signal[{}]: trajectory[{}]: latitude out of range", i, iw));
                    }
                    if(w.longitude < -180.0 || w.longitude > 180.0) {
                        res.errors.push_back(std::format("signal[{}]: trajectory[{}]: longitude out of range", i, iw));
                    }
                    if(iw > 0 && w.t <= v->trajectory[iw - 1].t) {
                        res.errors.push_back(std::format("signal[{}]: trajectory[{}]: time does not advance", i, iw));
                    }
                }
            }
            // else if(std::holds_alternative<) ...
        }

        void ValidateOutput(ValidationResult &res, Scenario &scen, std::size_t i) {
            auto &out = scen.outputs[i];
            if(std::holds_alternative<OutputVariant::Null>(out.variant.variant)) {
                res.errors.push_back(std::format("output[{}]: null output variant", i));
            }
        }
    }


    ValidationResult ValidateScenario(Scenario &scen) {
        ValidationResult res;

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
                auto &sig = scen.signals[idxSig[v.signalName]];

                auto outP = out.variant.GetCommonParams();

                // the gnss engine generates at the rate of the output
                if(std::holds_alternative<SignalVariant::GnssSpoofing>(sig.variant.variant)
                    && std::fmod(outP.sampleRate, 1000.0) != 0.0
                ) {
                    res.errors.push_back(std::format(
                        "event[{}]: gnss spoofing needs an output sample rate of whole khz, {} has {}",
                        i, v.outputName.c_str(), outP.sampleRate));
                }

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

