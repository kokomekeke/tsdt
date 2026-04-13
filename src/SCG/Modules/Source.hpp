#pragma once
#include <memory>
#include <vector>
#include "Pipeline/Pipeline.hpp"
#include "SSG/IO/IQFileIn.hpp"
#include "SSG/Packets/Packets.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {

    struct StreamSource : public Sagax::Pipeline::Module {
        Sagax::Pipeline::OutPort<SSG::Packets::IQPacket> outP = AddOutPort(&outP);

        std::size_t nSamples;
        std::vector<Scenario::Event> events;
        std::vector<std::unique_ptr<SignalGenerator>> generators;
        double duration;

        std::vector<bool> genStarted;
        std::size_t iEvent = 0;
        double t = 0.0;

        void Reset() override;
        void Start() override;
        void Stop() override;
        void Process() override;
    };

    struct PlaybackSource : public Sagax::Pipeline::Module {
        Sagax::Pipeline::OutPort<SSG::Packets::IQPacket> outP = AddOutPort(&outP);

        struct EventCtx {
            std::size_t iStart;
            std::size_t iEnd;
            Scenario::Event event;
        };

        std::size_t nSamples;
        std::vector<EventCtx> events;
        std::string fileName;

        SSG::IO::IQFileIn file;
        std::size_t iSample = 0;

        void Reset() override;
        void Start() override;
        void Stop() override;
        void Process() override;
    };
}

