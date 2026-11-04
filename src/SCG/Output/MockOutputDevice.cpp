#include <algorithm>
#include <thread>
#include <chrono>
#include <format>
#include "Common/Logger.hpp"
#include "MockOutputDevice.hpp"

namespace Sagax::SCG {

    void MockOutputDevice::Start() {
        nSamplesTotal = 0;
        nSamplesDropped = 0;
        nSamplesLogged = 0;
        nBlocksLogged = 0;
        nSamplesDroppedLogged = 0;
        inUnderflow = false;

        std::string centerList;
        for(std::size_t i = 0; i < centers.size(); ++i) {
            centerList += std::format("{}{:.6f}", i == 0 ? "" : ", ", centers[i] / 1.0e6);
        }
        Common::Logger::Info(
            "mock radio {} started, {} channels, {:.6f} Msps, centers {} MHz, buffer {} samples ({:.3f} ms)",
            serial, nChannels, sampleRate / 1.0e6, centerList,
            bufferSize, double(bufferSize) / sampleRate * 1.0e3
        );
    }

    void MockOutputDevice::Stop() {
        Common::Logger::Info(
            "mock radio {} stopped, {} samples sent, {:.3f} s of stream",
            serial, nSamplesTotal - nSamplesDropped, double(nSamplesTotal) / sampleRate
        );
        if(nSamplesDropped > 0) {
            Common::Logger::Warn(
                "mock radio {} dropped {} late samples, {:.3f} ms of the stream",
                serial, nSamplesDropped, double(nSamplesDropped) / sampleRate * 1.0e3
            );
        }
    }

    void MockOutputDevice::Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) {
        std::size_t nSamples = channels[0].size();
        if(nSamplesTotal == 0) {
            streamStart = std::chrono::steady_clock::now();
            if(time.has_value()) {
                streamStart += std::chrono::duration_cast<std::chrono::steady_clock::duration>(time.value() - std::chrono::system_clock::now());
            }
        }

        // the position of the sample sent now, negative before the start time. taken over
        // the whole stream, so the rounding of a sleep does not add up
        double position = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - streamStart
        ).count() * sampleRate;

        std::size_t nLate = std::size_t(std::clamp(position - double(nSamplesTotal), 0.0, double(nSamples)));
        if(nLate > 0) {
            nSamplesDropped += nLate;
            nSamplesDroppedLogged += nLate;
            if(!inUnderflow) {
                inUnderflow = true;
                Common::Logger::Warn(
                    "mock radio {} buffer underflow at {:.3f} s, {:.3f} ms of samples dropped",
                    serial,
                    double(nSamplesTotal) / sampleRate,
                    double(nLate) / sampleRate * 1.0e3
                );
            }
        }
        else {
            inUnderflow = false;
        }

        // the buffer drains from the start time on, the block is taken once it fits
        double level = std::max(double(nSamplesTotal + nSamples) - std::max(position, 0.0), 0.0);
        if(level > double(bufferSize)) {
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>((level - double(bufferSize)) / sampleRate)
                )
            );
            level = double(bufferSize);
        }

        nSamplesTotal += nSamples;
        nSamplesLogged += nSamples;
        ++nBlocksLogged;

        if(double(nSamplesLogged) >= sampleRate) {
            Common::Logger::Info(
                "mock radio {} at {:.3f} s, {} samples sent, {} blocks in the last second, buffer {:.3f} ms",
                serial,
                double(nSamplesTotal) / sampleRate,
                nSamplesTotal - nSamplesDropped,
                nBlocksLogged,
                level / sampleRate * 1.0e3
            );
            if(nSamplesDroppedLogged > 0) {
                Common::Logger::Warn(
                    "mock radio {} dropped {} late samples in the last {:.3f} s, {:.3f} ms of the stream",
                    serial,
                    nSamplesDroppedLogged,
                    double(nSamplesLogged) / sampleRate,
                    double(nSamplesDroppedLogged) / sampleRate * 1.0e3
                );
            }
            nSamplesLogged = 0;
            nBlocksLogged = 0;
            nSamplesDroppedLogged = 0;
        }
    }

    void MockOutputDevice::Flush() {
        double position = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - streamStart
        ).count() * sampleRate;
        double level = double(nSamplesTotal) - position;
        if(level > 0.0) {
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(level / sampleRate)
                )
            );
        }
    }

}
