#pragma once
#include <format>
#include <string>
#include "Common/Logger.hpp"
#include "SCG/Scenario/Validate.hpp"

namespace Sagax::SCG {

    // an empty string means the step succeeded
    using CommandError = std::string;

    // the warnings and the errors are logged one by one, the first error is handed back
    inline CommandError CheckValidity(const Common::Logger::Handle &lh, const ValidationResult &validity, const char *what) {
        for(auto &s : validity.warnings) {
            Common::Logger::Warn(lh, "{}", s.c_str());
        }
        if(validity.errors.empty()) {
            return {};
        }
        for(auto &s : validity.errors) {
            Common::Logger::Error(lh, "{}", s.c_str());
        }
        return std::format("{} validation failed: {}", what, validity.errors.front());
    }

}

#define SCG_CHECK(expr)                 \
    do {                                \
        CommandError err_ = (expr);     \
        if(!err_.empty()) {             \
            return err_;                \
        }                               \
    } while(false)
