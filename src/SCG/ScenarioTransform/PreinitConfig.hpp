#pragma once
#include <map>
#include <string>
#include <memory>

#ifdef HAVE_USRP
#include "SSG/Usrp/UsrpB200.hpp"
#endif

namespace Sagax::SCG {

    struct PreinitConfig {

        #ifdef HAVE_USRP
        struct Usrp {
            bool usePreinitOnly;
            std::map<std::string, std::unique_ptr<SSG::UsrpB200::Device>> devices; // serial -> device
        };
        Usrp usrp;
        #endif

    };
}


