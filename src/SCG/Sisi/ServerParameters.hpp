#pragma once
#include <chrono>

namespace Sagax::SCG {

    // values of the server that are compiled in instead of read from the config

    // the main loop runs at least this often
    inline constexpr std::chrono::milliseconds scheduleTick{50};

    // the radio monitor reads the temperatures this often
    inline constexpr std::chrono::milliseconds radioMonitorTick{1000};

    // how late a wait may end, see WaitUntil
    inline constexpr std::chrono::milliseconds schedulePrecision{5};
    inline constexpr std::chrono::milliseconds radioMonitorPrecision{5};

    // a radio at or above its upper temperature makes the radios not ready, they are ready
    // again once every radio has been below its lower temperature since
    inline constexpr double sidekiqUpperTemperature_celsius = 100.0;
    inline constexpr double sidekiqLowerTemperature_celsius = 90.0;
    inline constexpr double usrpUpperTemperature_celsius = 100.0;
    inline constexpr double usrpLowerTemperature_celsius = 90.0;

}
