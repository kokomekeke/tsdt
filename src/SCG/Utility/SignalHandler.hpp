#pragma once
#include <functional>
#include <memory>
#include <vector>

namespace Sagax::SCG {

    // calls the callback with the signal number for every signal it was given.
    //
    // the callback runs on an ordinary thread, never inside a signal handler, so it may
    // lock a mutex, notify a condition variable or log like any other code.
    //
    // this lives as a local in main, and where it sits among the other locals matters in
    // both directions.
    //
    // it goes after whatever the callback reaches into is ready, and before anything
    // starts a thread. the constructor blocks the signals, and a thread only inherits
    // that mask from the thread that created it, so a thread started earlier can still be
    // handed one and take the default action for it.
    //
    // the destructor stops the callback and runs before the destructors of everything
    // declared above it, which is what keeps a signal arriving during shutdown from
    // reaching a mutex or a log stream that is already gone.
    //
    // only one of these may exist at a time.
    struct SignalHandler {
        using Callback = std::function<void(int)>;

        SignalHandler(const std::vector<int> &signals, Callback callback);
        ~SignalHandler();

        SignalHandler(const SignalHandler &) = delete;
        SignalHandler &operator=(const SignalHandler &) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

}
