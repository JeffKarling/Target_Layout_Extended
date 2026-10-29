#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace target_layout_extended::perf_scope {

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

    // Cost-to-score importance curve defined by the human orchestrator.
    enum class utility_curve : std::uint8_t {
        linear = 0,
        logarithmic = 1,
        diminishing_returns = 2
    };

    // Component birth baseline state stored in <name>BENCH/baseline.json.
    struct score_baseline {
        std::string component;
        hardware_metrics birth_metrics{};
        double human_baseline_score{5.0};
        double target_score{8.0};
        std::size_t token_budget{4000U};
        utility_curve curve{utility_curve::logarithmic};
    };

    // Result of comparing current component metrics against its birth baseline.
    struct score_evaluation {
        hardware_metrics current_metrics{};
        double speedup_ratio{1.0};
        double achieved_score{5.0};
        double score_delta{0.0};
        double marginal_gain_next_20pct{0.0};
        bool worth_further_tokens{true};
        std::string_view decision{"continue_optimizing"};
    };

    // RAII Linux perf_event_open hardware counter guard for zero-noise hot-path isolation.
    // Replaces proprietary Intel ITT __itt_resume() / __itt_pause() instrumentation.
    class counter_guard {
    public:
        counter_guard() noexcept;
        ~counter_guard() noexcept;

        counter_guard(const counter_guard&) = delete;
        auto operator=(const counter_guard&) -> counter_guard& = delete;
        counter_guard(counter_guard&&) = delete;
        auto operator=(counter_guard&&) -> counter_guard& = delete;

        void start() noexcept;
        [[nodiscard]] auto stop(std::size_t operations) noexcept -> hardware_metrics;

    private:
        int _fd_cycles{-1};
        int _fd_instructions{-1};
        int _fd_branch_misses{-1};
        int _fd_cache_misses{-1};
        std::int64_t _start_ns{0};
    };

    // Executes a callable for the specified iteration count under a zero-noise counter_guard.
    [[nodiscard]] auto measure_workload(
        std::size_t iterations,
        const std::function<void()>& workload
    ) noexcept -> hardware_metrics;

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

    // Evaluates current metrics against the component birth baseline using the human score curve.
    [[nodiscard]] auto evaluate_score(
        const score_baseline& baseline,
        const hardware_metrics& current
    ) noexcept -> score_evaluation;

    // Formats a compact, low-token markdown report for developers and AI optimization loops.
    [[nodiscard]] auto format_score_report(
        const score_baseline& baseline,
        const score_evaluation& evaluation
    ) noexcept -> std::string;

} // namespace target_layout_extended::perf_scope
