#pragma once
#include <complex>
#include <cstddef>

namespace Sagax::SCG {
    using SCGSampleBase = float;
    using SCGSample = std::complex<float>;

    // samples of the fastest output in one pass of the pipeline
    inline constexpr std::size_t scgBlockSize = (1 << 12);
}
