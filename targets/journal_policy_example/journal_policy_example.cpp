#include <journal_policy_example/journal_policy_example.hpp>
#include <journal/journal.hpp>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <pthread.h>
#include <stop_token>
#include <thread>
#include <utility>

namespace tle::journal_policy_example {

    namespace {
        constexpr std::int64_t k_poll_interval_ns = 50'000'000; // 50 milliseconds

        // Demonstration loop illustrating journal's decoupled write policy.
        // Waits for externally triggered policy changes via SIGUSR1 and SIGUSR2 so the core journal
        // writer remains independent of any specific OS signal or IPC transport.
        void listener_loop(const std::stop_token& stop_token, journal::policy debug_policy) noexcept {
            sigset_t listen_set{};
            sigemptyset(&listen_set);
            sigaddset(&listen_set, SIGUSR1);
            sigaddset(&listen_set, SIGUSR2);

            struct timespec timeout{
                .tv_sec = 0,
                .tv_nsec = k_poll_interval_ns
            };

            while (!stop_token.stop_requested()) {
                siginfo_t info{};
                int const sig = ::sigtimedwait(&listen_set, &info, &timeout);
                if (sig == SIGUSR1) {
                    (void)journal::activate_policy(debug_policy);
                    journal::log(
                        journal::severity::notice,
                        journal::message{"Signal adapter activated verbose debug policy"}
                    );
                } else if (sig == SIGUSR2) {
                    journal::reset_default_policy();
                    journal::log(
                        journal::severity::notice,
                        journal::message{"Signal adapter restored default flight recorder policy"}
                    );
                }
            }
        }
    } // namespace

    struct signal_adapter::impl {
        std::jthread worker;
    };

    auto signal_adapter::default_debug_policy() noexcept -> journal::policy {
        journal::policy policy{};
        policy.stride_by_level.fill(1U);
        return policy;
    }

    auto signal_adapter::block_signals(sigset_t& saved_mask) noexcept -> bool {
        sigset_t set{};
        sigemptyset(&set);
        sigaddset(&set, SIGUSR1);
        sigaddset(&set, SIGUSR2);
        return ::pthread_sigmask(SIG_BLOCK, &set, &saved_mask) == 0;
    }

    auto signal_adapter::restore_signals(const sigset_t& saved_mask) noexcept -> bool {
        drain_pending_signals();
        return ::pthread_sigmask(SIG_SETMASK, &saved_mask, nullptr) == 0;
    }

    void signal_adapter::drain_pending_signals() noexcept {
        sigset_t set{};
        sigemptyset(&set);
        sigaddset(&set, SIGUSR1);
        sigaddset(&set, SIGUSR2);
        struct timespec timeout{.tv_sec = 0, .tv_nsec = 0};
        siginfo_t info{};
        while (::sigtimedwait(&set, &info, &timeout) > 0) {
        }
    }

    auto signal_adapter::create(
        journal::policy debug_policy
    ) noexcept -> std::optional<signal_adapter> {
        try {
            if (!journal::is_valid_policy(debug_policy)) {
                return std::nullopt;
            }
            return signal_adapter(debug_policy);
        } catch (...) {
            (void)journal::catch_boundary_exception(k_catalog);
            return std::nullopt;
        }
    }

    signal_adapter::signal_adapter(journal::policy debug_policy)
        : _debug_policy(debug_policy),
          _impl(std::make_unique<impl>()) {
        if (block_signals(_saved_mask)) {
            _signals_blocked = true;
            try {
                _impl->worker = std::jthread([debug_policy](const std::stop_token& token) noexcept {
                    listener_loop(token, debug_policy);
                });
            } catch (...) {
                (void)restore_signals(_saved_mask);
                _signals_blocked = false;
                throw;
            }
        }
    }

    signal_adapter::~signal_adapter() noexcept {
        stop();
    }

    signal_adapter::signal_adapter(signal_adapter&& other) noexcept
        : _debug_policy(other._debug_policy),
          _impl(std::move(other._impl)),
          _saved_mask(other._saved_mask),
          _signals_blocked(other._signals_blocked) {
        other._signals_blocked = false;
    }

    auto signal_adapter::operator=(signal_adapter&& other) noexcept -> signal_adapter& {
        if (this != &other) {
            stop();
            _debug_policy = other._debug_policy;
            _impl = std::move(other._impl);
            _saved_mask = other._saved_mask;
            _signals_blocked = other._signals_blocked;
            other._signals_blocked = false;
        }
        return *this;
    }

    auto signal_adapter::is_running() const noexcept -> bool {
        return _impl != nullptr && _impl->worker.joinable();
    }

    void signal_adapter::stop() noexcept {
        if (_impl != nullptr && _impl->worker.joinable()) {
            _impl->worker.request_stop();
            _impl->worker.join();
        }
        if (_signals_blocked) {
            (void)restore_signals(_saved_mask);
            _signals_blocked = false;
        }
    }

} // namespace tle::journal_policy_example
