#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>

namespace tle::perf_scope {

    namespace detail {
        void record_workload_exception() noexcept;
    }

    // Physical hardware and timing metrics captured strictly across a component's hot path.
    struct hardware_metrics {
        double wall_ns_per_op{0.0};
        double cycles_per_op{0.0};
        double instructions_per_op{0.0};
        double ipc{0.0};
        double branch_misses_per_op{0.0};
        double cache_misses_per_op{0.0};
        bool pmu_counters_active{false};
    };

    // Component birth baseline state stored in <name>BENCH/baseline.json.
    struct score_baseline {
        std::string component;
        hardware_metrics birth_metrics{};
    };
    using benchmark_baseline = score_baseline;

    // Result of comparing current component metrics against its birth baseline.
    struct score_evaluation {
        hardware_metrics current_metrics{};
        double speedup_ratio{1.0};
        std::string_view bottleneck{"compute_bound"};
        std::string_view decision{"continue_optimizing"};
        bool worth_optimizing{true};
    };
    using benchmark_evaluation = score_evaluation;

    // RAII Linux perf_event_open hardware counter guard for isolated component benchmarking.
    class counter_guard {
    public:
        counter_guard() noexcept;
        ~counter_guard() noexcept;

        counter_guard(const counter_guard&) = delete;
        auto operator=(const counter_guard&) -> counter_guard& = delete;
        counter_guard(counter_guard&&) = delete;
        auto operator=(counter_guard&&) -> counter_guard& = delete;

        void start() noexcept;
        [[nodiscard]] auto stop(std::size_t operations) const noexcept -> hardware_metrics;

    private:
        int _fd_cycles{-1};
        int _fd_instructions{-1};
        int _fd_branch_misses{-1};
        int _fd_cache_misses{-1};
        std::int64_t _start_ns{0};
    };

    // Executes a callable for the specified iteration count under a hardware counter_guard.
    template <typename Callable>
        requires std::invocable<Callable>
    [[nodiscard]] inline auto measure_workload(
        std::size_t iterations,
        Callable&& workload
    ) noexcept -> hardware_metrics {
        try {
            std::size_t const count = std::max<std::size_t>(1U, iterations);
            workload();

            counter_guard guard;
            for ([[maybe_unused]] std::size_t const i : std::views::iota(std::size_t{0U}, count)) {
                workload();
            }
            return guard.stop(count);
        } catch (...) {
            detail::record_workload_exception();
            return hardware_metrics{};
        }
    }

    // Loads a component birth baseline from JSON on disk, or writes default_baseline if missing.
    [[nodiscard]] auto load_or_create_baseline(
        const std::filesystem::path& baseline_path,
        const score_baseline& default_baseline
    ) noexcept -> score_baseline;

    // Saves a component birth baseline to JSON on disk.
    [[nodiscard]] auto save_baseline(
        const std::filesystem::path& baseline_path,
        const score_baseline& baseline
    ) noexcept -> bool;

    // Evaluates current metrics against the component birth baseline.
    [[nodiscard]] auto evaluate_performance(
        const score_baseline& baseline,
        const hardware_metrics& current
    ) noexcept -> score_evaluation;

    [[nodiscard]] inline auto evaluate_score(
        const score_baseline& baseline,
        const hardware_metrics& current
    ) noexcept -> score_evaluation {
        return evaluate_performance(baseline, current);
    }

    // Formats an objective hardware report for engineers and optimization loops.
    [[nodiscard]] auto format_performance_report(
        const score_baseline& baseline,
        const score_evaluation& evaluation
    ) noexcept -> std::string;

    [[nodiscard]] inline auto format_score_report(
        const score_baseline& baseline,
        const score_evaluation& evaluation
    ) noexcept -> std::string {
        return format_performance_report(baseline, evaluation);
    }

} // namespace tle::perf_scope
