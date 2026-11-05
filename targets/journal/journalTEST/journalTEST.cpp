#include <gtest/gtest.h>
#include <journal/journal.hpp>
#include <link.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <source_location>
#include <span>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {
    namespace jrn = tle::journal;

    struct single_component_catalog {
        jrn::catalog_entry k_default_failure{
            .message_id = "6d3f8912a0c54e37b9241f08c3e2a201",
            .component = "single_comp",
            .summary = "Single component boundary failure"
        };
    };
    inline constexpr single_component_catalog k_single_catalog{};

    struct multi_component_catalog {
        jrn::catalog_entry k_default_failure{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e101",
            .component = "multi_comp",
            .summary = "Default boundary failure"
        };
        jrn::catalog_entry k_corrupt_header{
            .message_id = "4c8a1023e5b94d719f012a34c8b5e102",
            .component = "multi_comp",
            .summary = "Corrupt binary header detected inside helper"
        };
    };
    inline constexpr multi_component_catalog k_multi_catalog{};

    struct empty_catalog {};

    struct nine_entry_catalog {
        jrn::catalog_entry e1{}, e2{}, e3{}, e4{}, e5{}, e6{}, e7{}, e8{}, e9{};
    };

    struct sample_binary_snapshot {
        std::uint64_t magic{0xCAFEBABE12345678ULL};
        std::uint32_t sequence{42U};
        std::uint32_t flags{0x07U};
    };

    [[nodiscard]] auto single_catalog_boundary_function(int mode) noexcept -> bool {
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
            return jrn::catch_boundary_exception(k_single_catalog);
        }
    }

    [[nodiscard]] auto multi_catalog_boundary_function(int mode) noexcept -> bool {
        try {
            if (mode == 1) {
                throw jrn::catalog_exception(k_multi_catalog.k_corrupt_header, "header checksum mismatch");
            }
            if (mode == 2) {
                throw std::out_of_range("index out of bounds");
            }
            return true;
        } catch (...) {
            return jrn::catch_boundary_exception(k_multi_catalog.k_default_failure);
        }
    }

    [[gnu::noinline]] auto helper_throwing_catalog_exception_for_symbolization() -> void {
        throw jrn::catalog_exception(k_multi_catalog.k_corrupt_header, "symbolizer showcase failure");
    }

    [[nodiscard]] auto find_addr2line_executable() -> std::string {
        for (auto const prefix : {"/usr/bin/", "/usr/local/bin/"}) {
            for (auto const name : {"addr2line", "llvm-addr2line"}) {
                auto path = std::string(prefix) + name;
                if (std::filesystem::exists(path)) {
                    return path;
                }
            }
        }
        return {};
    }

    [[nodiscard]] auto get_main_executable_base_address() noexcept -> std::uintptr_t {
        std::uintptr_t base_addr = 0;
        dl_iterate_phdr([](struct dl_phdr_info* info, std::size_t, void* data) -> int {
            auto* out_base = static_cast<std::uintptr_t*>(data);
            if (info->dlpi_name == nullptr || info->dlpi_name[0] == '\0') {
                *out_base = info->dlpi_addr;
                return 1;
            }
            return 0;
        }, &base_addr);
        return base_addr;
    }
} // namespace

// Compile-time verification of all 8 Syslog severities
static_assert(jrn::to_syslog_priority(jrn::severity::emergency) == 0);
static_assert(jrn::to_syslog_priority(jrn::severity::alert) == 1);
static_assert(jrn::to_syslog_priority(jrn::severity::critical) == 2);
static_assert(jrn::to_syslog_priority(jrn::severity::error) == 3);
static_assert(jrn::to_syslog_priority(jrn::severity::warning) == 4);
static_assert(jrn::to_syslog_priority(jrn::severity::notice) == 5);
static_assert(jrn::to_syslog_priority(jrn::severity::info) == 6);
static_assert(jrn::to_syslog_priority(jrn::severity::debug) == 7);
static_assert(jrn::is_valid_severity(jrn::severity::emergency));
static_assert(jrn::is_valid_severity(jrn::severity::debug));

// Compile-time verification of catalog counts and single-entry catalog concepts
static_assert(jrn::catalog_entry_count_v<empty_catalog> == 0U);
static_assert(jrn::catalog_entry_count_v<single_component_catalog> == 1U);
static_assert(jrn::catalog_entry_count_v<multi_component_catalog> == 2U);
static_assert(!jrn::single_entry_catalog<empty_catalog>);
static_assert(jrn::single_entry_catalog<jrn::catalog>);
static_assert(jrn::single_entry_catalog<single_component_catalog>);
static_assert(!jrn::single_entry_catalog<multi_component_catalog>);
static_assert(!jrn::is_valid_catalog(nine_entry_catalog{}));
static_assert(jrn::is_valid_catalog(jrn::k_catalog));
static_assert(jrn::is_valid_catalog(k_single_catalog));
static_assert(jrn::is_valid_catalog(k_multi_catalog));
static_assert(jrn::extract_single_catalog_entry(k_single_catalog).component == "single_comp");

