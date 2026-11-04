#include <gtest/gtest.h>
#include <journal_signal_adapter/journal_signal_adapter.hpp>
#include <journal/journal.hpp>
#include <chrono>
#include <csignal>
#include <thread>
#include <unistd.h>

namespace jrn = target_layout_extended::journal;
namespace adapter = target_layout_extended::journal_signal_adapter;

static_assert(jrn::single_entry_catalog<adapter::catalog>);
static_assert(jrn::is_valid_catalog(adapter::k_catalog));

TEST(SignalAdapterTest, CatalogConformsToComponentFirewallSpecification) {
    EXPECT_TRUE(adapter::k_catalog.k_adapter_fault.is_valid());
    EXPECT_EQ(adapter::k_catalog.k_adapter_fault.component, "journal_signal_adapter");
}

TEST(SignalAdapterTest, StartsAndStopsCleanlyViaFactory) {
    auto instance = adapter::signal_adapter::create();
    ASSERT_TRUE(instance.has_value());
    EXPECT_TRUE(instance->is_running());

    instance->stop();
    EXPECT_FALSE(instance->is_running());

    // Idempotent stop
    instance->stop();
    EXPECT_FALSE(instance->is_running());
}

TEST(SignalAdapterTest, RejectsInvalidCandidatePolicy) {
    jrn::policy invalid_policy{};
    invalid_policy.congestion_threshold_pct = 101U;

    auto instance = adapter::signal_adapter::create(invalid_policy);
    EXPECT_FALSE(instance.has_value());
}

TEST(SignalAdapterTest, HandlesMoveSemantics) {
    auto instance = adapter::signal_adapter::create();
    ASSERT_TRUE(instance.has_value());
    EXPECT_TRUE(instance->is_running());

    adapter::signal_adapter moved(std::move(*instance));
    EXPECT_TRUE(moved.is_running());
    EXPECT_FALSE(instance->is_running());

    moved.stop();
    EXPECT_FALSE(moved.is_running());
}

TEST(SignalAdapterTest, TogglesDebugAndDefaultPolicyOnSignals) {
    jrn::reset_default_policy();
    EXPECT_EQ(jrn::active_policy(), jrn::policy{});

    auto instance = adapter::signal_adapter::create();
    ASSERT_TRUE(instance.has_value());
    EXPECT_TRUE(instance->is_running());

    // Send SIGUSR1 to activate verbose debug policy
    ASSERT_EQ(::kill(::getpid(), SIGUSR1), 0);

    bool activated_debug = false;
    for (int i = 0; i < 40; ++i) {
        if (jrn::active_policy() == adapter::signal_adapter::default_debug_policy()) {
            activated_debug = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(activated_debug);
    EXPECT_EQ(jrn::active_policy(), adapter::signal_adapter::default_debug_policy());

    // Send SIGUSR2 to restore default flight recorder policy
    ASSERT_EQ(::kill(::getpid(), SIGUSR2), 0);

    bool restored_default = false;
    for (int i = 0; i < 40; ++i) {
        if (jrn::active_policy() == jrn::policy{}) {
            restored_default = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(restored_default);
    EXPECT_EQ(jrn::active_policy(), jrn::policy{});

    instance->stop();
    EXPECT_FALSE(instance->is_running());
}

