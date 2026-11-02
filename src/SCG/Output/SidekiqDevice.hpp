#pragma once
#ifdef HAVE_SIDEKIQ

#include <vector>
#include <memory>
#include <complex>
#include <string>
#include "SSG/Radio/Sidekiq/Sidekiq.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Signal/Sample.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    struct SidekiqDevice : public OutputDevice, public Resource {
        std::string serial;

        double sampleRate;
        std::size_t nChannels;
        std::vector<std::string> handlers;
        std::vector<double> centers;
        std::vector<double> gains;

        std::unique_ptr<SSG::Sidekiq::Device> devOwned; // null if the device is not owned by this
        SSG::Sidekiq::Device *dev = nullptr; // the pointer that is actually used, it may point to the same as devOwned or another device

        std::unique_ptr<SSG::Sidekiq::TxStream> stream;

        void Init_() override;
        void Dispose_() override;
        void Start() override;
        void Stop() override;
        void Take(const std::vector<std::span<const SCGSample>> &channels) override;
    };

}


#endif