// Compile-time verification of variadic strong log pack constraints
static_assert(jrn::valid_log_pack<jrn::severity, jrn::message>);
static_assert(jrn::valid_log_pack<jrn::message, jrn::severity>);
static_assert(jrn::valid_log_pack<jrn::severity, jrn::binary_payload>);
static_assert(jrn::valid_log_pack<jrn::binary_payload, jrn::severity>);
static_assert(jrn::valid_log_pack<jrn::severity, jrn::message, jrn::binary_payload>);
static_assert(jrn::valid_log_pack<jrn::binary_payload, jrn::message, jrn::severity>);
static_assert(!jrn::valid_log_pack<jrn::severity>);
static_assert(!jrn::valid_log_pack<jrn::message>);
static_assert(!jrn::valid_log_pack<jrn::severity, jrn::severity, jrn::message>);
static_assert(!jrn::valid_log_pack<jrn::severity, jrn::message, jrn::message>);
static_assert(!jrn::valid_log_pack<jrn::catalog_entry, jrn::message>);

// Compile-time verification of order-independent make_log_record folding
static_assert(
    jrn::make_log_record(
        std::source_location::current(),
        jrn::message{"compile time check"},
        jrn::severity::warning
    ).level == jrn::severity::warning
);
static_assert(
    jrn::make_log_record(
        std::source_location::current(),
        jrn::message{"compile time check"},
        jrn::severity::warning
    ).text == "compile time check"
);

// Compile-time verification of constexpr catalog_exception literal type
static_assert(
    jrn::catalog_exception(k_multi_catalog.k_corrupt_header, "bad magic").entry().message_id ==
    "4c8a1023e5b94d719f012a34c8b5e102"
);
static_assert(
    jrn::catalog_exception(k_multi_catalog.k_corrupt_header, "bad magic").reason() == "bad magic"
);
static_assert(
    std::string_view(jrn::catalog_exception(k_multi_catalog.k_corrupt_header, "bad magic").what()) == "bad magic"
);
static_assert(
    std::string_view(jrn::catalog_exception(k_multi_catalog.k_corrupt_header).what()) ==
    "Corrupt binary header detected inside helper"
);
static_assert(
    jrn::catalog_exception(k_multi_catalog.k_corrupt_header, "bad magic").trace_addresses().empty()
);

// Compile-time verification of extract_component
static_assert(jrn::extract_component("/home/user/project/targets/app/app.cpp") == "app");
static_assert(jrn::extract_component("/path/to/target2/my_comp/my_comp.cpp", "target2/") == "my_comp");
static_assert(jrn::extract_component("") == "unknown");

TEST(JournalTest, ValidatesCatalogMessageIdFormat) {
    EXPECT_TRUE(jrn::is_valid_message_id(jrn::k_catalog.k_unhandled_boundary_exception.message_id));
    EXPECT_FALSE(jrn::is_valid_message_id("too_short"));
    EXPECT_FALSE(jrn::is_valid_message_id("7A1D4F90E3C84A21B5809E41D2A7C001"));
}

TEST(JournalTest, SingleEntryCatalogBoundaryDeducesEntryAndCapturesStacktrace) {
    EXPECT_TRUE(single_catalog_boundary_function(0));
    EXPECT_FALSE(single_catalog_boundary_function(1));
    EXPECT_FALSE(single_catalog_boundary_function(2));
    EXPECT_FALSE(single_catalog_boundary_function(3));
}

