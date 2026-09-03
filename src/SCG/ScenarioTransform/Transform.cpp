#include <map>
#include <vector>
#include <numbers>
#include <variant>
#include <algorithm>
#include <format>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SCG/Output/FileOutputDevice.hpp"
#include "SCG/Output/UsrpB200Device.hpp"
#include "SCG/Output/SidekiqDevice.hpp"
#include "Transform.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<OutputDevice>,
        std::vector<Resource*>
    >
    GetOutputDevice(
        [[maybe_unused]] const PipelineConfig &config,
        const OutputVariant &v
    ) {
        std::unique_ptr<OutputDevice> basePtr;
        std::vector<Resource*> resources;
        if([[maybe_unused]] auto *s = std::get_if<OutputVariant::Null>(&v.variant)) {
            Common::Assert(false, "null output");
        }
        if(auto *s = std::get_if<OutputVariant::FileIQ>(&v.variant)) {
            auto p = std::make_unique<FileOutputDevice>();
            p->fileName = s->fileName;
            p->nChannels = s->nChannels;
            p->sampleRate = s->sampleRate;
            p->center = s->center;
            basePtr = std::move(p);
        }

        if([[maybe_unused]] auto *s = std::get_if<OutputVariant::RadioUsrpB200>(&v.variant)) {
#ifdef HAVE_USRP
            auto p = std::make_unique<UsrpB200Device>();
            p->serial = s->serial;
            p->subdevSpec = s->subdevSpec;
            p->sampleRate = s->sampleRate;
            p->center = s->center;
            p->gain = s->gain;
            {
                auto &u = config.preinit.usrp;
                auto it = u.devices.find(s->serial);
                if(it == u.devices.end()) {
                    Common::Assert(!u.usePreinitOnly, "device was not preinitialized");
                    p->dev = nullptr;
                }
                else {
                    p->dev = it->second.get();
                }
            }
            resources.push_back((Resource*)(p.get()));
            basePtr = std::move(p);
#else
            Common::Assert(false, "usrp output device not available");
#endif
        }
        if([[maybe_unused]] auto *s = std::get_if<OutputVariant::RadioSidekiq>(&v.variant)) {
#if HAVE_SIDEKIQ

            auto p = std::make_unique<SidekiqDevice>();
            p->serial = s->serial;
            p->nChannels = s->nChannels;
            p->sampleRate = s->sampleRate;
            p->centers = s->centers;
            p->gain = s->gain;
            p->handlers = s->handlers;
            {
                auto &u = config.preinit.sidekiq;
                auto it = u.devices.find(s->serial);
                if(it == u.devices.end()) {
                    // Common::Assert(!u.usePreinitOnly, "device was not preinitialized");
                    p->dev = nullptr;
                }
                else {
                    p->dev = it->second.get();
                }
            }
            resources.push_back((Resource*)(p.get()));
            basePtr = std::move(p);
#else
            Common::Assert(false, "sidekiq output device not available");
#endif
        }
        Common::Assert(bool(basePtr), "failed to create output device");
        return { std::move(basePtr), resources };
    }


    std::tuple<
        std::unique_ptr<SignalGenerator>,
        std::vector<Resource*>
    >
    GetSignalGenerator(
        [[maybe_unused]] const PipelineConfig &config,
        const SignalVariant &v
    ) {
        std::unique_ptr<SignalGenerator> basePtr;
        std::vector<Resource*> resources;
        if([[maybe_unused]] auto *s = std::get_if<SignalVariant::Null>(&v.variant)) {
            Common::Assert(false, "null signal");
        }
        if(auto *s = std::get_if<SignalVariant::Noise>(&v.variant)) {
            auto p = std::make_unique<NoiseGenerator>();
            p->Setup(s->span, 1e-5, 1<<15, 8);
            basePtr = std::move(p);
        }
        if([[maybe_unused]] auto *s = std::get_if<SignalVariant::Tone>(&v.variant)) {
            auto p = std::make_unique<ToneGenerator>();
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::Chirp>(&v.variant)) {
            auto p = std::make_unique<ChirpGenerator>();
            p->span = s->span;
            p->rate = s->rate;
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::Pulse>(&v.variant)) {
            auto p = std::make_unique<PulseGenerator>();
            p->duty = s->duty;
            p->rate = s->rate;
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
                    auto mod = std::make_unique<SSG::Modulators::FMModulator<SCGSampleBase>>();
                    mod->freq = 0.0;
                    mod->ampl = 1.0;
                    mod->deviation = 50000.0;
                    p->mod = std::move(mod);
                } break;
                case SignalVariant::AudioFile::Modulation::AM: {
                    auto mod = std::make_unique<SSG::Modulators::AMModulator<SCGSampleBase>>();
                    mod->freq = 0.0;
                    mod->ampl = 0.5;
                    mod->deviation = 0.3;
                    p->mod = std::move(mod);
                } break;
                case SignalVariant::AudioFile::Modulation::PM: {
                    auto mod = std::make_unique<SSG::Modulators::PMModulator<SCGSampleBase>>();
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
        return { std::move(basePtr), resources };
    }

}

