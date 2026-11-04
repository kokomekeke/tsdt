#pragma once
#include <string>
#include "Messages.hpp"

namespace Sagax::SCG {

    struct SavedTest {
        // the config values the gnss recordings of the test were made with
        struct Gnss {
            bool prerecord;
            bool mock;
            std::string ephemerisFile;
        };

        CommandMessage::TestSetup command;
        TestStatusMessage::TestStatus status;
        bool authorized;
        Gnss gnss;
    };

}
