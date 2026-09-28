#pragma once
#include <string>
#include <stop_token>

namespace Sagax::SCG {

    struct Job {
        virtual ~Job() = default;
        virtual std::string Describe() const = 0;
        virtual double Estimate_s() const = 0;
        virtual void Run(std::stop_token stop) = 0;
    };

}
