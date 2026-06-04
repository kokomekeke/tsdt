#pragma once
#include "SCG/Modules/Packets.hpp"

namespace Sagax::SCG {

    struct OutputDevice {
        virtual ~OutputDevice() = default;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void Take(SCGIQPacket &packet) = 0;
    };

}

