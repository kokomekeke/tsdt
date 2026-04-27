#pragma once
#include <vector>
#include <memory>
#include <complex>
#include <string>
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Scenario/OutputVariant.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Modules/Packets.hpp"

namespace Sagax::SCG {

    struct OutputDevice {
        virtual ~OutputDevice() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void Take(SCGIQPacket &packet) = 0;
    };

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

#ifdef HAVE_USRP
#include "SSG/Usrp/UsrpB200.hpp"
namespace Sagax::SCG {
    struct UsrpB200Device : public OutputDevice, public Resource {
        std::string serial;
        std::string subdevSpec;
        double sampleRate;
        double center;
        double gain;

        std::unique_ptr<SSG::UsrpB200::Device> dev;

        void Init_() override;
        void Dispose_() override;
        void Start() override;
        void Stop() override;
        void Take(SCGIQPacket &packet) override;
    };
}
#endif


