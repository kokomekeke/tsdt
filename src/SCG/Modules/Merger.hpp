#pragma once
#include <vector>
#include <memory>
#include "Pipeline/Pipeline.hpp"
#include "SSG/Math/Math.hpp"
#include "Packets.hpp"

namespace Sagax::SCG {

    struct Merger : Pipeline::Module {
        struct Impl;
        std::unique_ptr<Impl> impl;

        std::vector<Pipeline::InPort<SCGIQPacket>> inP;
        Pipeline::OutPort<SCGIQPacket> outP = AddOutPort(&outP);

        double sampleRate;
        std::size_t nChannelsOut;
        std::vector<std::vector<std::size_t>> channelMap;


        Merger(std::size_t nChannelsOut_, std::vector<std::vector<std::size_t>> channelMap_);
        ~Merger();

        void Reset() override;
        void Start() override;

        void Stop() override;
        void Process() override;
    };

}