TEST(JournalTest, MultiEntryCatalogBoundarySelectsThrownCatalogExceptionAndPreUnwindStacktrace) {
    jrn::catalog_exception const runtime_ex(k_multi_catalog.k_corrupt_header, "corrupt header frame");
    EXPECT_EQ(runtime_ex.entry().message_id, k_multi_catalog.k_corrupt_header.message_id);
    EXPECT_EQ(runtime_ex.reason(), "corrupt header frame");
    EXPECT_FALSE(runtime_ex.trace_addresses().empty());
    EXPECT_FALSE(runtime_ex.trace_bytes().empty());

    EXPECT_TRUE(multi_catalog_boundary_function(0));
    EXPECT_FALSE(multi_catalog_boundary_function(1));
    EXPECT_FALSE(multi_catalog_boundary_function(2));
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

TEST(JournalTest, EmitsTextAndBinaryLogsAcrossAllSyslogSeveritiesInAnyArgumentOrder) {
    sample_binary_snapshot const snapshot{};
    auto const payload = jrn::as_binary_payload(snapshot);

    std::array<std::uintptr_t, jrn::k_max_stacktrace_frames> frames{};
    std::size_t const frame_count = jrn::detail::capture_current_stacktrace(frames);
    EXPECT_GT(frame_count, 0U);
    auto const trace_payload = jrn::as_binary_payload(
        std::span<const std::uintptr_t>{frames.data(), frame_count}
    );

    jrn::log(jrn::severity::emergency, jrn::message{"emergency level diagnostic"});
    jrn::log(jrn::severity::alert, jrn::message{"alert level diagnostic"});
    jrn::log(jrn::severity::critical, jrn::message{"critical level diagnostic"});
    jrn::log(jrn::severity::error, jrn::message{"error level diagnostic"});
    jrn::log(jrn::severity::warning, jrn::message{"warning level diagnostic"});
    jrn::log(jrn::severity::notice, jrn::message{"notice level diagnostic"});
    jrn::log(jrn::severity::info, jrn::message{"info level diagnostic"});
    jrn::log(jrn::severity::debug, jrn::message{"debug level diagnostic"});

    // Order-independent binary and combined logs
    jrn::log(jrn::severity::debug, payload);
    jrn::log(payload, jrn::severity::debug);
    jrn::log(jrn::severity::error, jrn::message{"snapshot + trace"}, trace_payload);
    jrn::log(trace_payload, jrn::message{"reordered attributes"}, jrn::severity::warning);
}

TEST(JournalTest, SymbolizesBinaryStacktraceAddressesUsingAddr2line) {
    auto const addr2line_tool = find_addr2line_executable();
    if (addr2line_tool.empty()) {
        GTEST_SKIP() << "addr2line or llvm-addr2line not found on host system";
    }

    std::error_code ec;
    auto const exe_path = std::filesystem::canonical("/proc/self/exe", ec);
    if (ec) {
        GTEST_SKIP() << "Cannot resolve canonical path of /proc/self/exe";
    }

    std::vector<std::byte> raw_bytes;
    try {
        helper_throwing_catalog_exception_for_symbolization();
    } catch (const jrn::catalog_exception& ex) {
        auto const span = ex.trace_bytes();
        raw_bytes.assign(span.begin(), span.end());
    }

    ASSERT_FALSE(raw_bytes.empty());
    ASSERT_EQ(raw_bytes.size() % sizeof(std::uintptr_t), 0U);

    std::span<const std::uintptr_t> const frames{
        reinterpret_cast<const std::uintptr_t*>(raw_bytes.data()),
        raw_bytes.size() / sizeof(std::uintptr_t)
    };
    ASSERT_GE(frames.size(), 2U);

    std::uintptr_t const base_address = get_main_executable_base_address();
    std::string symbolized_trace;

    for (std::uintptr_t const frame_addr : frames) {
        std::uintptr_t const offset = (base_address != 0U) ? (frame_addr - base_address) : frame_addr;

        std::stringstream cmd;
        cmd << addr2line_tool << " -e " << exe_path << " -f -C 0x" << std::hex << offset;

        FILE* pipe = popen(cmd.str().c_str(), "r");
        if (pipe == nullptr) {
            continue;
        }

        std::array<char, 256U> line_buf{};
        while (fgets(line_buf.data(), static_cast<int>(line_buf.size()), pipe) != nullptr) {
            symbolized_trace += line_buf.data();
        }
        pclose(pipe);
    }

    EXPECT_NE(symbolized_trace.find("helper_throwing_catalog_exception_for_symbolization"), std::string::npos);
    EXPECT_NE(symbolized_trace.find("journalTEST.cpp"), std::string::npos);
}


namespace {
    using namespace std::chrono_literals;

    struct captured_record {
        jrn::severity level{jrn::severity::info};
        std::string component;
        std::string text;
        std::vector<std::byte> binary;
        bool has_entry{false};
        std::string message_id;
        std::string label;
        bool lookbehind{false};
        std::uint64_t sequence{0U};
        std::uint32_t thread_id{0U};
        std::uint64_t monotonic_ns{0U};
    };

    // Redirects the consumer into memory. Records are read only after a successful flush, which
    // synchronizes with the consumer thread.
    class capture_sink {
    public:
        capture_sink() {
            jrn::detail::set_record_sink({&capture_sink::on_record, this});
        }
        capture_sink(const capture_sink&) = delete;
        capture_sink& operator=(const capture_sink&) = delete;
        ~capture_sink() {
            (void)jrn::flush(5000ms);
            jrn::detail::reset_record_sink();
        }

        void block_next_record() { gate_.store(true); }
        void release() { gate_.store(false); }
        [[nodiscard]] auto blocked() const -> bool { return entered_.load(); }

        std::vector<captured_record> records;
        int consumer_scheduler{-1};
        std::string consumer_thread_name;

    private:
        static void on_record(void* context, const jrn::detail::emitted_record& record) noexcept {
            auto* self = static_cast<capture_sink*>(context);
            self->entered_.store(self->gate_.load());
            while (self->gate_.load()) {
                std::this_thread::sleep_for(100us);
            }
            self->entered_.store(false);
            if (self->consumer_scheduler < 0) {
                self->consumer_scheduler = ::sched_getscheduler(0);
                std::array<char, 16U> name{};
                if (::pthread_getname_np(::pthread_self(), name.data(), name.size()) == 0) {
                    self->consumer_thread_name = name.data();
                }
            }
            captured_record item;
            item.level = record.level;
            item.component = std::string(record.component);
            item.text = std::string(record.text);
            item.binary.assign(record.binary.begin(), record.binary.end());
            item.has_entry = record.entry != nullptr;
            if (record.entry != nullptr) {
                item.message_id = std::string(record.entry->message_id);
            }
            item.label = std::string(record.label);
            item.lookbehind = record.lookbehind;
            item.sequence = record.sequence;
            item.thread_id = record.thread_id;
            item.monotonic_ns = record.monotonic_ns;
            self->records.push_back(std::move(item));
        }

        std::atomic<bool> gate_{false};
        std::atomic<bool> entered_{false};
    };

    class PolicyTest : public ::testing::Test {
    protected:
        void SetUp() override {
            jrn::reset_default_policy();
            sink = std::make_unique<capture_sink>();
            jrn::detail::drop_history();
            sink->records.clear();
        }

        void TearDown() override {
            sink.reset();
            jrn::reset_default_policy();
        }

        static void log_text(jrn::severity level, std::string_view text) {
            jrn::log(level, jrn::message{text});
        }

        [[nodiscard]] auto drained() -> std::vector<captured_record>& {
            EXPECT_TRUE(jrn::flush(5000ms));
            return sink->records;
        }

        [[nodiscard]] static auto texts(const std::vector<captured_record>& records) -> std::vector<std::string> {
            std::vector<std::string> result(records.size());
            std::ranges::transform(records, result.begin(), &captured_record::text);
            return result;
        }

        std::unique_ptr<capture_sink> sink;
    };

    constexpr auto k_override_journal = jrn::make_component_override("journal", 1U);
    static_assert(k_override_journal.has_value());
    static_assert(jrn::component_name(*k_override_journal) == "journal");
    static_assert(!jrn::make_component_override("", 1U).has_value());
    static_assert(!jrn::make_component_override("this_component_name_is_far_too_long_to_fit", 1U).has_value());
    static_assert(jrn::is_valid_policy(jrn::policy{}));
    static_assert(!jrn::is_valid_policy(jrn::policy{.lookbehind_slots = static_cast<std::uint16_t>(jrn::k_ring_capacity + 1U)}));
}

TEST(PolicyDefaultsTest, DefaultPolicyIsTheFlightRecorderPolicy) {
    jrn::policy const defaults{};
    for (std::uint32_t const stride : std::span{defaults.stride_by_level}.first(7U)) {
        EXPECT_EQ(stride, 1U);
    }
    EXPECT_EQ(defaults.stride_by_level.at(7U), 0U);
    EXPECT_EQ(defaults.congestion_threshold_pct, 50U);
    EXPECT_EQ(defaults.congestion_info_stride, 16U);
    EXPECT_EQ(defaults.lookbehind_trigger, jrn::severity::error);
    EXPECT_EQ(defaults.lookbehind_slots, 128U);
    for (auto const& entry : defaults.overrides) {
        EXPECT_TRUE(jrn::component_name(entry).empty());
    }
}

TEST(PolicyDefaultsTest, ValidationRejectsOutOfRangeValues) {
    EXPECT_TRUE(jrn::is_valid_policy(jrn::policy{}));

    jrn::policy bad_stride{};
    bad_stride.stride_by_level.at(6U) = jrn::k_max_stride + 1U;
    EXPECT_FALSE(jrn::is_valid_policy(bad_stride));

    jrn::policy bad_percent{};
    bad_percent.congestion_threshold_pct = 101U;
    EXPECT_FALSE(jrn::is_valid_policy(bad_percent));

    jrn::policy bad_congestion{};
    bad_congestion.congestion_info_stride = jrn::k_max_stride + 1U;
    EXPECT_FALSE(jrn::is_valid_policy(bad_congestion));

    jrn::policy bad_trigger{};
    bad_trigger.lookbehind_trigger = static_cast<jrn::severity>(9U);
    EXPECT_FALSE(jrn::is_valid_policy(bad_trigger));

    jrn::policy bad_slots{};
    bad_slots.lookbehind_slots = static_cast<std::uint16_t>(jrn::k_ring_capacity + 1U);
    EXPECT_FALSE(jrn::is_valid_policy(bad_slots));

    jrn::policy bad_name{};
    bad_name.overrides.at(0U).component = {'a', '/', 'b'};
    EXPECT_FALSE(jrn::is_valid_policy(bad_name));

    jrn::policy unterminated{};
    unterminated.overrides.at(1U).component.fill('x');
    EXPECT_FALSE(jrn::is_valid_policy(unterminated));

    jrn::policy bad_override_stride{};
    bad_override_stride.overrides.at(2U) = *jrn::make_component_override("geometry", jrn::k_max_stride + 1U);
    EXPECT_FALSE(jrn::is_valid_policy(bad_override_stride));
}

TEST(PolicyDefaultsTest, ActivationRoundTripsAndResetRestoresDefault) {
    jrn::reset_default_policy();
    EXPECT_EQ(jrn::active_policy(), jrn::policy{});

    jrn::policy custom{};
    custom.stride_by_level.at(7U) = 4U;
    custom.lookbehind_slots = 7U;
    custom.lookbehind_trigger = jrn::severity::critical;
    custom.overrides.at(0U) = *jrn::make_component_override("geometry", 1U);
    EXPECT_TRUE(jrn::activate_policy(custom));
    EXPECT_EQ(jrn::active_policy(), custom);

    jrn::reset_default_policy();
    EXPECT_EQ(jrn::active_policy(), jrn::policy{});
}

TEST(PolicyDefaultsTest, InvalidActivationKeepsPreviousPolicy) {
    jrn::reset_default_policy();
    jrn::policy good{};
    good.lookbehind_slots = 33U;
    ASSERT_TRUE(jrn::activate_policy(good));

    jrn::policy bad{};
    bad.congestion_threshold_pct = 200U;
    EXPECT_FALSE(jrn::activate_policy(bad));
    EXPECT_EQ(jrn::active_policy(), good);
    jrn::reset_default_policy();
}

TEST(PolicyDefaultsTest, ConcurrentActivationsNeverExposeATornPolicy) {
    jrn::reset_default_policy();
    std::atomic<bool> torn{false};

    {
        std::vector<std::jthread> writers;
        for (std::uint32_t const id : std::views::iota(1U, 5U)) {
            writers.emplace_back([id](const std::stop_token& stop_token) {
                jrn::policy candidate{};
                candidate.stride_by_level.fill(id);
                candidate.lookbehind_slots = static_cast<std::uint16_t>(id);
                candidate.congestion_info_stride = id;
                while (!stop_token.stop_requested()) {
                    (void)jrn::activate_policy(candidate);
                }
            });
        }
        std::jthread const reader([&]() {
            for ([[maybe_unused]] int const round : std::views::iota(0, 20000)) {
                jrn::policy const snapshot = jrn::active_policy();
                std::uint32_t const marker = snapshot.stride_by_level.at(0U);
                bool const consistent = snapshot.stride_by_level.at(7U) == marker &&
                                        snapshot.lookbehind_slots == marker &&
                                        snapshot.congestion_info_stride == marker;
                if (!consistent && snapshot != jrn::policy{}) {
                    torn.store(true);
                }
            }
        });
    }
    EXPECT_FALSE(torn.load());
    jrn::reset_default_policy();
}

TEST_F(PolicyTest, DebugRecordsStayInTheRingByDefault) {
    log_text(jrn::severity::debug, "quiet one");
    log_text(jrn::severity::debug, "quiet two");
    log_text(jrn::severity::info, "audible");
    auto& records = drained();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().text, "audible");
    EXPECT_FALSE(records.front().lookbehind);
}

