#include <gtest/gtest.h>
#include <journal/journal.hpp>
#include <stdexcept>
#include <string>

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

TEST(JournalTest, ExtractsComponentNameFromPaths) {
    EXPECT_EQ(jrn::extract_component("/home/user/project/targets/app/app.cpp"), "app");
    EXPECT_EQ(jrn::extract_component("targets/database_manager/database_helper.cpp"), "database_manager");
    EXPECT_EQ(jrn::extract_component("./targets/journal/journalTEST/journalTEST.cpp"), "journal");
    EXPECT_EQ(jrn::extract_component("targets/foo.cpp"), "foo");
    EXPECT_EQ(jrn::extract_component("/usr/include/foo.h"), "unknown");
}

TEST(JournalTest, HandlesRenamedTargetRootDirectory) {
    EXPECT_EQ(jrn::extract_component("/home/user/project/target2/app/app.cpp", "target2/"), "app");
    EXPECT_EQ(jrn::extract_component("target2/database_manager/database_helper.cpp", "target2/"), "database_manager");
    EXPECT_EQ(jrn::extract_component("/path/to/modules/core/core.cpp", "modules/"), "core");
}

TEST(JournalTest, HandlesWindowsBackslashesAndLegacyPrefixes) {
    EXPECT_EQ(jrn::extract_component(R"(C:\projects\Target_Layout\targets\app\app.cpp)"), "app");
    EXPECT_EQ(jrn::extract_component(R"(targets\database_manager\database_helper.cpp)"), "database_manager");
    EXPECT_EQ(jrn::extract_component("srcTargets/geometry/geometry.cpp"), "geometry");
    EXPECT_EQ(jrn::extract_component(R"(srcTargets\geometry\geometry.cpp)"), "geometry");
}

TEST(JournalTest, HandlesEdgeCasesAndFallbacksSafely) {
    EXPECT_EQ(jrn::extract_component(""), "unknown");
    EXPECT_EQ(jrn::extract_component("targets/"), "unknown");
    EXPECT_EQ(jrn::extract_component("targets//foo.cpp"), "foo");
    EXPECT_EQ(jrn::extract_component("/home/user/targets/"), "unknown");
    EXPECT_EQ(jrn::extract_component("/some/random/path/file.cpp"), "unknown");
}

static_assert(jrn::extract_component("/home/user/project/targets/app/app.cpp") == "app");
static_assert(jrn::extract_component("/path/to/target2/my_comp/my_comp.cpp", "target2/") == "my_comp");
static_assert(jrn::extract_component("") == "unknown");

TEST(JournalTest, NonCatalogedLogsEmitStructuredComponentEvents) {
    jrn::log_info("internal diagnostic trace without catalog UUID");
    jrn::log_error("internal operational error without catalog UUID");
}
