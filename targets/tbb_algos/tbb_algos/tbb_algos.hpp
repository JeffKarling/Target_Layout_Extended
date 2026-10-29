#pragma once

#include <cstddef>
#include <vector>
#include <journal/journal.hpp>

namespace target_layout_extended::tbb_algos {

    namespace catalog {
        inline constexpr journal::catalog_entry k_parallel_execution_failed{
            .message_id = "6d3f8912a0c54e37b9241f08c3e2a201",
            .component = "tbb_algos",
            .summary = "Parallel TBB workload failed at component boundary"
        };
    } // namespace catalog

    // Doubles every element in parallel using oneTBB.
    [[nodiscard]] auto parallel_double(std::vector<int>& numbers) noexcept -> bool;

    // Performs a CPU-intensive parallel trigonometric reduction over a vector for hardware benchmarking.
    [[nodiscard]] auto parallel_trig_work(
        std::vector<double>& values,
        std::size_t inner_iterations = 2500U
    ) noexcept -> bool;

} // namespace target_layout_extended::tbb_algos
