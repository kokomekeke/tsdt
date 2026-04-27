#include <complex>
#include <numbers>
#include <vector>
#include <span>
#include <cmath>
#include "Common/Logger.hpp"
#include "Common/Assert.hpp"
#include "SSG/Math/Math.hpp"
#include "MEventTag.hpp"
#include "Tuner.hpp"

namespace Sagax::SCG {

    struct Tuner::Impl {
        double phase;
        double omega;
        std::vector<SCGSample> tone;

        void SetTone(double sampleRate_, double freqTune_, std::size_t size_) {
            double omega_ = 2.0 * std::numbers::pi * (freqTune_ / sampleRate_);

            if(omega_ != omega) {
                tone.clear();
                omega = omega_;
            }

            std::size_t size = tone.size();
            if(size_ > size) {
                tone.resize(size_);
                for(std::size_t i = size; i < size_; ++i) {
                    tone[i] = SCGSample(std::polar(1.0, double(i) * omega));
                }
            }
        }

        void ApplyTone(std::span<SCGSample> out, double ampl) {
            std::size_t n = out.size();
            if(n == 0) {
                return;
            }
            Common::Assert(n <= tone.size(), "tone not set");

            SCGSample p = SCGSample(std::polar(ampl, phase));
            phase = std::fmod(phase + omega * double(n), 2.0 * std::numbers::pi);
            SSG::Math::MulV<SCGSample>(out, std::span(tone.begin(), tone.begin() + n));
            SSG::Math::MulC<SCGSample>(out, p);
        }
    };

    Tuner::Tuner() : impl(std::make_unique<Impl>()) {
    }

    Tuner::~Tuner() = default;

    void Tuner::Reset() {
        impl->phase = 0.0;
    }

    void Tuner::Start() {
    }

    void Tuner::Stop() {
    }

    void Tuner::Process() {
        if (inP.nItems() == 0 || outP.nSpaces() == 0) {
            return;
        }
        auto &input = inP.Pop();
        auto &output = outP.Push();

        Common::Assert(input.nChannels == 1, "multiple channels");
        Common::Assert(input.sampleRate == sampleRate, "wrong samplerate");

        auto *mEntry = input.meta.GetEntry<MEventTag>();
        if(!mEntry || mEntry->events.empty() || input.null) {
            std::swap(input, output);
            return;
        }
        Common::Assert(mEntry && mEntry->events.size() == 1, "multiple events");
        auto &event = mEntry->events[0];

        output = input;
        output.SetNull(false);

        double freqTune = event.frequency - freqOut;
        double amplTune = event.amplitude;

        impl->SetTone(sampleRate, freqTune, input.nSamples);
        impl->ApplyTone(input.data, amplTune);
    }

}


