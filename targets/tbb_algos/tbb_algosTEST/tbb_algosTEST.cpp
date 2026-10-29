#include <gtest/gtest.h>
#include <tbb_algos/tbb_algos.hpp>
#include <array>
#include <string>
#include <vector>

namespace ta = target_layout_extended::tbb_algos;

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
