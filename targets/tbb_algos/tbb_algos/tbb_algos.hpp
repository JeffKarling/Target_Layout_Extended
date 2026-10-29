#pragma once

#include <cstddef>
#include <span>
#include <vector>
#include <journal/journal.hpp>

namespace target_layout_extended::tbb_algos {

    namespace catalog {
        inline constexpr journal::catalog_entry k_parallel_execution_failed{
            .message_id = "6d3f8912a0c54e37b9241f08c3e2a201",
            .component = "tbb_algos",
            .summary = "Parallel TBB workload failure caught at component boundary"
        };
    } // namespace catalog

    inline constexpr std::size_t k_default_trig_iterations = 2500U;

    // Doubles every element in parallel using oneTBB across any contiguous container or slice.
    [[nodiscard]] auto parallel_double(std::span<int> numbers) noexcept -> bool;

    // Performs a CPU-intensive parallel trigonometric reduction over a contiguous span for hardware benchmarking.
    [[nodiscard]] auto parallel_trig_work(
        std::span<double> values,
        std::size_t inner_iterations = k_default_trig_iterations
    ) noexcept -> bool;

} // namespace target_layout_extended::tbb_algos
