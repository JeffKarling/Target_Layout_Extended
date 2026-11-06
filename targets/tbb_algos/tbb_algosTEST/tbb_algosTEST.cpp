#include <gtest/gtest.h>
#include <tbb_algos/tbb_algos.hpp>
#include <array>
#include <string>
#include <vector>

namespace ta = tle::tbb_algos;

static_assert(tle::journal::single_entry_catalog<ta::catalog>);
static_assert(tle::journal::is_valid_catalog(ta::k_catalog));

TEST(TbbAlgosTest, ParallelDoubleDoublesValues) {
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    EXPECT_TRUE(ta::parallel_double(numbers));

    std::vector<int> const expected = {2, 4, 6, 8, 10};
    EXPECT_EQ(numbers, expected);
}

TEST(TbbAlgosTest, ParallelDoubleAcceptsSpanFromStackArray) {
    std::array<int, 5> numbers = {10, 20, 30, 40, 50};
    EXPECT_TRUE(ta::parallel_double(numbers));

    std::array<int, 5> const expected = {20, 40, 60, 80, 100};
    EXPECT_EQ(numbers, expected);
}

TEST(TbbAlgosTest, ParallelTrigWorkRejectsZeroIterationsAtBoundary) {
    std::vector<double> values = {1.0, 2.0, 3.0, 4.0};
    EXPECT_FALSE(ta::parallel_trig_work(values, 0U));
    EXPECT_TRUE(ta::parallel_trig_work(values, 64U));
}

TEST(TbbAlgosTest, ParallelPolynomialFmaComputesCorrectValues) {
    std::vector<float> const input = {1.0F, 2.0F, 3.0F, 4.0F};
    std::vector<float> output(input.size(), 0.0F);

    EXPECT_TRUE(ta::parallel_polynomial_fma(output, input, 2.0F, 3.0F, 4.0F));

    std::vector<float> const expected = {9.0F, 18.0F, 31.0F, 48.0F};
    EXPECT_EQ(output, expected);
}

TEST(TbbAlgosTest, ParallelPolynomialFmaRejectsMismatchedSizes) {
    std::vector<float> const input = {1.0F, 2.0F, 3.0F, 4.0F};
    std::vector<float> output(3U, 0.0F);

    EXPECT_FALSE(ta::parallel_polynomial_fma(output, input, 1.0F, 1.0F, 1.0F));
}

TEST(TbbAlgosTest, ParallelPolynomialFmaHandlesEmptySpans) {
    std::vector<float> empty_in{};
    std::vector<float> empty_out{};

    EXPECT_TRUE(ta::parallel_polynomial_fma(empty_out, empty_in, 1.0F, 1.0F, 1.0F));
}

TEST(TbbAlgosTest, ParallelPolynomialFmaAcceptsSpanFromStackArray) {
    std::array<float, 3> const input = {0.0F, 1.0F, 2.0F};
    std::array<float, 3> output = {0.0F, 0.0F, 0.0F};

    EXPECT_TRUE(ta::parallel_polynomial_fma(output, input, 1.0F, 1.0F, 1.0F));

    std::array<float, 3> const expected = {1.0F, 3.0F, 7.0F};
    EXPECT_EQ(output, expected);
}
