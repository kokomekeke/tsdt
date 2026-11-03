#include <vector>
#include <numbers>
#include <variant>
#include "Common/Assert.hpp"
#include "SignalTransform.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<SignalGenerator>,
        std::vector<Resource*>
    >
    GetSignalGenerator(
        [[maybe_unused]] const PipelineConfig &config,
        const SignalVariant &v,
        double duration_s
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
            p->loop = s->loop;
            basePtr = std::move(p);
        }
        if([[maybe_unused]] auto *s = std::get_if<SignalVariant::Zero>(&v.variant)) {
            auto p = std::make_unique<ZeroGenerator>();
            basePtr = std::move(p);
        }
        if(auto *s = std::get_if<SignalVariant::GnssSpoofing>(&v.variant)) {
            auto p = std::make_unique<GnssSpoofingGenerator>();
            p->Setup(*s, duration_s);
            basePtr = std::move(p);
        }

        Common::Assert(bool(basePtr), "failed to create generator");
        return { std::move(basePtr), resources };
    }

}
