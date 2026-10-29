#include <perf_scope/perf_scope.hpp>
#include <tbb_algos/tbb_algos.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

int main() {
    namespace ps = target_layout_extended::perf_scope;
    namespace ta = target_layout_extended::tbb_algos;

    std::vector<double> workload_data(256U, 1.25);

    auto const current_metrics = ps::measure_workload(20U, [&workload_data]() {
        (void)ta::parallel_trig_work(workload_data, 1500U);
    });

    ps::score_baseline default_birth{};
    default_birth.component = "tbb_algos";
    default_birth.human_baseline_score = 6.0;
    default_birth.target_score = 8.5;
    default_birth.token_budget = 4000U;
    default_birth.curve = ps::utility_curve::logarithmic;
    default_birth.birth_metrics = current_metrics;

    std::filesystem::path const baseline_path = COMPONENT_BASELINE_PATH;
    auto const baseline = ps::load_or_create_baseline(baseline_path, default_birth);
    auto const evaluation = ps::evaluate_score(baseline, current_metrics);
    auto const report = ps::format_score_report(baseline, evaluation);

    std::cout << report;

    std::filesystem::path const report_path = COMPONENT_REPORT_PATH;
    if (report_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(report_path.parent_path(), ec);
    }
    std::ofstream out(report_path, std::ios::out | std::ios::trunc);
    if (out.is_open()) {
        out << report;
    }

    return 0;
}
