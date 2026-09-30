#pragma once
#include "Messages.hpp"

namespace Sagax::SCG {

    struct SavedTest {
        CommandMessage::TestSetup command;
        TestStatusMessage::TestStatus status;
    };

}
