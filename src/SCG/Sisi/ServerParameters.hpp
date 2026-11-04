#pragma once
#include <chrono>
#include <cstdint>

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
    inline constexpr double sidekiqUpperTemperature_celsius = 90.0;
    inline constexpr double sidekiqLowerTemperature_celsius = 75.0;
    inline constexpr double usrpUpperTemperature_celsius = 90.0;
    inline constexpr double usrpLowerTemperature_celsius = 75.0;

    // mqtt quality of service of the published messages
    inline constexpr std::uint8_t sessionLogQos = 1;
    inline constexpr std::uint8_t testStatusQos = 2;
    inline constexpr std::uint8_t signalStatusQos = 2;
    inline constexpr std::uint8_t commandQos = 2;

}
