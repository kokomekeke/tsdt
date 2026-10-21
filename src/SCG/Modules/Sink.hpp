#pragma once
#include <memory>
#include <cstdint>
#include <vector>
#include <functional>
#include <optional>
#include <chrono>
#include "Pipeline/Pipeline.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Output/OutputDevice.hpp"
#include "Packets.hpp"

namespace Sagax::SCG {

    struct StreamSink : public Sagax::Pipeline::Module {
        Pipeline::InPort<SCGIQPacket> inP = AddInPort(&inP);
        std::unique_ptr<OutputDevice> dev;
        std::vector<std::int32_t> lastEvents;
        bool firstPacket = true;

        std::function<void(std::int32_t)> eventStart;
        std::function<void(std::int32_t)> eventEnd;
        // called once, right before the first packet goes to the device
        std::function<void(std::chrono::system_clock::time_point)> transmissionStart;
        std::optional<std::chrono::system_clock::time_point> startTime;

        void Reset() override;
        void Start() override;
        void Stop() override;
        void Process() override;
    };

    struct RecordSink : public Sagax::Pipeline::Module {
        Pipeline::InPort<SCGIQPacket> inP = AddInPort(&inP);

        std::string fileName;
        double center;
        double sampleRate;
        std::size_t nChannels;
        SSG::IO::IQFileOut<SCGSampleBase> file;

        void Reset() override;
        void Start() override;
        void Stop() override;
        void Process() override;
    };

}
