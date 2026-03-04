#pragma once
#include <algorithm>
#include <format>
#include <cmath>
#include "Common/Assert.hpp"
#include "Pipeline/Pipeline.hpp"
#include "SSG/Packets/Packets.hpp"

namespace Sagax::SCG::Modules {

    struct EventDelay : public Sagax::Pipeline::Module {

        Sagax::Pipeline::InPort<SSG::Packets::IQPacket> inP = AddInPort(&inP);
        Sagax::Pipeline::OutPort<SSG::Packets::IQPacket> outP = AddOutPort(&outP);

        double tStart; // signal start time
        double tEnd; // signal end time
        double tStop;

        // used for the empty packets only
        std::size_t nSamples;
        double sampleRate;

        double t;

        void Reset() override {
            t = 0.0;
        }
        void Start() override {
        }

        void Stop() override {
        }

        void Process() override {
            if(t < tStart) {
                if(outP.nSpaces() == 0) {
                    return;
                }
                SSG::Packets::IQPacket &output = outP.Push();
                std::size_t nSamplesNow = std::max(std::size_t(1), std::min(nSamples, std::size_t(std::round((tStart - t) * sampleRate))));
                output.Reshape(sampleRate, 1, nSamplesNow, true);
                t += double(nSamplesNow) / sampleRate;
                return;
            }
            else if(t < tEnd) {
                if(inP.nItems() == 0 || outP.nSpaces() == 0) {
                    return;
                }
                SSG::Packets::IQPacket &output = outP.Push();
                SSG::Packets::IQPacket &input = inP.Pop();
                Common::Assert(input.nChannels == 1, "input must have 1 channel");
                if(input.nSamples / input.sampleRate > tEnd - t) {
                    std::size_t nSamplesKept = std::max(std::size_t(1), std::size_t(std::round((tEnd - t) * input.sampleRate)));
                    input.nSamples = nSamplesKept;
                    input.data.resize(nSamplesKept);
                    t = tEnd;
                }
                else {
                    t += double(input.nSamples) / input.sampleRate;
                }
                std::swap(input, output);
                return;
            }
            else if(t < tStop) {
                if(outP.nSpaces() == 0) {
                    return;
                }
                SSG::Packets::IQPacket &output = outP.Push();
                std::size_t nSamplesNow = std::max(std::size_t(1), std::min(nSamples, std::size_t(std::round((tStop - t) * sampleRate))));
                output.Reshape(sampleRate, 1, nSamplesNow, true);
                t += nSamplesNow / sampleRate;
                return;
            }
        }
    };
}

