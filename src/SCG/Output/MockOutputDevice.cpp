#include <algorithm>
#include <thread>
#include <chrono>
#include <format>
#include "Common/Logger.hpp"
#include "MockOutputDevice.hpp"

namespace Sagax::SCG {

    void MockOutputDevice::Start() {
        nSamplesTotal = 0;
        nSamplesGap = 0;
        nSamplesLogged = 0;
        nBlocksLogged = 0;
        nSamplesGapLogged = 0;
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
            serial, nSamplesTotal, double(nSamplesTotal) / sampleRate
        );
        if(nSamplesGap > 0) {
            Common::Logger::Warn(
                "mock radio {} ran dry for {} samples, {:.3f} ms of the stream",
                serial, nSamplesGap, double(nSamplesGap) / sampleRate * 1.0e3
            );
        }
    }

    void MockOutputDevice::Take(const std::vector<std::span<const SCGSample>> &channels) {
        std::size_t nSamples = channels[0].size();
        if(nSamplesTotal == 0) {
            streamStart = std::chrono::steady_clock::now();
        }

        // taken over the whole stream, so the rounding of a sleep does not add up. the
        // gap is counted in, so an underflow does not move the stream later
        double elapsed_s = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - streamStart
        ).count();
        double level = double(nSamplesTotal + nSamplesGap) - elapsed_s * sampleRate;

        if(level < 0.0) {
            std::size_t nMissing = std::size_t(-level);
            level = 0.0;
            nSamplesGap += nMissing;
            nSamplesGapLogged += nMissing;
            if(!inUnderflow) {
                inUnderflow = true;
                Common::Logger::Warn(
                    "mock radio {} buffer underflow at {:.3f} s, {:.3f} ms of samples missing",
                    serial,
                    double(nSamplesTotal) / sampleRate,
                    double(nMissing) / sampleRate * 1.0e3
                );
            }
        }
        else {
            inUnderflow = false;
        }

        double room = double(bufferSize) - level;
        if(double(nSamples) > room) {
            std::this_thread::sleep_for(
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>((double(nSamples) - room) / sampleRate)
                )
            );
            // the wait ends the moment the block fits exactly
            level = double(bufferSize) - double(nSamples);
        }
        level += double(nSamples);

        nSamplesTotal += nSamples;
        nSamplesLogged += nSamples;
        ++nBlocksLogged;

        if(double(nSamplesLogged) >= sampleRate) {
            Common::Logger::Info(
                "mock radio {} at {:.3f} s, {} samples sent, {} blocks in the last second, buffer {:.3f} ms",
                serial,
                double(nSamplesTotal) / sampleRate,
                nSamplesTotal,
                nBlocksLogged,
                std::max(level, 0.0) / sampleRate * 1.0e3
            );
            if(nSamplesGapLogged > 0) {
                Common::Logger::Warn(
                    "mock radio {} missed {} samples in the last {:.3f} s, {:.3f} ms of the stream",
                    serial,
                    nSamplesGapLogged,
                    double(nSamplesLogged) / sampleRate,
                    double(nSamplesGapLogged) / sampleRate * 1.0e3
                );
            }
            nSamplesLogged = 0;
            nBlocksLogged = 0;
                nSamplesGapLogged = 0;
        }
    }

}
