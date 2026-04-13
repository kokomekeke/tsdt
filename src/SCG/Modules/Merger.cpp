#include "MEventTag.hpp"
#include "Merger.hpp"

namespace Sagax::SCG {

    using SSG::Packets::IQPacket;

    struct Merger::Impl {
        std::vector<IQPacket> inputs;
        std::vector<bool> inputValid;
        std::vector<std::size_t> nSamplesRead;
    };

    Merger::Merger(
        std::size_t nChannelsOut_,
        std::vector<std::vector<std::size_t>> channelMap_
    ) :
        impl(std::make_unique<Impl>())
    {
        nChannelsOut = nChannelsOut_;
        channelMap = channelMap_;
        std::size_t nInputs = channelMap.size();
        for(std::size_t i = 0; i < nInputs; ++i) {
            for(std::size_t j = 0; j < channelMap[i].size(); ++j) {
                Common::Assert(channelMap[i][j] >= 0 && channelMap[i][j] < nChannelsOut, "invalid channelMap");
            }
        }
        inP.resize(nInputs);
        for(std::size_t i = 0; i < nInputs; ++i) {
            inP[i] = AddInPort(&inP[i]);
        }

        impl->inputs.resize(nInputs);
        impl->inputValid.assign(nInputs, false);
        impl->nSamplesRead.assign(nInputs, 0);
    }

    Merger::~Merger() {
    }

    void Merger::Reset() {
        std::size_t nInputs = channelMap.size();
        impl->inputValid.assign(nInputs, false);
        impl->nSamplesRead.assign(nInputs, 0);
    }

    void Merger::Start() {
    }

    void Merger::Stop() {
    }

    void Merger::Process() {
        std::size_t nInputs = channelMap.size();
        if(nInputs == 0) {
            return;
        }

        for(std::size_t i = 0; i < nInputs; ++i) {
            if(!impl->inputValid[i] && inP[i].nItems() == 0) {
                return;
            }
        }
        if(outP.nSpaces() == 0) {
            return;
        }

        std::size_t nSamplesNow = 0;
        for(std::size_t i = 0; i < nInputs; ++i) {
            if(!impl->inputValid[i]) {
                std::swap(impl->inputs[i], inP[i].Pop());
                impl->inputValid[i] = true;
                impl->nSamplesRead[i] = 0;
            }
            Common::Assert(impl->inputs[i].sampleRate == sampleRate, "samplerate mismatch");
            Common::Assert(impl->inputs[i].nChannels == channelMap[i].size(), "channelcount mismatch");

            std::size_t nSamplesAvailable = impl->inputs[i].nSamples - impl->nSamplesRead[i];
            if(i == 0 || nSamplesAvailable < nSamplesNow) {
                nSamplesNow = nSamplesAvailable;
            }
        }

        IQPacket &output = outP.Push();
        output.meta.Clear();
        output.Reshape(sampleRate, nChannelsOut, nSamplesNow, false);
        output.Fill(0.0);
        auto &mOut = output.meta.AddEntry<MEventTag>();
        auto co = output.GetChannels();

        for(std::size_t i = 0; i < nInputs; ++i) {
            auto &input = impl->inputs[i];

            if(!input.null) {
                auto ci = input.GetChannels();
                for(std::size_t j = 0; j < channelMap[i].size(); ++j) {
                    auto si = ci[j];
                    auto so = co[channelMap[i][j]];
                    for(std::size_t s = 0; s < nSamplesNow; ++s) {
                        so[s] += si[impl->nSamplesRead[i] + s];
                    }
                }
            }

            impl->nSamplesRead[i] += nSamplesNow;
            if(impl->nSamplesRead[i] == input.nSamples) {
                impl->inputValid[i] = false;
            }

            if(auto *mIn = input.meta.GetEntry<MEventTag>()) {
                mOut.events.insert(mOut.events.end(), mIn->events.begin(), mIn->events.end());
            }
        }
    }



}

