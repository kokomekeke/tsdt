#include <thread>
#include <chrono>
#include <format>
#include "Common/Logger.hpp"
#include "MockOutputDevice.hpp"

namespace Sagax::SCG {

    void MockOutputDevice::Start() {
        nSamplesTotal = 0;
        nSamplesLogged = 0;
        nPacketsLogged = 0;
        nPacketsNotNull = 0;

        std::string centerList;
        for(std::size_t i = 0; i < centers.size(); ++i) {
            centerList += std::format("{}{:.6f}", i == 0 ? "" : ", ", centers[i] / 1.0e6);
        }
        Common::Logger::Info(
            "mock radio {} started, {} channels, {:.6f} Msps, centers {} MHz",
            serial, nChannels, sampleRate / 1.0e6, centerList
        );
    }

    void MockOutputDevice::Stop() {
        Common::Logger::Info(
            "mock radio {} stopped, {} samples sent, {:.3f} s of stream",
            serial, nSamplesTotal, double(nSamplesTotal) / sampleRate
        );
    }

    void MockOutputDevice::Take(SCGIQPacket &packet) {
        if(nSamplesTotal == 0) {
            streamStart = std::chrono::steady_clock::now();
        }

        nSamplesTotal += packet.nSamples;
        nSamplesLogged += packet.nSamples;
        ++nPacketsLogged;
        if(!packet.null) {
            ++nPacketsNotNull;
        }

        // the deadline is measured from streamStart over the total sample count rather
        // than sleeping once per packet. a sleep is rounded up to the timer resolution,
        // which is far longer than one packet, but overshooting leaves the next deadline
        // already in the past, so the packets behind it go out without waiting and the
        // average rate stays correct
        auto deadline = streamStart +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(
                    double(nSamplesTotal) / packet.sampleRate
                )
            );
        std::this_thread::sleep_until(deadline);

        if(double(nSamplesLogged) >= sampleRate) {
            Common::Logger::Info(
                "mock radio {} at {:.3f} s, {} samples sent, {} of the last {} packets not null",
                serial,
                double(nSamplesTotal) / sampleRate,
                nSamplesTotal,
                nPacketsNotNull,
                nPacketsLogged
            );
            nSamplesLogged = 0;
            nPacketsLogged = 0;
            nPacketsNotNull = 0;
        }
    }

}
