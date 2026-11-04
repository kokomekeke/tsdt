#pragma once
#ifdef HAVE_SIDEKIQ

#include <vector>
#include <memory>
#include <complex>
#include <string>
#include "SSG/Radio/Sidekiq/Sidekiq.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Sample.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    struct SidekiqDevice : public OutputDevice, public Resource {
        std::string serial;

        double sampleRate;
        std::size_t nChannels;
        std::vector<std::string> handlers;
        std::vector<double> centers;
        std::vector<double> gains;

        // a device opened before the pipeline and owned elsewhere, it is kept across Dispose_.
        // without it Init_ opens devOwned. dev is the one of the two in use from Init_ to Dispose_
        SSG::Sidekiq::Device *devPreinit = nullptr;
        std::unique_ptr<SSG::Sidekiq::Device> devOwned;
        SSG::Sidekiq::Device *dev = nullptr;

        std::unique_ptr<SSG::Sidekiq::TxStream> stream;

        // the rf timestamp of the card and the system time it was read at
        std::uint64_t syncTimestamp;
        std::chrono::system_clock::time_point syncTime;

        void Init_() override;
        void Dispose_() override;
        void Start() override;
        void Stop() override;
        void Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) override;
        void Flush() override;
    };

}


#endif