TEST_F(PolicyTest, EveryNthStrideEmitsEveryNthRecordStartingAtActivation) {
    jrn::policy custom{};
    custom.stride_by_level.at(6U) = 3U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    for (int const idx : std::views::iota(0, 9)) {
        log_text(jrn::severity::info, "tick " + std::to_string(idx));
    }
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"tick 0", "tick 3", "tick 6"}));
}

TEST_F(PolicyTest, StrideZeroKeepsALevelRingOnly) {
    jrn::policy custom{};
    custom.stride_by_level.at(6U) = 0U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    log_text(jrn::severity::info, "silent info");
    log_text(jrn::severity::warning, "loud warning");
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"loud warning"}));
}

TEST_F(PolicyTest, DebugStrideOneEmitsEverything) {
    jrn::policy custom{};
    custom.stride_by_level.at(7U) = 1U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    log_text(jrn::severity::debug, "d1");
    log_text(jrn::severity::debug, "d2");
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"d1", "d2"}));
}

TEST_F(PolicyTest, ErrorFlushesSkippedHistoryAheadOfTheFault) {
    log_text(jrn::severity::debug, "breadcrumb a");
    log_text(jrn::severity::info, "visible info");
    log_text(jrn::severity::debug, "breadcrumb b");
    log_text(jrn::severity::error, "the fault");
    auto& records = drained();
    ASSERT_EQ(records.size(), 4U);
    EXPECT_EQ(records.at(0U).text, "visible info");
    EXPECT_FALSE(records.at(0U).lookbehind);
    EXPECT_EQ(records.at(1U).text, "breadcrumb a");
    EXPECT_TRUE(records.at(1U).lookbehind);
    EXPECT_EQ(records.at(2U).text, "breadcrumb b");
    EXPECT_TRUE(records.at(2U).lookbehind);
    EXPECT_EQ(records.at(3U).text, "the fault");
    EXPECT_FALSE(records.at(3U).lookbehind);
    EXPECT_LT(records.at(1U).sequence, records.at(2U).sequence);
    EXPECT_LT(records.at(2U).sequence, records.at(3U).sequence);
}

