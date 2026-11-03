#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace Sagax::SCG {

    // waits on cv until time, lck holds the mutex of cv. the wait ends at most precision late
    // a wait on a condition variable can end up to a timer period late. when that is longer
    // than precision, the cv wait ends early by the difference and the rest is spun with lck
    // unlocked
    // a notify or a spurious wakeup ends the wait early, but not during the spin
    void WaitUntil(
        std::condition_variable &cv,
        std::unique_lock<std::mutex> &lck,
        std::chrono::system_clock::time_point time,
        std::chrono::system_clock::duration precision
    );

}
