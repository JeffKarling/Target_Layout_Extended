#include <perf_scope/perf_scope.hpp>
#include <journal/journal.hpp>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace target_layout_extended::perf_scope {

    namespace {
        namespace jrn = target_layout_extended::journal;

        inline constexpr jrn::catalog_entry k_baseline_io_error{
            .message_id = "9b2e5a11c4d74f08a6123e89f1b4d002",
            .component = "perf_scope",
            .summary = "Baseline file I/O failure caught at component boundary"
        };

        inline constexpr jrn::catalog_entry k_workload_exception{
            .message_id = "9b2e5a11c4d74f08a6123e89f1b4d003",
            .component = "perf_scope",
            .summary = "Benchmark workload exception caught at component boundary"
        };

        [[nodiscard]] auto open_hw_counter(std::uint64_t config, int group_fd) noexcept -> int {
            perf_event_attr attr{};
            attr.type = PERF_TYPE_HARDWARE;
            attr.size = sizeof(perf_event_attr);
            attr.config = config;
            attr.disabled = 1;
            attr.exclude_kernel = 1;
            attr.exclude_hv = 1;

            long const fd = syscall(SYS_perf_event_open, &attr, 0, -1, group_fd, 0UL);
            return static_cast<int>(fd);
        }

        [[nodiscard]] auto read_counter_value(int fd) noexcept -> std::uint64_t {
            if (fd < 0) {
                return 0ULL;
            }
            std::uint64_t value = 0ULL;
            ssize_t const bytes = ::read(fd, &value, sizeof(value));
            if (bytes != static_cast<ssize_t>(sizeof(value))) {
                return 0ULL;
            }
            return value;
        }

        [[nodiscard]] auto steady_now_ns() noexcept -> std::int64_t {
            auto const now = std::chrono::steady_clock::now().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        }

        [[nodiscard]] auto curve_to_string(utility_curve curve) noexcept -> std::string_view {
            switch (curve) {
                case utility_curve::linear:
                    return "linear";
                case utility_curve::logarithmic:
                    return "logarithmic";
                case utility_curve::diminishing_returns:
                    return "diminishing_returns";
            }
            return "logarithmic";
        }

        [[nodiscard]] auto string_to_curve(std::string_view name) noexcept -> utility_curve {
            if (name == "linear") {
                return utility_curve::linear;
            }
            if (name == "diminishing_returns") {
                return utility_curve::diminishing_returns;
            }
            return utility_curve::logarithmic;
        }

        [[nodiscard]] auto compute_curve_score(
            double base_score,
            double speedup,
            utility_curve curve
        ) noexcept -> double {
            if (speedup <= 0.0) {
                return 1.0;
            }
            double delta = 0.0;
            switch (curve) {
                case utility_curve::linear:
                    delta = (speedup - 1.0) * 2.0;
                    break;
                case utility_curve::logarithmic:
                    // Each 2x speedup yields +2.0 score points; gains taper naturally at higher speedups.
                    delta = 2.0 * std::log2(speedup);
                    break;
                case utility_curve::diminishing_returns: {
                    // Asymptotic curve: moving from 5 -> 7 is fast, while 7 -> 10 requires exponential speedup.
                    double const headroom = std::max(0.5, 10.0 - base_score);
                    if (speedup >= 1.0) {
                        delta = headroom * (1.0 - std::exp(-0.85 * (speedup - 1.0)));
                    } else {
                        delta = 2.5 * std::log2(speedup);
                    }
                    break;
                }
            }
            return std::clamp(base_score + delta, 1.0, 10.0);
        }

        [[nodiscard]] auto extract_json_string(std::string_view content, std::string_view key) -> std::string {
            std::string const pattern = "\"" + std::string(key) + "\"";
            auto const pos = content.find(pattern);
            if (pos == std::string_view::npos) {
                return {};
            }
            auto const colon = content.find(':', pos + pattern.size());
            if (colon == std::string_view::npos) {
                return {};
            }
            auto const first_quote = content.find('"', colon + 1U);
            if (first_quote == std::string_view::npos) {
                return {};
            }
            auto const second_quote = content.find('"', first_quote + 1U);
            if (second_quote == std::string_view::npos) {
                return {};
            }
            return std::string(content.substr(first_quote + 1U, second_quote - first_quote - 1U));
        }

        [[nodiscard]] auto extract_json_number(
            std::string_view content,
            std::string_view key,
            double fallback
        ) -> double {
            std::string const pattern = "\"" + std::string(key) + "\"";
            auto const pos = content.find(pattern);
            if (pos == std::string_view::npos) {
                return fallback;
            }
            auto const colon = content.find(':', pos + pattern.size());
            if (colon == std::string_view::npos) {
                return fallback;
            }
            auto start = colon + 1U;
            while (start < content.size() && (content[start] == ' ' || content[start] == '\t' || content[start] == '\n')) {
                ++start;
            }
            std::string const tail(content.substr(start, 48U));
            char* end_ptr = nullptr;
            double const parsed = std::strtod(tail.c_str(), &end_ptr);
            if (end_ptr == tail.c_str()) {
                return fallback;
            }
            return parsed;
        }
    } // namespace

    counter_guard::counter_guard() noexcept {
        _fd_cycles = open_hw_counter(PERF_COUNT_HW_CPU_CYCLES, -1);
        if (_fd_cycles >= 0) {
            _fd_instructions = open_hw_counter(PERF_COUNT_HW_INSTRUCTIONS, _fd_cycles);
            _fd_branch_misses = open_hw_counter(PERF_COUNT_HW_BRANCH_MISSES, _fd_cycles);
            _fd_cache_misses = open_hw_counter(PERF_COUNT_HW_CACHE_MISSES, _fd_cycles);
        }
    }

    counter_guard::~counter_guard() noexcept {
        if (_fd_cache_misses >= 0) {
            (void)::close(_fd_cache_misses);
        }
        if (_fd_branch_misses >= 0) {
            (void)::close(_fd_branch_misses);
        }
        if (_fd_instructions >= 0) {
            (void)::close(_fd_instructions);
        }
        if (_fd_cycles >= 0) {
            (void)::close(_fd_cycles);
        }
    }

    void counter_guard::start() noexcept {
        if (_fd_cycles >= 0) {
            (void)::ioctl(_fd_cycles, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
            (void)::ioctl(_fd_cycles, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
        }
        _start_ns = steady_now_ns();
    }

    auto counter_guard::stop(std::size_t operations) noexcept -> hardware_metrics {
        std::int64_t const elapsed_ns = std::max<std::int64_t>(1, steady_now_ns() - _start_ns);
        if (_fd_cycles >= 0) {
            (void)::ioctl(_fd_cycles, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
        }

        double const ops = static_cast<double>(std::max<std::size_t>(1U, operations));
        hardware_metrics metrics{};
        metrics.wall_ns_per_op = static_cast<double>(elapsed_ns) / ops;

        if (_fd_cycles >= 0) {
            std::uint64_t const cycles = read_counter_value(_fd_cycles);
            std::uint64_t const instructions = read_counter_value(_fd_instructions);
            std::uint64_t const branch_misses = read_counter_value(_fd_branch_misses);
            std::uint64_t const cache_misses = read_counter_value(_fd_cache_misses);

            metrics.pmu_counters_active = (cycles > 0ULL);
            metrics.cycles_per_op = static_cast<double>(cycles) / ops;
            metrics.instructions_per_op = static_cast<double>(instructions) / ops;
            if (cycles > 0ULL) {
                metrics.ipc = static_cast<double>(instructions) / static_cast<double>(cycles);
            }
            metrics.branch_misses_per_op = static_cast<double>(branch_misses) / ops;
            metrics.cache_misses_per_op = static_cast<double>(cache_misses) / ops;
        }
        return metrics;
    }

    auto measure_workload(
        std::size_t iterations,
        const std::function<void()>& workload
    ) noexcept -> hardware_metrics {
        try {
            std::size_t const count = std::max<std::size_t>(1U, iterations);
            // Warm up instruction and data caches once before enabling counters
            workload();

            counter_guard guard;
            guard.start();
            for (std::size_t i = 0U; i < count; ++i) {
                workload();
            }
            return guard.stop(count);
        } catch (...) {
            (void)jrn::catch_boundary_exception(k_workload_exception);
            return hardware_metrics{};
        }
    }

    auto save_baseline(
        const std::filesystem::path& baseline_path,
        const score_baseline& baseline
    ) noexcept -> bool {
        try {
            if (baseline_path.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(baseline_path.parent_path(), ec);
            }
            std::ofstream out(baseline_path, std::ios::out | std::ios::trunc);
            if (!out.is_open()) {
                jrn::log_error(k_baseline_io_error, baseline_path.string());
                return false;
            }
            out << std::fixed << std::setprecision(3);
            out << "{\n";
            out << "  \"component\": \"" << baseline.component << "\",\n";
            out << "  \"human_baseline_score\": " << baseline.human_baseline_score << ",\n";
            out << "  \"target_score\": " << baseline.target_score << ",\n";
            out << "  \"token_budget\": " << baseline.token_budget << ",\n";
            out << "  \"utility_curve\": \"" << curve_to_string(baseline.curve) << "\",\n";
            out << "  \"wall_ns_per_op\": " << baseline.birth_metrics.wall_ns_per_op << ",\n";
            out << "  \"cycles_per_op\": " << baseline.birth_metrics.cycles_per_op << ",\n";
            out << "  \"instructions_per_op\": " << baseline.birth_metrics.instructions_per_op << ",\n";
            out << "  \"ipc\": " << baseline.birth_metrics.ipc << ",\n";
            out << "  \"branch_misses_per_op\": " << baseline.birth_metrics.branch_misses_per_op << ",\n";
            out << "  \"cache_misses_per_op\": " << baseline.birth_metrics.cache_misses_per_op << "\n";
            out << "}\n";
            return out.good();
        } catch (...) {
            return jrn::catch_boundary_exception(k_baseline_io_error);
        }
    }

    auto load_or_create_baseline(
        const std::filesystem::path& baseline_path,
        const score_baseline& default_baseline
    ) noexcept -> score_baseline {
        try {
            std::error_code ec;
            if (!std::filesystem::exists(baseline_path, ec)) {
                (void)save_baseline(baseline_path, default_baseline);
                return default_baseline;
            }

            std::ifstream in(baseline_path, std::ios::in);
            if (!in.is_open()) {
                return default_baseline;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            std::string const content = buffer.str();

            score_baseline loaded = default_baseline;
            std::string const comp = extract_json_string(content, "component");
            if (!comp.empty()) {
                loaded.component = comp;
            }
            std::string const curve_str = extract_json_string(content, "utility_curve");
            if (!curve_str.empty()) {
                loaded.curve = string_to_curve(curve_str);
            }
            loaded.human_baseline_score = extract_json_number(content, "human_baseline_score", default_baseline.human_baseline_score);
            loaded.target_score = extract_json_number(content, "target_score", default_baseline.target_score);
            loaded.token_budget = static_cast<std::size_t>(
                std::max(0.0, extract_json_number(content, "token_budget", static_cast<double>(default_baseline.token_budget)))
            );
            loaded.birth_metrics.wall_ns_per_op = extract_json_number(content, "wall_ns_per_op", default_baseline.birth_metrics.wall_ns_per_op);
            loaded.birth_metrics.cycles_per_op = extract_json_number(content, "cycles_per_op", default_baseline.birth_metrics.cycles_per_op);
            loaded.birth_metrics.instructions_per_op = extract_json_number(content, "instructions_per_op", default_baseline.birth_metrics.instructions_per_op);
            loaded.birth_metrics.ipc = extract_json_number(content, "ipc", default_baseline.birth_metrics.ipc);
            loaded.birth_metrics.branch_misses_per_op = extract_json_number(content, "branch_misses_per_op", default_baseline.birth_metrics.branch_misses_per_op);
            loaded.birth_metrics.cache_misses_per_op = extract_json_number(content, "cache_misses_per_op", default_baseline.birth_metrics.cache_misses_per_op);
            return loaded;
        } catch (...) {
            (void)jrn::catch_boundary_exception(k_baseline_io_error);
            return default_baseline;
        }
    }

    auto evaluate_score(
        const score_baseline& baseline,
        const hardware_metrics& current
    ) noexcept -> score_evaluation {
        score_evaluation eval{};
        eval.current_metrics = current;

        double const base_ns = std::max(1.0, baseline.birth_metrics.wall_ns_per_op);
        double const curr_ns = std::max(1.0, current.wall_ns_per_op);
        eval.speedup_ratio = base_ns / curr_ns;

        eval.achieved_score = compute_curve_score(
            baseline.human_baseline_score,
            eval.speedup_ratio,
            baseline.curve
        );
        eval.score_delta = eval.achieved_score - baseline.human_baseline_score;

        double const projected_score_20pct = compute_curve_score(
            baseline.human_baseline_score,
            eval.speedup_ratio * 1.20,
            baseline.curve
        );
        eval.marginal_gain_next_20pct = projected_score_20pct - eval.achieved_score;

        if (eval.achieved_score >= baseline.target_score) {
            eval.worth_further_tokens = false;
            eval.decision = "target_score_reached";
        } else if (eval.marginal_gain_next_20pct < 0.25) {
            eval.worth_further_tokens = false;
            eval.decision = "stop_diminishing_returns";
        } else {
            eval.worth_further_tokens = true;
            eval.decision = "continue_optimizing";
        }
        return eval;
    }

    auto format_score_report(
        const score_baseline& baseline,
        const score_evaluation& evaluation
    ) noexcept -> std::string {
        try {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2);
            out << "# Component Benchmark & Score Report (" << baseline.component << ")\n";
            out << "- Birth baseline score: " << baseline.human_baseline_score
                << " / 10.00 (target: " << baseline.target_score
                << ", curve: " << curve_to_string(baseline.curve)
                << ", token budget: " << baseline.token_budget << ")\n";
            out << "- Latency (ns/op):      " << evaluation.current_metrics.wall_ns_per_op
                << " (birth: " << baseline.birth_metrics.wall_ns_per_op
                << ", speedup: " << evaluation.speedup_ratio << "x)\n";
            if (evaluation.current_metrics.pmu_counters_active) {
                out << "- Hardware PMU/op:      cycles=" << evaluation.current_metrics.cycles_per_op
                    << ", instructions=" << evaluation.current_metrics.instructions_per_op
                    << ", IPC=" << evaluation.current_metrics.ipc
                    << ", branch_misses=" << evaluation.current_metrics.branch_misses_per_op << "\n";
            } else {
                out << "- Hardware PMU/op:      steady_clock fallback active (kernel perf_event_paranoid > 1)\n";
            }
            out << "- Achieved score:       " << evaluation.achieved_score
                << " / 10.00 (delta: " << (evaluation.score_delta >= 0.0 ? "+" : "") << evaluation.score_delta << ")\n";
            out << "- Workflow decision:    " << evaluation.decision
                << " (marginal gain for +20% speedup: +" << evaluation.marginal_gain_next_20pct << " pts)\n";
            return out.str();
        } catch (...) {
            (void)jrn::catch_boundary_exception(k_workload_exception);
            return {};
        }
    }

} // namespace target_layout_extended::perf_scope
