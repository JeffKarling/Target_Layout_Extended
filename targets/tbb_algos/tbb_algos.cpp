#include <tbb_algos/tbb_algos.hpp>
#include <journal/journal.hpp>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_for_each.h>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>

namespace {

    __attribute__((target_clones("default", "arch=x86-64-v3", "arch=x86-64-v4")))
    void evaluate_polynomial_kernel(
        float* out,
        float const* in,
        std::size_t count,
        float a,
        float b,
        float c
    ) noexcept {
        for (std::size_t i = 0; i < count; ++i) {
            float const x = in[i];
            out[i] = (x * x * a) + (x * b) + c;
        }
    }

} // namespace

namespace tle::tbb_algos {

    auto parallel_double(std::span<int> numbers) noexcept -> bool {
        try {
            oneapi::tbb::parallel_for_each(numbers.begin(), numbers.end(), [](int& num) noexcept {
                num *= 2;
            });
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog);
        }
    }

    auto parallel_trig_work(
        std::span<double> values,
        std::size_t inner_iterations
    ) noexcept -> bool {
        try {
            if (inner_iterations == 0U) {
                throw std::invalid_argument("inner_iterations must be greater than zero");
            }
            oneapi::tbb::parallel_for_each(values.begin(), values.end(), [inner_iterations](double& val) noexcept {
                double acc = val;
                for (std::size_t const i : std::views::iota(std::size_t{0U}, inner_iterations)) {
                    auto const step = static_cast<double>(i) * 0.001;
                    acc += std::sin(acc + step) * std::cos(acc - step);
                }
                val = acc;
            });
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog);
        }
    }

    auto parallel_polynomial_fma(
        std::span<float> output,
        std::span<const float> input,
        float a,
        float b,
        float c
    ) noexcept -> bool {
        try {
            if (output.size() != input.size()) {
                throw std::invalid_argument("output and input span sizes must match");
            }
            if (output.empty()) {
                return true;
            }
            auto const total_size = output.size();
            oneapi::tbb::parallel_for(
                oneapi::tbb::blocked_range<std::size_t>(0U, total_size, 2048U),
                [output, input, a, b, c](oneapi::tbb::blocked_range<std::size_t> const& range) noexcept {
                    evaluate_polynomial_kernel(
                        output.data() + range.begin(),
                        input.data() + range.begin(),
                        range.size(),
                        a, b, c
                    );
                }
            );
            return true;
        } catch (...) {
            return journal::catch_boundary_exception(k_catalog);
        }
    }

} // namespace tle::tbb_algos
