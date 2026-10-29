#include <gtest/gtest.h>
#include <journal/journal.hpp>
#include <stdexcept>

namespace {
    namespace jrn = target_layout_extended::journal;

    [[nodiscard]] auto boundary_function_that_throws(int mode) noexcept -> bool {
        try {
            if (mode == 1) {
                throw std::invalid_argument("invalid mode argument");
            }
            if (mode == 2) {
                throw std::runtime_error("runtime failure inside component");
            }
            if (mode == 3) {
                throw 42;
            }
            return true;
        } catch (...) {
            return jrn::catch_boundary_exception(jrn::catalog::k_unhandled_boundary_exception);
        }
    }
} // namespace

TEST(JournalTest, ValidatesCatalogMessageIdFormat) {
    EXPECT_TRUE(jrn::is_valid_message_id(jrn::catalog::k_unhandled_boundary_exception.message_id));
    EXPECT_FALSE(jrn::is_valid_message_id("too_short"));
    EXPECT_FALSE(jrn::is_valid_message_id("7A1D4F90E3C84A21B5809E41D2A7C001"));
}

TEST(JournalTest, LippincottFunctionCatchesAllExceptionTypesAtBoundary) {
    EXPECT_TRUE(boundary_function_that_throws(0));
    EXPECT_FALSE(boundary_function_that_throws(1));
    EXPECT_FALSE(boundary_function_that_throws(2));
    EXPECT_FALSE(boundary_function_that_throws(3));
}

TEST(JournalTest, NonCatalogedLogsEmitStructuredComponentEvents) {
    jrn::log_info("journal", "internal diagnostic trace without catalog UUID");
    jrn::log_error("journal", "internal operational error without catalog UUID");
}
