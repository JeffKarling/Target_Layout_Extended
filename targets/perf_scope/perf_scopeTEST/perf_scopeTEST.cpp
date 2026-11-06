#include <gtest/gtest.h>
#include <perf_scope/perf_scope.hpp>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <ranges>
#include <string>
#include <system_error>
#include <vector>

namespace ps = tle::perf_scope;

TEST(PerfScopeTest, MeasuresHotWorkloadMetrics) {
    std::vector<int> values(1024U, 3);
    auto const metrics = ps::measure_workload(10U, [&values]() {
        int const sum = std::ranges::fold_left(values, 0, std::plus<>{});
        values[0] = (sum > 0) ? 3 : 1;
    });
    EXPECT_GT(metrics.wall_ns_per_op, 0.0);
}

TEST(PerfScopeTest, CounterGuardStartsOnConstruction) {
    ps::counter_guard guard;
    volatile int dummy = 0;
    for (int const i : std::views::iota(0, 1000)) {
        dummy += i;
    }
    (void)dummy;
    auto const metrics = guard.stop(1000U);
    EXPECT_GT(metrics.wall_ns_per_op, 0.0);
}

TEST(PerfScopeTest, EvaluatesDiminishingReturnsAndSavesBaseline) {
    std::filesystem::path const temp_baseline = "test_perf_baseline.json";
    std::error_code ec;
    std::filesystem::remove(temp_baseline, ec);

    ps::score_baseline birth{};
    birth.component = "demo_component";
    birth.birth_metrics.wall_ns_per_op = 1000.0;
    birth.birth_metrics.cycles_per_op = 2000.0;
    birth.birth_metrics.instructions_per_op = 3000.0;
    birth.birth_metrics.ipc = 1.5;
    birth.birth_metrics.branch_misses_per_op = 1.0;
    birth.birth_metrics.cache_misses_per_op = 1.0;
    birth.birth_metrics.pmu_counters_active = true;

    auto const loaded = ps::load_or_create_baseline(temp_baseline, birth);
    EXPECT_EQ(loaded.component, "demo_component");
    EXPECT_DOUBLE_EQ(loaded.birth_metrics.wall_ns_per_op, 1000.0);

    // 2x speedup with balanced compute continues optimizing
    ps::hardware_metrics fast = birth.birth_metrics;
    fast.wall_ns_per_op = 500.0;
    fast.cycles_per_op = 1000.0;
    auto const eval = ps::evaluate_performance(loaded, fast);
    EXPECT_NEAR(eval.speedup_ratio, 2.0, 0.01);
    EXPECT_TRUE(eval.worth_optimizing);
    EXPECT_EQ(eval.decision, "continue_optimizing");
    EXPECT_EQ(eval.bottleneck, "compute_bound");

    // Micro gain under 3% speedup signals diminishing returns
    ps::hardware_metrics micro_gain = birth.birth_metrics;
    micro_gain.wall_ns_per_op = 985.0; // 1.015x speedup
    auto const eval_dim = ps::evaluate_performance(loaded, micro_gain);
    EXPECT_FALSE(eval_dim.worth_optimizing);
    EXPECT_EQ(eval_dim.decision, "stop_diminishing_returns");

    // Saturated pipeline (high IPC >= 2.5 with minimal misses) signals hardware saturation
    ps::hardware_metrics saturated = birth.birth_metrics;
    saturated.wall_ns_per_op = 250.0;
    saturated.ipc = 3.2;
    auto const eval_sat = ps::evaluate_performance(loaded, saturated);
    EXPECT_EQ(eval_sat.bottleneck, "hardware_saturated");
    EXPECT_FALSE(eval_sat.worth_optimizing);
    EXPECT_EQ(eval_sat.decision, "stop_hardware_saturated");

    std::filesystem::remove(temp_baseline, ec);
}