TEST_F(PolicyTest, LookbehindNeverRepeatsAlreadyEmittedRecords) {
    log_text(jrn::severity::info, "once only");
    log_text(jrn::severity::critical, "fault");
    auto& records = drained();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records.at(0U).text, "once only");
    EXPECT_EQ(records.at(1U).text, "fault");
}

TEST_F(PolicyTest, LookbehindWindowIsBoundedBySlots) {
    jrn::policy custom{};
    custom.lookbehind_slots = 2U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    for (int const idx : std::views::iota(0, 5)) {
        log_text(jrn::severity::debug, "d" + std::to_string(idx));
    }
    log_text(jrn::severity::error, "fault");
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"d3", "d4", "fault"}));
}

TEST_F(PolicyTest, RaisedTriggerKeepsErrorsQuiet) {
    jrn::policy custom{};
    custom.lookbehind_trigger = jrn::severity::critical;
    ASSERT_TRUE(jrn::activate_policy(custom));
    log_text(jrn::severity::debug, "crumb");
    log_text(jrn::severity::error, "plain error");
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"plain error"}));
    log_text(jrn::severity::critical, "real fault");
    auto& records = drained();
    ASSERT_EQ(records.size(), 3U);
    EXPECT_EQ(records.at(1U).text, "crumb");
    EXPECT_TRUE(records.at(1U).lookbehind);
    EXPECT_EQ(records.at(2U).text, "real fault");
}

