#include <algorithm>
#include <thread>
#include "WaitUntil.hpp"

namespace Sagax::SCG {

    namespace {

        // at least the timer period of the platform. windows ticks every 15.625 ms unless
        // a process asks for a finer one
#ifdef _WIN32
        constexpr std::chrono::milliseconds timerPeriod(16);
#else
        constexpr std::chrono::milliseconds timerPeriod(2);
#endif

    }

    void WaitUntil(
        std::condition_variable &cv,
        std::unique_lock<std::mutex> &lck,
        std::chrono::system_clock::time_point time,
        std::chrono::system_clock::duration precision
    ) {
        std::chrono::system_clock::duration spinTime = std::max(
            std::chrono::system_clock::duration(timerPeriod) - precision,
            std::chrono::system_clock::duration::zero()
        );
        if(std::chrono::system_clock::now() < time - spinTime) {
            if(cv.wait_until(lck, time - spinTime) == std::cv_status::no_timeout) {
                return;
            }
        }
        lck.unlock();
        while(std::chrono::system_clock::now() < time) {
            std::this_thread::yield();
        }
        lck.lock();
    }

}
