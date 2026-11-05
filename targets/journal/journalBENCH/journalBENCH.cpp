#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <journal/journal.hpp>
#include <perf_scope/perf_scope.hpp>
#include <ranges>
#include <span>
#include <string>
#include <system_error>

int main() {
  namespace ps = tle::perf_scope;
  namespace jrn = tle::journal;

  // Keep the benchmark off the real journal and make sure the consumer thread
  // exists before timing.
  jrn::detail::set_record_sink(
      {[](void *, const jrn::detail::emitted_record &) noexcept {}, nullptr});
  jrn::log(jrn::severity::info, jrn::message{"warm up"});
  (void)jrn::flush(std::chrono::milliseconds{5000});

  std::array<std::byte, 64U> const payload{};
  static constexpr std::size_t k_batch = 1024U;

  // One operation is a batch of log calls that fits inside the ring so the
  // producer never laps the consumer. The default policy keeps debug records in
  // the ring, which is the flight recorder hot path.
  auto const current_metrics = ps::measure_workload(200U, [&payload]() {
    for ([[maybe_unused]] std::size_t const idx :
         std::views::iota(std::size_t{0U}, k_batch)) {
      jrn::log(jrn::severity::debug, jrn::message{"benchmark event"},
               jrn::binary_payload{payload});
    }
  });
  (void)jrn::flush(std::chrono::milliseconds{5000});

  // Producer latency per call, measured in ring sized bursts with the consumer
  // drained in between.
  std::chrono::nanoseconds producer_time{0};
  constexpr std::size_t k_bursts = 200U;
  for ([[maybe_unused]] std::size_t const burst :
       std::views::iota(std::size_t{0U}, k_bursts)) {
    auto const started = std::chrono::steady_clock::now();
    for ([[maybe_unused]] std::size_t const idx :
         std::views::iota(std::size_t{0U}, k_batch)) {
      jrn::log(jrn::severity::debug, jrn::message{"benchmark event"},
               jrn::binary_payload{payload});
    }
    producer_time += std::chrono::steady_clock::now() - started;
    (void)jrn::flush(std::chrono::milliseconds{5000});
  }
  std::chrono::nanoseconds text_time{0};
  for ([[maybe_unused]] std::size_t const burst :
       std::views::iota(std::size_t{0U}, k_bursts)) {
    auto const started = std::chrono::steady_clock::now();
    for ([[maybe_unused]] std::size_t const idx :
         std::views::iota(std::size_t{0U}, k_batch)) {
      jrn::log(jrn::severity::debug, jrn::message{"benchmark event"});
    }
    text_time += std::chrono::steady_clock::now() - started;
    (void)jrn::flush(std::chrono::milliseconds{5000});
  }
  std::cout << "producer latency text only: "
            << static_cast<double>(text_time.count()) /
                   static_cast<double>(k_bursts * k_batch)
            << " ns per call\n";
  std::cout << "producer latency: "
            << static_cast<double>(producer_time.count()) /
                   static_cast<double>(k_bursts * k_batch)
            << " ns per call, " << k_batch << " calls per workload operation\n";

  ps::score_baseline default_birth{};
  default_birth.component = "journal";
  default_birth.human_baseline_score = 6.0;
  default_birth.target_score = 8.5;
  default_birth.token_budget = 4000U;
  default_birth.curve = ps::utility_curve::logarithmic;
  default_birth.birth_metrics = current_metrics;

  std::filesystem::path const baseline_path = COMPONENT_BASELINE_PATH;
  auto const baseline =
      ps::load_or_create_baseline(baseline_path, default_birth);
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

  jrn::detail::reset_record_sink();
  return 0;
}
