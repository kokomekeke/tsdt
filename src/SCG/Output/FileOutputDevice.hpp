#pragma once
#include <vector>
#include <memory>
#include <complex>
#include <string>
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Modules/Packets.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    struct FileOutputDevice : public OutputDevice {
        SSG::IO::IQFileOut<SCGSampleBase> file;
        std::string fileName;
        std::size_t nChannels;
        double sampleRate;
        double center;

        void Start() override;
        void Stop() override;
        void Take(SCGIQPacket &packet) override;
    };

}

