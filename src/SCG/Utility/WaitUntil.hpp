#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Sagax::SCG {

    // waits on cv until time, lck holds the mutex of cv
    // a wait on a condition variable can end up to a timer period late, the last stretch is spun
    // with lck unlocked
    // a notify or a spurious wakeup ends the wait early, but not during the spin
    void WaitUntil(
        std::condition_variable &cv,
        std::unique_lock<std::mutex> &lck,
        std::chrono::system_clock::time_point time
    );

}
