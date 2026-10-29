#include <gtest/gtest.h>
#include <perf_scope/perf_scope.hpp>
#include <filesystem>
#include <numeric>
#include <vector>

namespace ps = target_layout_extended::perf_scope;

TEST(PerfScopeTest, MeasuresHotWorkloadMetrics) {
    std::vector<int> values(1024U, 3);
    auto const metrics = ps::measure_workload(10U, [&values]() {
        int sum = std::accumulate(values.begin(), values.end(), 0);
        values[0] = (sum > 0) ? 3 : 1;
    });
    EXPECT_GT(metrics.wall_ns_per_op, 0.0);
}

TEST(PerfScopeTest, EvaluatesDiminishingReturnsAndSavesBaseline) {
    std::filesystem::path const temp_baseline = "test_perf_baseline.json";
    std::error_code ec;
    std::filesystem::remove(temp_baseline, ec);

    ps::score_baseline birth{};
    birth.component = "demo_component";
    birth.human_baseline_score = 5.0;
    birth.target_score = 8.0;
    birth.token_budget = 3000U;
    birth.curve = ps::utility_curve::logarithmic;
    birth.birth_metrics.wall_ns_per_op = 1000.0;

    auto const loaded = ps::load_or_create_baseline(temp_baseline, birth);
    EXPECT_EQ(loaded.component, "demo_component");
    EXPECT_DOUBLE_EQ(loaded.human_baseline_score, 5.0);

    // 2x speedup on logarithmic curve adds +2.0 points (5.0 -> 7.0)
    ps::hardware_metrics fast{};
    fast.wall_ns_per_op = 500.0;
    auto const eval = ps::evaluate_score(loaded, fast);
    EXPECT_NEAR(eval.speedup_ratio, 2.0, 0.01);
    EXPECT_NEAR(eval.achieved_score, 7.0, 0.05);
    EXPECT_TRUE(eval.worth_further_tokens);

    // 4x speedup reaches 9.0 >= target_score (8.0) -> stops optimizing
    ps::hardware_metrics target_hit{};
    target_hit.wall_ns_per_op = 250.0;
    auto const eval_done = ps::evaluate_score(loaded, target_hit);
    EXPECT_GE(eval_done.achieved_score, 8.0);
    EXPECT_FALSE(eval_done.worth_further_tokens);
    EXPECT_EQ(eval_done.decision, "target_score_reached");

    std::filesystem::remove(temp_baseline, ec);
}
