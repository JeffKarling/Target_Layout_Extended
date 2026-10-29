#include <tbb_algos/tbb_algos.hpp>
#include <oneapi/tbb/parallel_for_each.h>
#include <cmath>
#include <stdexcept>

namespace target_layout_extended::tbb_algos {

    auto parallel_double(std::vector<int>& numbers) noexcept -> bool {
        try {
            oneapi::tbb::parallel_for_each(numbers.begin(), numbers.end(), [](int& n) noexcept {
                n *= 2;
            });
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(catalog::k_parallel_execution_failed);
        }
    }

    auto parallel_trig_work(
        std::vector<double>& values,
        std::size_t inner_iterations
    ) noexcept -> bool {
        try {
            if (inner_iterations == 0U) {
                throw std::invalid_argument("inner_iterations must be greater than zero");
            }
            oneapi::tbb::parallel_for_each(values.begin(), values.end(), [inner_iterations](double& v) noexcept {
                double acc = v;
                for (std::size_t i = 0U; i < inner_iterations; ++i) {
                    auto const step = static_cast<double>(i) * 0.001;
                    acc += std::sin(acc + step) * std::cos(acc - step);
                }
                v = acc;
            });
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(catalog::k_parallel_execution_failed);
        }
    }

} // namespace target_layout_extended::tbb_algos
