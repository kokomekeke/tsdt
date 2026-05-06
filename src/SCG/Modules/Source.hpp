#pragma once
#include <memory>
#include <vector>
#include "Pipeline/Pipeline.hpp"
#include "SSG/IO/IQFileIn.hpp"
#include "SSG/IO/PlaybackAdapter.hpp"
#include "SCG/Signal/SignalGenerator.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "Packets.hpp"

namespace Sagax::SCG {

    struct SampleIndexedEventList {
        struct EventCtx {
            std::size_t iStart;
            std::size_t iEnd;
            Scenario::Event event;
        };
        std::vector<EventCtx> events;
        std::size_t nSamples;
    };

    struct StreamSource : public Sagax::Pipeline::Module {
        Sagax::Pipeline::OutPort<SCGIQPacket> outP = AddOutPort(&outP);

        std::size_t nSamples;
        SampleIndexedEventList events;
        std::vector<std::unique_ptr<SignalGenerator>> generators;
        double sampleRate;

        std::vector<bool> genStarted;
        std::size_t iEvent = 0;
        std::size_t iSample = 0;

        void Reset() override;
        void Start() override;
        void Stop() override;
        void Process() override;
    };

    struct PlaybackSource : public Sagax::Pipeline::Module {
        Sagax::Pipeline::OutPort<SCGIQPacket> outP = AddOutPort(&outP);

        std::string fileName;
        std::size_t nSamples;
        SampleIndexedEventList events;

        SSG::IO::IQFileIn<SCGSampleBase> file;
        SSG::IO::PlaybackAdapter<SCGSample> fileAdapter;

        void Reset() override;
        void Start() override;
        void Stop() override;
        void Process() override;
    };
}