TEST_F(PolicyTest, DropHistoryHidesEarlierRecordsFromLookbehind) {
    log_text(jrn::severity::debug, "ancient");
    jrn::detail::drop_history();
    sink->records.clear();
    log_text(jrn::severity::debug, "recent");
    log_text(jrn::severity::error, "fault");
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"recent", "fault"}));
}

TEST_F(PolicyTest, ComponentOverrideMicroscopesOneComponentAndRevertsOnReset) {
    jrn::policy other_only{};
    other_only.overrides.at(0U) = *jrn::make_component_override("geometry", 1U);
    ASSERT_TRUE(jrn::activate_policy(other_only));
    log_text(jrn::severity::debug, "not this component");
    EXPECT_TRUE(drained().empty());

    jrn::policy microscope{};
    microscope.overrides.at(1U) = *jrn::make_component_override("journal", 1U);
    ASSERT_TRUE(jrn::activate_policy(microscope));
    log_text(jrn::severity::debug, "streamed");
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"streamed"}));
    sink->records.clear();

    jrn::reset_default_policy();
    log_text(jrn::severity::debug, "quiet again");
    EXPECT_TRUE(drained().empty());
}

TEST_F(PolicyTest, ComponentOverrideStrideThinsDebugTraffic) {
    jrn::policy microscope{};
    microscope.overrides.at(0U) = *jrn::make_component_override("journal", 4U);
    ASSERT_TRUE(jrn::activate_policy(microscope));
    for (int const idx : std::views::iota(0, 8)) {
        log_text(jrn::severity::debug, "beat " + std::to_string(idx));
    }
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"beat 0", "beat 4"}));
}

