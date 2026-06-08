#pragma once
#include <map>
#include <string>
#include <memory>
#include <functional>

#ifdef HAVE_USRP
#include "SSG/Usrp/UsrpB200.hpp"
#endif

namespace Sagax::SCG {

    struct PipelineConfig {
        struct PreinitConfig {
            #ifdef HAVE_USRP
            struct Usrp {
                bool usePreinitOnly;
                std::map<std::string, std::unique_ptr<SSG::UsrpB200::Device>> devices; // serial -> device
            };
            Usrp usrp;
            #endif
        };

        struct EventLogConfig {
            std::function<void(std::int32_t)> eventStart;
            std::function<void(std::int32_t)> eventEnd;
        };

        PreinitConfig preinit;
        EventLogConfig events;
    };

}


