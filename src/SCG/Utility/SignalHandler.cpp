#include <utility>
#include "Common/Assert.hpp"
#include "SignalHandler.hpp"

#ifdef _WIN32
#include <csignal>
#else
#include <atomic>
#include <cerrno>
#include <ctime>
#include <thread>
#include <pthread.h>
#include <signal.h>
#endif

namespace Sagax::SCG {

#ifdef _WIN32

    // the c runtime dispatches a console signal on a thread it starts for that purpose,
    // so the callback already runs outside any handler context and is called directly
    struct SignalHandler::Impl {
        std::vector<int> signals;
        Callback callback;

        // std::signal takes a plain function pointer with no context, so the object the
        // trampoline belongs to is reached through this
        static Impl *instance;

        static void Trampoline(int sig) {
            // the disposition is reset before the handler is entered, so it is put back
            std::signal(sig, &Impl::Trampoline);
            Impl *self = instance;
            if(self != nullptr) {
                self->callback(sig);
            }
        }

        Impl(const std::vector<int> &signals_, Callback callback_)
            : signals(signals_), callback(std::move(callback_)) {
            Common::Assert(instance == nullptr, "only one SignalHandler may exist at a time");
            instance = this;
            for(int sig : signals) {
                std::signal(sig, &Impl::Trampoline);
            }
        }

        ~Impl() {
            for(int sig : signals) {
                std::signal(sig, SIG_DFL);
            }
            instance = nullptr;
        }
    };

    SignalHandler::Impl *SignalHandler::Impl::instance = nullptr;

#else

    // no handler is installed at all. the signals are blocked in every thread, which is
    // why this has to exist before any thread is started, a thread inherits the mask of
    // the one that created it. the thread below then takes them out of the pending set
    struct SignalHandler::Impl {
        std::vector<int> signals;
        Callback callback;
        sigset_t mask{};
        std::atomic<bool> running{false};
        std::thread thread;

        Impl(const std::vector<int> &signals_, Callback callback_)
            : signals(signals_), callback(std::move(callback_)) {
            sigemptyset(&mask);
            for(int sig : signals) {
                sigaddset(&mask, sig);
            }
            Common::Assert(
                pthread_sigmask(SIG_BLOCK, &mask, nullptr) == 0,
                "failed to block the signals"
            );

            running.store(true);
            thread = std::thread([this](){
                // sigtimedwait rather than sigwait, so the loop comes back often enough
                // to see the flag. there is no signal to wake it with otherwise
                const timespec timeout{ 0, 100 * 1000 * 1000 };
                while(running.load()) {
                    siginfo_t info{};
                    int sig = sigtimedwait(&mask, &info, &timeout);
                    if(sig < 0) {
                        // EAGAIN when the wait timed out, EINTR when something else
                        // interrupted it, neither is a signal for the callback
                        continue;
                    }
                    callback(sig);
                }
            });
        }

        ~Impl() {
            running.store(false);
            if(thread.joinable()) {
                thread.join();
            }
            pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
        }
    };

#endif


    SignalHandler::SignalHandler(const std::vector<int> &signals, Callback callback) {
        Common::Assert(!signals.empty(), "no signals to handle");
        Common::Assert(bool(callback), "no callback to call");
        impl = std::make_unique<Impl>(signals, std::move(callback));
    }

    SignalHandler::~SignalHandler() = default;

}
