#pragma once

#include <csignal>
#include <optional>
#include <stop_token>
#include <thread>
#include <journal/journal.hpp>

namespace tle::journal_signal_adapter {

    struct catalog {
        journal::catalog_entry k_adapter_fault{
            .message_id = "ba2938d457754bafa974bf0e5523af8e",
            .component = "journal_signal_adapter",
            .summary = "Signal adapter failure caught at component boundary"
        };
    };
    inline constexpr catalog k_catalog{};

    // RAII POSIX signal adapter component that manages a dedicated listener thread.
    // The thread waits on SIGUSR1 and SIGUSR2 using sigtimedwait to avoid signal handler reentrancy.
    // SIGUSR1 activates a verbose debug policy, while SIGUSR2 resets default flight recorder policy.
    class signal_adapter {
    public:
        // Returns the verbose debug policy activated upon receiving SIGUSR1.
        [[nodiscard]] static auto default_debug_policy() noexcept -> journal::policy;

        // Static factory function returning std::optional for fallible initialization.
        [[nodiscard]] static auto create(
            journal::policy debug_policy = default_debug_policy()
        ) noexcept -> std::optional<signal_adapter>;

        explicit signal_adapter(journal::policy debug_policy = default_debug_policy());
        ~signal_adapter() noexcept;

        signal_adapter(const signal_adapter&) = delete;
        auto operator=(const signal_adapter&) -> signal_adapter& = delete;
        signal_adapter(signal_adapter&& other) noexcept;
        auto operator=(signal_adapter&& other) noexcept -> signal_adapter&;

        // Checks whether the dedicated background listener thread is currently active.
        [[nodiscard]] auto is_running() const noexcept -> bool;

        // Stops the background listener thread and restores the signal mask on the calling thread.
        void stop() noexcept;

        // Masks SIGUSR1 and SIGUSR2 on the calling thread, storing the previous mask into saved_mask.
        static auto block_signals(sigset_t& saved_mask) noexcept -> bool;

        // Restores the given signal mask on the calling thread.
        static auto restore_signals(const sigset_t& saved_mask) noexcept -> bool;

        // Discards any pending SIGUSR1 or SIGUSR2 instances without blocking.
        static void drain_pending_signals() noexcept;

    private:
        static void listener_loop(std::stop_token stop_token, journal::policy debug_policy) noexcept;

        journal::policy _debug_policy{};
        std::jthread _worker{};
        sigset_t _saved_mask{};
        bool _signals_blocked{false};
    };

} // namespace tle::journal_signal_adapter
