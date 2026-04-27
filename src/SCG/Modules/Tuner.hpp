#pragma once
#include <vector>
#include <memory>
#include "Pipeline/Pipeline.hpp"
#include "SSG/Math/Math.hpp"
#include "Packets.hpp"

namespace Sagax::SCG {

    struct Tuner : Pipeline::Module {
        struct Impl;
        std::unique_ptr<Impl> impl;

        Pipeline::InPort<SCGIQPacket> inP = AddInPort(&inP);
        Pipeline::OutPort<SCGIQPacket> outP = AddOutPort(&outP);

        // set before start
        double freqOut;
        double sampleRate;

        Tuner();
        ~Tuner();

        void Reset() override;
        void Start() override;
        void Stop() override;
        void Process() override;
    };

}