TEST_F(PolicyTest, CongestionBackoffThinsInfoWhileTheConsumerIsBehind) {
    constexpr int k_total = 1500;
    sink->block_next_record();
    log_text(jrn::severity::info, "first blocks the consumer");
    while (!sink->blocked()) {
        std::this_thread::sleep_for(100us);
    }
    for ([[maybe_unused]] int const idx : std::views::iota(0, k_total)) {
        log_text(jrn::severity::info, "burst");
    }
    sink->release();
    auto& records = drained();
    EXPECT_GT(records.size(), 1U);
    EXPECT_LT(records.size(), static_cast<std::size_t>(k_total) - 100U);
}

TEST_F(PolicyTest, CongestionBackoffCanBeDisabledByThreshold) {
    jrn::policy custom{};
    custom.congestion_threshold_pct = 100U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    sink->block_next_record();
    log_text(jrn::severity::info, "first blocks the consumer");
    while (!sink->blocked()) {
        std::this_thread::sleep_for(100us);
    }
    for ([[maybe_unused]] int const idx : std::views::iota(0, 1500)) {
        log_text(jrn::severity::info, "burst");
    }
    sink->release();
    EXPECT_EQ(drained().size(), 1501U);
}

TEST_F(PolicyTest, ProducersNeverBlockWhenTheRingOverflows) {
    sink->block_next_record();
    log_text(jrn::severity::info, "first blocks the consumer");
    while (!sink->blocked()) {
        std::this_thread::sleep_for(100us);
    }
    auto const before = jrn::statistics_snapshot();
    std::size_t const flood = jrn::k_ring_capacity * 3U;
    for ([[maybe_unused]] std::size_t const idx : std::views::iota(std::size_t{0U}, flood)) {
        log_text(jrn::severity::warning, "flood");
    }
    sink->release();
    EXPECT_TRUE(jrn::flush(10000ms));
    auto const after = jrn::statistics_snapshot();
    EXPECT_EQ(after.submitted - before.submitted, flood);
    EXPECT_GT(after.lost - before.lost, 0U);
    EXPECT_LT(sink->records.size(), flood);
}

TEST_F(PolicyTest, RecordsCarrySequenceTimestampThreadAndComponent) {
    jrn::policy custom{};
    custom.stride_by_level.at(7U) = 1U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    log_text(jrn::severity::info, "first");
    log_text(jrn::severity::debug, "second");
    auto& records = drained();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records.at(0U).component, "journal");
    EXPECT_LT(records.at(0U).sequence, records.at(1U).sequence);
    EXPECT_LE(records.at(0U).monotonic_ns, records.at(1U).monotonic_ns);
    EXPECT_GT(records.at(0U).monotonic_ns, 0U);
    EXPECT_EQ(records.at(0U).thread_id, static_cast<std::uint32_t>(::syscall(SYS_gettid)));
    EXPECT_EQ(sink->consumer_scheduler, SCHED_BATCH);
    EXPECT_EQ(sink->consumer_thread_name, "journal-consume");
}

TEST_F(PolicyTest, SequenceOrderIsTotalAcrossProducerThreads) {
    jrn::policy custom{};
    custom.stride_by_level.at(7U) = 1U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    {
        std::vector<std::jthread> producers;
        for ([[maybe_unused]] int const id : std::views::iota(0, 4)) {
            producers.emplace_back([]() {
                for ([[maybe_unused]] int const idx : std::views::iota(0, 200)) {
                    log_text(jrn::severity::debug, "threaded");
                }
            });
        }
    }
    auto& records = drained();
    ASSERT_EQ(records.size(), 800U);
    for (std::size_t const idx : std::views::iota(std::size_t{1U}, records.size())) {
        EXPECT_LT(records.at(idx - 1U).sequence, records.at(idx).sequence);
    }
}

TEST_F(PolicyTest, TextAndBinaryPayloadsRoundTripThroughTheRing) {
    sample_binary_snapshot const snapshot{};
    jrn::policy custom{};
    custom.stride_by_level.at(7U) = 1U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    jrn::log(jrn::severity::debug, jrn::message{"with payload"}, jrn::as_binary_payload(snapshot));
    jrn::log(jrn::as_binary_payload(snapshot), jrn::severity::debug);
    auto& records = drained();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records.at(0U).text, "with payload");
    ASSERT_EQ(records.at(0U).binary.size(), sizeof(snapshot));
    EXPECT_EQ(std::memcmp(records.at(0U).binary.data(), &snapshot, sizeof(snapshot)), 0);
    EXPECT_TRUE(records.at(1U).text.empty());
    EXPECT_EQ(records.at(1U).binary.size(), sizeof(snapshot));
}

