#pragma once
#ifdef HAVE_USRP

#include <vector>
#include <memory>
#include <complex>
#include <string>
#include "SSG/Radio/Usrp/UsrpB200.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Sample.hpp"
#include "OutputDevice.hpp"

namespace Sagax::SCG {

    struct UsrpB200Device : public OutputDevice, public Resource {
        std::string serial;
        std::string subdevSpec;
        double sampleRate;
        double center;
        double gain;

        // a device opened before the pipeline and owned elsewhere, it is kept across Dispose_.
        // without it Init_ opens devOwned. dev is the one of the two in use from Init_ to Dispose_
        SSG::UsrpB200::Device *devPreinit = nullptr;
        std::unique_ptr<SSG::UsrpB200::Device> devOwned;
        SSG::UsrpB200::Device *dev = nullptr;

        std::unique_ptr<SSG::UsrpB200::TxStream> stream;

        // the system time the device time 0 was set at
        std::chrono::system_clock::time_point deviceTimeZero;

        void Init_() override;
        void Dispose_() override;
        void Start() override;
        void Stop() override;
        void Take(const std::vector<std::span<const SCGSample>> &channels, std::optional<std::chrono::system_clock::time_point> time) override;
        void Flush() override;
    };

}


#endif
