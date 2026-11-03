#pragma once
#include <functional>
#include <memory>
#include <vector>

namespace Sagax::SCG {

    // calls the callback with the signal number for every signal it was given
    // the callback runs on an ordinary thread, never inside a signal handler
    // the constructor blocks the signals. a thread inherits the mask of the thread that created
    // it, a thread started earlier can still take the default action for a signal
    // it is created after whatever the callback uses and before any thread starts
    // the destructor stops the callback
    // only one may exist at a time
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
