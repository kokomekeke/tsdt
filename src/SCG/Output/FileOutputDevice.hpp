#pragma once
#include <vector>
#include <memory>
#include <complex>
#include <string>
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Scenario/Sample.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    struct FileOutputDevice : public OutputDevice {
        SSG::IO::IQFileOut<SCGSampleBase> file;
        SSG::Packets::SamplePacket<SCGSample> packet;
        std::string fileName;
        std::size_t nChannels;
        double sampleRate;
        double center;

        void Start() override;
        void Stop() override;
        void Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) override;
        void Flush() override;
    };

}

