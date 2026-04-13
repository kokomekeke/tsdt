#pragma once
#include <vector>
#include <memory>
#include <complex>
#include <string>
#include "SSG/Packets/Packets.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Scenario/OutputVariant.hpp"

namespace Sagax::SCG {

    struct OutputDevice {
        virtual ~OutputDevice() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void Take(SSG::Packets::IQPacket &packet) = 0;
    };

    struct FileOutputDevice : public OutputDevice {
        SSG::IO::IQFileOut file;
        std::string fileName;
        std::size_t nChannels;
        double sampleRate;
        double center;

        void Start() override;
        void Stop() override;
        void Take(SSG::Packets::IQPacket &packet) override;
    };
}

#ifdef HAVE_USRP
#include "SSG/Usrp/UsrpB200.hpp"
namespace Sagax::SCG {
    struct UsrpB200Device : public OutputDevice {
        std::unique_ptr<SSG::UsrpB200::Device> dev;

        std::vector<std::complex<float>> conversionBuf;

        void Start() override;
        void Stop() override;
        void Take(SSG::Packets::IQPacket &packet) override;
    };
}
#endif


