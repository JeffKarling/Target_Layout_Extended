#include <perf_scope/perf_scope.hpp>
#include <journal/journal.hpp>

#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>
// IWYU pragma: no_include <stdexcept>

namespace tle::perf_scope {

    namespace {
        namespace jrn = tle::journal;

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

            auto const file_desc = syscall(SYS_perf_event_open, &attr, 0, -1, group_fd, 0UL);
            return static_cast<int>(file_desc);
        }

        [[nodiscard]] auto read_counter_value(int file_desc) noexcept -> std::uint64_t {
            if (file_desc < 0) {
                return 0ULL;
            }
            std::uint64_t value = 0ULL;
            ssize_t const bytes = ::read(file_desc, &value, sizeof(value));
            if (std::cmp_not_equal(bytes, sizeof(value))) {
                return 0ULL;
            }
            return value;
        }

        [[nodiscard]] auto steady_now_ns() noexcept -> std::int64_t {
            auto const now = std::chrono::steady_clock::now().time_since_epoch();
            return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        }

        inline constexpr double k_min_worthwhile_speedup = 1.03;
        inline constexpr double k_regression_threshold = 0.98;
        inline constexpr double k_baseline_tolerance = 0.001;
        inline constexpr double k_saturation_ipc_threshold = 2.5;
        inline constexpr double k_cache_miss_threshold = 10.0;
        inline constexpr double k_branch_miss_threshold = 5.0;
        inline constexpr double k_instruction_bloat_ratio = 1.15;

        [[nodiscard]] auto diagnose_bottleneck(
            const hardware_metrics& current,
            const hardware_metrics& birth
        ) noexcept -> std::string_view {
            if (!current.pmu_counters_active) {
                return "unknown_pmu_inactive";
            }
            if (current.cache_misses_per_op >= k_cache_miss_threshold) {
                return "memory_bound";
            }
            if (current.branch_misses_per_op >= k_branch_miss_threshold) {
                return "branch_stalled";
            }
            if (birth.instructions_per_op > 0.0 &&
                current.instructions_per_op > (birth.instructions_per_op * k_instruction_bloat_ratio)) {
                return "instruction_bloat";
            }
            if (current.ipc >= k_saturation_ipc_threshold) {
                return "hardware_saturated";
            }
            return "compute_bound";
        }

        [[nodiscard]] auto determine_directive(
            double speedup_ratio,
            std::string_view bottleneck
        ) noexcept -> std::pair<std::string_view, bool> {
            if (speedup_ratio < k_regression_threshold) {
                return {"regression_detected", false};
            }
            if (bottleneck == "hardware_saturated") {
                return {"stop_hardware_saturated", false};
            }
            if (speedup_ratio > (1.0 + k_baseline_tolerance) && speedup_ratio < k_min_worthwhile_speedup) {
                return {"stop_diminishing_returns", false};
            }
            return {"continue_optimizing", true};
        }

        [[nodiscard]] auto extract_json_string(std::string_view content, std::string_view key) -> std::string_view {
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
            return content.substr(first_quote + 1U, second_quote - first_quote - 1U);
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
            while (start < content.size() && (content.at(start) == ' ' || content.at(start) == '\t' || content.at(start) == '\n')) {
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

    counter_guard::counter_guard() noexcept
        : _fd_cycles(open_hw_counter(PERF_COUNT_HW_CPU_CYCLES, -1)) {
        if (_fd_cycles >= 0) {
            _fd_instructions = open_hw_counter(PERF_COUNT_HW_INSTRUCTIONS, _fd_cycles);
            _fd_branch_misses = open_hw_counter(PERF_COUNT_HW_BRANCH_MISSES, _fd_cycles);
            _fd_cache_misses = open_hw_counter(PERF_COUNT_HW_CACHE_MISSES, _fd_cycles);
        }
        start();
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

    auto counter_guard::stop(std::size_t operations) const noexcept -> hardware_metrics {
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

    namespace detail {
        void record_workload_exception() noexcept {
            (void)jrn::catch_boundary_exception(k_workload_exception);
        }
    }

    auto save_baseline(
        const std::filesystem::path& baseline_path,
        const score_baseline& baseline
    ) noexcept -> bool {
        try {
            if (baseline_path.has_parent_path()) {
                std::error_code err_code;
                std::filesystem::create_directories(baseline_path.parent_path(), err_code);
            }
            std::ofstream out(baseline_path, std::ios::out | std::ios::trunc);
            if (!out.is_open()) {
                throw jrn::catalog_exception(k_baseline_io_error, baseline_path.string());
            }
            out << std::fixed << std::setprecision(3);
            out << "{\n";
            out << R"(  "component": ")" << baseline.component << "\",\n";
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
            std::error_code err_code;
            if (!std::filesystem::exists(baseline_path, err_code)) {
                (void)save_baseline(baseline_path, default_baseline);
                return default_baseline;
            }

            std::ifstream input(baseline_path, std::ios::in);
            if (!input.is_open()) {
                return default_baseline;
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            std::string const content = buffer.str();

            score_baseline loaded = default_baseline;
            std::string_view const comp = extract_json_string(content, "component");
            if (!comp.empty()) {
                loaded.component = std::string(comp);
            }
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

    auto evaluate_performance(
        const score_baseline& baseline,
        const hardware_metrics& current
    ) noexcept -> score_evaluation {
        score_evaluation eval{};
        eval.current_metrics = current;

        double const base_ns = std::max(1.0, baseline.birth_metrics.wall_ns_per_op);
        double const curr_ns = std::max(1.0, current.wall_ns_per_op);
        eval.speedup_ratio = base_ns / curr_ns;

        eval.bottleneck = diagnose_bottleneck(current, baseline.birth_metrics);
        auto const [decision, worth] = determine_directive(eval.speedup_ratio, eval.bottleneck);
        eval.decision = decision;
        eval.worth_optimizing = worth;
        return eval;
    }

    auto format_performance_report(
        const score_baseline& baseline,
        const score_evaluation& evaluation
    ) noexcept -> std::string {
        try {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2);
            out << "# Component Benchmark Report (" << baseline.component << ")\n";
            out << "- Latency (ns/op):      " << evaluation.current_metrics.wall_ns_per_op
                << " (birth: " << baseline.birth_metrics.wall_ns_per_op
                << ", speedup: " << evaluation.speedup_ratio << "x)\n";
            if (evaluation.current_metrics.pmu_counters_active) {
                out << "- Hardware PMU/op:      cycles=" << evaluation.current_metrics.cycles_per_op
                    << ", instructions=" << evaluation.current_metrics.instructions_per_op
                    << ", IPC=" << evaluation.current_metrics.ipc
                    << ", branch_misses=" << evaluation.current_metrics.branch_misses_per_op
                    << ", cache_misses=" << evaluation.current_metrics.cache_misses_per_op << "\n";
            } else {
                out << "- Hardware PMU/op:      steady_clock fallback active (kernel perf_event_paranoid > 1)\n";
            }
            out << "- Bottleneck:           " << evaluation.bottleneck << "\n";
            out << "- Directive:            " << evaluation.decision << "\n";
            return out.str();
        } catch (...) {
            (void)jrn::catch_boundary_exception(k_workload_exception);
            return {};
        }
    }


} // namespace tle::perf_scope