TEST_F(PolicyTest, OversizedPayloadsAreTruncatedToTheSlotLimits) {
    jrn::policy custom{};
    custom.stride_by_level.at(7U) = 1U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    std::string const long_text(jrn::k_max_message_bytes + 200U, 'x');
    std::vector<std::byte> const long_binary(jrn::k_max_binary_bytes + 300U, std::byte{0x5A});
    jrn::log(jrn::severity::debug, jrn::message{long_text}, jrn::binary_payload{long_binary});
    auto& records = drained();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().text.size(), jrn::k_max_message_bytes);
    EXPECT_EQ(records.front().binary.size(), jrn::k_max_binary_bytes);
}

TEST_F(PolicyTest, OddLengthPayloadsKeepTheirExactBytes) {
    jrn::policy custom{};
    custom.stride_by_level.at(7U) = 1U;
    ASSERT_TRUE(jrn::activate_policy(custom));
    std::array<std::byte, 13U> odd{};
    for (auto const [idx, byte] : std::views::enumerate(odd)) {
        byte = static_cast<std::byte>(idx + 1);
    }
    jrn::log(jrn::severity::debug, jrn::message{"odd length 13"}, jrn::binary_payload{odd});
    auto& records = drained();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().text, "odd length 13");
    ASSERT_EQ(records.front().binary.size(), odd.size());
    EXPECT_EQ(std::memcmp(records.front().binary.data(), odd.data(), odd.size()), 0);
}

TEST_F(PolicyTest, BoundaryExceptionTravelsThroughTheRingWithLookbehind) {
    log_text(jrn::severity::debug, "before the fault");
    EXPECT_FALSE(single_catalog_boundary_function(1));
    auto& records = drained();
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records.at(0U).text, "before the fault");
    EXPECT_TRUE(records.at(0U).lookbehind);

    auto const& fault = records.at(1U);
    EXPECT_TRUE(fault.has_entry);
    EXPECT_EQ(fault.level, jrn::severity::error);
    EXPECT_EQ(fault.message_id, k_single_catalog.k_default_failure.message_id);
    EXPECT_EQ(fault.component, "single_comp");
    EXPECT_EQ(fault.label, "std::invalid_argument: ");
    EXPECT_EQ(fault.text, "invalid mode argument");
    EXPECT_FALSE(fault.binary.empty());
    EXPECT_EQ(fault.binary.size() % sizeof(std::uintptr_t), 0U);
}

TEST_F(PolicyTest, UnknownExceptionIsRecordedAsCritical) {
    EXPECT_FALSE(single_catalog_boundary_function(3));
    auto& records = drained();
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(records.front().level, jrn::severity::critical);
    EXPECT_EQ(records.front().text, "non-standard unknown exception");
}

TEST_F(PolicyTest, FlushReturnsImmediatelyWhenNothingIsPending) {
    EXPECT_TRUE(jrn::flush(1000ms));
    EXPECT_TRUE(jrn::flush(0ms));
}

TEST_F(PolicyTest, StatisticsCountSubmittedAndEmittedRecords) {
    auto const before = jrn::statistics_snapshot();
    log_text(jrn::severity::info, "counted");
    log_text(jrn::severity::debug, "ring only");
    EXPECT_TRUE(jrn::flush(5000ms));
    auto const after = jrn::statistics_snapshot();
    EXPECT_EQ(after.submitted - before.submitted, 2U);
    EXPECT_EQ(after.emitted - before.emitted, 1U);
}

TEST_F(PolicyTest, ExternalControllerCanDriveThePolicyFromPlainKeyValuePairs) {
    // Mirrors what a Varlink, D-Bus or REST component would do with decoded request fields.
    std::map<std::string, std::string> const request{
        {"debug_stride", "1"},
        {"info_stride", "2"},
        {"lookbehind_slots", "16"},
        {"override_component", "geometry"},
        {"override_stride", "5"},
    };
    jrn::policy requested{};
    requested.stride_by_level.at(7U) = static_cast<std::uint32_t>(std::stoul(request.at("debug_stride")));
    requested.stride_by_level.at(6U) = static_cast<std::uint32_t>(std::stoul(request.at("info_stride")));
    requested.lookbehind_slots = static_cast<std::uint16_t>(std::stoul(request.at("lookbehind_slots")));
    auto const component = jrn::make_component_override(
        request.at("override_component"),
        static_cast<std::uint32_t>(std::stoul(request.at("override_stride")))
    );
    ASSERT_TRUE(component.has_value());
    requested.overrides.at(0U) = *component;

    ASSERT_TRUE(jrn::is_valid_policy(requested));
    ASSERT_TRUE(jrn::activate_policy(requested));
    EXPECT_EQ(jrn::active_policy(), requested);

    log_text(jrn::severity::info, "i0");
    log_text(jrn::severity::info, "i1");
    log_text(jrn::severity::debug, "d0");
    EXPECT_EQ(texts(drained()), (std::vector<std::string>{"i0", "d0"}));
}
