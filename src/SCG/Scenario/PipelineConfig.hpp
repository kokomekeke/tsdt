#pragma once
#include <map>
#include <string>
#include <memory>
#include <functional>
#include <optional>
#include <chrono>

#ifdef HAVE_USRP
#include "SSG/Radio/Usrp/UsrpB200.hpp"
#endif

#ifdef HAVE_SIDEKIQ
#include "SSG/Radio/Sidekiq/Sidekiq.hpp"
#endif

namespace Sagax::SCG {

    struct PipelineConfig {
        struct PreinitConfig {
            #ifdef HAVE_USRP
            struct Usrp {
                bool usePreinitOnly = false;
                std::map<std::string, std::unique_ptr<SSG::UsrpB200::Device>> devices; // serial -> device
            };
            Usrp usrp;
            #endif

            #ifdef HAVE_SIDEKIQ
            struct Sidekiq {
                bool usePreinitOnly = false;
                std::map<std::string, std::unique_ptr<SSG::Sidekiq::Device>> devices; // serial -> device
            };
            Sidekiq sidekiq;
            #endif
        };

        // the event callbacks are called by the sink stage of an output when it hands the
        // samples to the device, ahead of the time they are transmitted at. they get the
        // event id and that time
        struct EventLogConfig {
            std::function<void(std::int32_t, std::chrono::system_clock::time_point)> eventStart;
            std::function<void(std::int32_t, std::chrono::system_clock::time_point)> eventEnd;
            std::optional<std::chrono::system_clock::time_point> startTime;
        };

        PreinitConfig preinit;
        EventLogConfig events;

        // every radio output is built as a MockOutputDevice instead of the real device,
        // the preinitialized devices are not looked up
        bool mockRadios = false;
    };

}


