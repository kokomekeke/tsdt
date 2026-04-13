#include <map>
#include <vector>
#include <numbers>
#include <variant>
#include <algorithm>
#include <format>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "Transform.hpp"

namespace Sagax::SCG {

    std::unique_ptr<OutputDevice> GetOutputDevice(OutputVariant &v) {
        std::unique_ptr<OutputDevice> basePtr;
        if(auto *s = std::get_if<OutputVariant::Null>(&v.variant)) {
            Common::Assert(false, "null output");
        }
        if(auto *s = std::get_if<OutputVariant::RadioUsrpB200>(&v.variant)) {
#ifdef HAVE_USRP
            auto p = std::make_unique<UsrpB200Device>();
            auto dev = std::make_unique<SSG::UsrpB200::Device>(std::format("serial={}", s->serial), s->subdevSpec, 1);
            SSG::UsrpB200::DeviceParams params;
            params.sampleRate = s->sampleRate;
            params.bandwidth = s->sampleRate + 1.0;
            params.centers = { s->center };
            params.gains = { s->gain };
            dev->Configure(params);
            Common::Assert(abs(dev->params.sampleRate - s->sampleRate) < 1.0, "could not set samplerate");
            if(abs(dev->params.bandwidth - (s->sampleRate + 1.0)) >= 1.0) {
                Common::Logger::Warn("could not set bandwidth");
            }
            Common::Assert(abs(dev->params.centers[0] - s->center) < 1.0, "could not set frequency");
            if(abs(dev->params.gains[0] - s->gain) >= 1.0) {
                Common::Logger::Warn("could not set gain");
            }
            std::this_thread::sleep_for(std::chrono::seconds(1)); // required magic wait
            p->dev = std::move(dev);
            basePtr = std::move(p);
#else
            Common::Assert(false, "usrp output device not available");
#endif
        }
        if(auto *s = std::get_if<OutputVariant::FileIQ>(&v.variant)) {
            auto p = std::make_unique<FileOutputDevice>();
            p->fileName = s->fileName;
            p->nChannels = s->nChannels;
            p->sampleRate = s->sampleRate;
            p->center = s->center;
            basePtr = std::move(p);
        }

        Common::Assert(bool(basePtr), "failed to create output device");
        return basePtr;
    }


    std::unique_ptr<SignalGenerator> GetSignalGenerator(SignalVariant &v) {
        std::unique_ptr<SignalGenerator> basePtr;
        if(auto *s = std::get_if<SignalVariant::Null>(&v.variant)) {
            Common::Assert(false, "null signal");
        }
        if(auto *s = std::get_if<SignalVariant::Noise>(&v.variant)) {
            auto p = std::make_unique<NoiseGenerator>();
            p->Setup(s->span, 1e-5, 1<<15, 8);
            p->sampleRate = s->span;
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::Tone>(&v.variant)) {
            auto p = std::make_unique<ToneGenerator>();
            p->sampleRate = 10000.0; // arbitrary
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::Chirp>(&v.variant)) {
            auto p = std::make_unique<ChirpGenerator>();
            p->sampleRate = s->span * 3.0;
            p->span = s->span;
            p->rate = s->rate;
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::Pulse>(&v.variant)) {
            auto p = std::make_unique<PulseGenerator>();
            p->sampleRate = s->duty * 1000.0;
            p->duty = s->duty;
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::AudioFile>(&v.variant)) {
            auto p = std::make_unique<AudioFileGenerator>();
            p->fileName = s->fileName;
            switch(s->mod) {
                case SignalVariant::AudioFile::Modulation::IQ: {
                    Common::Assert(false, "iq modulation unimplemented");
                } break;
                case SignalVariant::AudioFile::Modulation::FM: {
                    auto mod = std::make_unique<SSG::Modulators::FMModulator>();
                    mod->freq = 0.0;
                    mod->ampl = 1.0;
                    mod->deviation = 50000.0;
                    p->mod = std::move(mod);
                } break;
                case SignalVariant::AudioFile::Modulation::AM: {
                    auto mod = std::make_unique<SSG::Modulators::AMModulator>();
                    mod->freq = 0.0;
                    mod->ampl = 0.5;
                    mod->deviation = 0.3;
                    p->mod = std::move(mod);
                } break;
                case SignalVariant::AudioFile::Modulation::PM: {
                    auto mod = std::make_unique<SSG::Modulators::PMModulator>();
                    mod->freq = 0.0;
                    mod->ampl = 1.0;
                    mod->deviation = std::numbers::pi;
                    p->mod = std::move(mod);
                } break;
            }
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::IQFile>(&v.variant)) {
            auto p = std::make_unique<IQFileGenerator>();
            p->fileName = s->fileName;
            basePtr = std::move(p);
        }

        Common::Assert(bool(basePtr), "failed to create generator");
        return basePtr;
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
                if(std::holds_alternative<SignalVariant::Null>(scen.signals[i].variant.variant)) {
                    res.errors.push_back(std::format("signal[{}]: null signal variant", i));
                }
            }
            for(std::size_t i = 0; i < scen.outputs.size(); ++i) {
                if(std::holds_alternative<OutputVariant::Null>(scen.outputs[i].variant.variant)) {
                    res.errors.push_back(std::format("output[{}]: null output variant", i));
                }
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

