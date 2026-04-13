#include <complex>
#include <numbers>
#include <cmath>
#include "MEventTag.hpp"
#include "Tuner.hpp"

namespace Sagax::SCG {

    void Tuner::Reset() {
        phase = 0.0;
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

        auto *mEntry = input.meta.GetEntry<MEventTag>();
        if(!mEntry || mEntry->events.empty() || input.null) {
            std::swap(input, output);
            return;
        }
        Common::Assert(mEntry && mEntry->events.size() == 1, "multiple events");
        auto &event = mEntry->events[0];

        output.Reshape(input.sampleRate, input.nChannels, input.nSamples, false);
        output.meta = input.meta;


        double freqTune = event.frequency - freqOut;
        double amplTune = event.amplitude;

        auto ci = input.GetChannels();
        auto co = output.GetChannels();
        double p = phase;
        double dp = 2.0 * std::numbers::pi * (freqTune / input.sampleRate);

        // TODO something much faster
        for (std::size_t i = 0; i < output.nSamples; ++i) {
            std::complex<double> phasor = std::polar(amplTune, p);
            for (std::size_t ch = 0; ch < output.nChannels; ++ch) {
                co[ch][i] = ci[ch][i] * phasor;
            }
            p += dp;
        }

        phase = std::fmod(p, 2.0 * std::numbers::pi);
    }

}


