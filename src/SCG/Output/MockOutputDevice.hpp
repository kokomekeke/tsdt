#pragma once
#include <vector>
#include <string>
#include <chrono>
#include <cstddef>
#include "SCG/Scenario/Sample.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    // 16 send frames of 8176 bytes, each with a 16 byte header, 4 bytes to a sample. the
    // transmit fifo of the fpga is not documented and is not counted
    inline constexpr std::size_t mockBufferSizeUsrpB200 = 16 * (8176 - 16) / 4;

    // the block the stream fills on the host, plus the share of a channel in the 16384 word
    // fpga fifo. the fifo keeps a 2048 word margin and is split between the two channels,
    // a block is the same size
    inline constexpr std::size_t mockBufferSizeSidekiq = 2 * ((16384 - 2048) / 2);

    // stands in for a radio when the server is configured without hardware, like a timed
    // stream. every sample has a time, the time of the first block and the number of
    // samples since. the buffer is a sample count, never the samples. it takes blocks until
    // it is full and holds them until their time, so before the start time it fills up and
    // after it the pipeline runs up to one buffer ahead. the samples of a block whose time
    // has passed when the block comes are dropped and counted, the stream does not move
    struct MockOutputDevice : public OutputDevice {
        std::string serial;
        double sampleRate;
        std::size_t nChannels;
        std::vector<double> centers;
        std::size_t bufferSize = 0; // 0 paces each block on its own

        std::chrono::steady_clock::time_point streamStart;
        std::size_t nSamplesTotal = 0; // taken, the dropped ones included
        std::size_t nSamplesDropped = 0;
        std::size_t nSamplesLogged = 0;
        std::size_t nBlocksLogged = 0;
        std::size_t nSamplesDroppedLogged = 0;
        bool inUnderflow = false;

        void Start() override;
        void Stop() override;
        void Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) override;
        void Flush() override;
    };

}
