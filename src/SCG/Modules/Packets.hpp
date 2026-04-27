#pragma once
#include <complex>
#include "SSG/Packets/Packets.hpp"

namespace Sagax::SCG {
    using SCGSampleBase = float;
    using SCGSample = std::complex<float>;
    using SCGRealPacket = Sagax::SSG::Packets::SamplePacket<float>;
    using SCGIQPacket = Sagax::SSG::Packets::SamplePacket<std::complex<float>>;

    inline constexpr std::size_t scgPacketSize = (1 << 12);
}

