#include <journal/journal.hpp>
#include <journal_sink.hpp>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <stop_token>
#include <string_view>
#include <thread>
#include <type_traits>

namespace tle::journal {

    namespace {
        inline constexpr std::uint8_t k_max_severity_index = 7U;
        inline constexpr std::size_t k_max_u8_len = 255U;
        inline constexpr std::size_t k_max_u16_len = 65535U;
        inline constexpr std::size_t k_component_cache_size = 16U;
        inline constexpr std::uintptr_t k_pointer_align_shift = 4U;
        inline constexpr std::size_t k_backtrace_warmup_frames = 4U;
        inline constexpr std::size_t k_cache_line_bytes = 64U;
        inline constexpr std::size_t k_slot_header_section_bytes = 40U;

        inline constexpr std::chrono::milliseconds k_calibration_sleep{2};
        inline constexpr std::uint64_t k_refine_interval_ns = 500'000'000ULL;
        inline constexpr unsigned k_short_idle_limit = 256U;
        inline constexpr std::chrono::microseconds k_short_idle_sleep{50};
        inline constexpr std::chrono::milliseconds k_long_idle_sleep{1};
        inline constexpr unsigned k_consumer_batch_limit = 64U;
        inline constexpr std::chrono::milliseconds k_slot_wait_timeout{20};
        inline constexpr std::chrono::microseconds k_flush_poll_interval{20};

        // ---------------------------------------------------------------------------------------------
        // Slot layout of the preallocated ring.
        // ---------------------------------------------------------------------------------------------

        // Common producer fields written on every log call. Colocated right after the 8-byte slot stamp so
        // the stamp, base header, and short message text fit inside a single 64-byte cache line.
        struct slot_base_header {
            std::uint64_t monotonic_ns{0U};
            std::uint64_t file{0U};
            std::uint64_t function{0U};
            std::uint32_t line{0U};
            std::uint32_t thread_id{0U};
            std::uint16_t text_len{0U};
            std::uint16_t binary_len{0U};
            std::uint8_t level{0U};
            std::uint8_t has_entry{0U};
            std::uint16_t padding{0U};
        };

        // Cold catalog fields written only when has_entry != 0 on boundary exception paths.
        struct slot_catalog_header {
            std::uint64_t label{0U};
            std::uint64_t message_id{0U};
            std::uint64_t entry_component{0U};
            std::uint64_t entry_summary{0U};
            std::uint16_t summary_len{0U};
            std::uint8_t label_len{0U};
            std::uint8_t message_id_len{0U};
            std::uint8_t component_len{0U};
            std::uint8_t pad0{0U};
            std::uint16_t pad1{0U};
        };

        inline constexpr std::size_t k_base_header_words = sizeof(slot_base_header) / sizeof(std::uint64_t);
        inline constexpr std::size_t k_catalog_header_words = sizeof(slot_catalog_header) / sizeof(std::uint64_t);
        inline constexpr std::size_t k_text_words = k_max_message_bytes / sizeof(std::uint64_t);
        inline constexpr std::size_t k_binary_words = k_max_binary_bytes / sizeof(std::uint64_t);
        inline constexpr std::size_t k_slot_stride_words = 208U;
        inline constexpr std::uint64_t k_ring_mask = k_ring_capacity - 1U;

        static_assert(sizeof(slot_base_header) == k_slot_header_section_bytes);
        static_assert(sizeof(slot_catalog_header) == k_slot_header_section_bytes);
        static_assert(
            k_base_header_words + k_catalog_header_words + k_text_words + k_binary_words <=
            k_slot_stride_words
        );
        static_assert(std::is_trivially_copyable_v<slot_base_header>);
        static_assert(std::is_trivially_copyable_v<slot_catalog_header>);
        static_assert((k_slot_stride_words * sizeof(std::uint64_t)) % k_cache_line_bytes == 0U);

        inline constexpr std::uint32_t k_state_idle = 0U;
        inline constexpr std::uint32_t k_state_starting = 1U;
        inline constexpr std::uint32_t k_state_running = 2U;
        inline constexpr std::uint32_t k_state_stopped = 3U;

        // Shared ring state. Plain integer words are only touched through std::atomic_ref so the protocol
        // is data race free while the whole structure stays constant initialized in zeroed storage.
        struct ring_state {
            // Written by every producer. Kept alone on its cache line.
            alignas(k_cache_line_bytes) std::atomic<std::uint64_t> head{0U};
            // Read by every producer and written rarely.
            alignas(k_cache_line_bytes) std::atomic<std::uint32_t> state{k_state_idle};
            std::atomic<std::uint64_t> history_floor{0U};
            // Written by the consumer for every record.
            alignas(k_cache_line_bytes) std::atomic<std::uint64_t> consumed{0U};
            std::atomic<std::uint64_t> emitted{0U};
            std::atomic<std::uint64_t> lost{0U};
            alignas(k_cache_line_bytes) std::array<std::uint64_t, k_ring_capacity> stamps{};
            alignas(k_cache_line_bytes) std::array<std::uint64_t, k_ring_capacity * k_slot_stride_words> words{};
        };

        struct spin_lock {
            std::atomic_flag flag;

            void lock() noexcept {
                while (flag.test_and_set(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }

            void unlock() noexcept {
                flag.clear(std::memory_order_release);
            }
        };

        struct consumer_context {
            std::uint64_t cursor{0U};
            std::uint64_t seen_generation{0U};
            policy active_policy{};
            bool active_has_overrides{false};
            std::array<std::uint64_t, k_severity_count> level_counters{};
            std::array<std::uint64_t, k_policy_override_count> override_counters{};
            std::array<std::uint64_t, k_ring_capacity> emitted_at{};
        };

        [[nodiscard]] auto now_ns() noexcept -> std::uint64_t {
            auto const since_epoch = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count());
        }

        // Producer timestamp. On x86_64 this is the raw time stamp counter, which costs a few nanoseconds
        // against roughly 25 for the vDSO clock. The consumer converts it to CLOCK_MONOTONIC nanoseconds.
        [[nodiscard]] inline auto raw_timestamp() noexcept -> std::uint64_t {
#ifdef __x86_64__
            return __builtin_ia32_rdtsc();
#else
            return now_ns();
#endif
        }

        // Converts raw producer timestamps. The mapping is calibrated against CLOCK_MONOTONIC when the
        // consumer starts and refined over a growing baseline while it runs.
        struct timestamp_converter {
            std::uint64_t base_raw{0U};
            std::uint64_t base_ns{0U};
            double ns_per_tick{1.0};
            std::uint64_t last_refine_ns{0U};

            void calibrate() noexcept {
#ifdef __x86_64__
                base_raw = raw_timestamp();
                base_ns = now_ns();
                std::this_thread::sleep_for(k_calibration_sleep);
                refine();
#else
                base_ns = 0U;
#endif
            }

            void refine() noexcept {
#ifdef __x86_64__
                std::uint64_t const raw = raw_timestamp();
                std::uint64_t const current_ns = now_ns();
                if (raw > base_raw && current_ns > base_ns) {
                    ns_per_tick = static_cast<double>(current_ns - base_ns) / static_cast<double>(raw - base_raw);
                }
                last_refine_ns = current_ns;
#endif
            }

            [[nodiscard]] auto to_ns(std::uint64_t raw) const noexcept -> std::uint64_t {
#ifdef __x86_64__
                if (raw < base_raw) {
                    return base_ns - static_cast<std::uint64_t>(static_cast<double>(base_raw - raw) * ns_per_tick);
                }
                return base_ns + static_cast<std::uint64_t>(static_cast<double>(raw - base_raw) * ns_per_tick);
#else
                return raw;
#endif
            }
        };

        struct runtime_state {
            ring_state ring{};
            spin_lock policy_lock{};
            policy current_policy{};
            std::atomic<std::uint64_t> policy_generation{0U};
            spin_lock sink_lock{};
            detail::record_sink sink{};
            timestamp_converter timestamps{};
            consumer_context consumer{};
        };

        [[nodiscard]] inline auto state() noexcept -> runtime_state& {
            static constinit runtime_state instance{};
            return instance;
        }

        void store_word(std::uint64_t& word, std::uint64_t value) noexcept {
            std::atomic_ref<std::uint64_t>(word).store(value, std::memory_order_release);
        }

        [[nodiscard]] auto load_word(std::uint64_t& word) noexcept -> std::uint64_t {
            return std::atomic_ref<std::uint64_t>(word).load(std::memory_order_acquire);
        }

        void store_bytes(std::span<std::uint64_t> destination, std::span<const std::byte> source) noexcept {
            std::size_t const full_words = source.size() / sizeof(std::uint64_t);
            for (auto const [idx, dest_word] : std::views::enumerate(destination.first(full_words))) {
                std::uint64_t word = 0U;
                auto const chunk = source.subspan(static_cast<std::size_t>(idx) * sizeof(std::uint64_t), sizeof(std::uint64_t));
                std::memcpy(&word, chunk.data(), sizeof(word));
                store_word(dest_word, word);
            }
            std::size_t const tail = source.size() % sizeof(std::uint64_t);
            if (tail != 0U) {
                std::uint64_t word = 0U;
                auto const chunk = source.subspan(full_words * sizeof(std::uint64_t), tail);
                std::memcpy(&word, chunk.data(), tail);
                store_word(destination.subspan(full_words).front(), word);
            }
        }

        void load_bytes(std::span<std::byte> destination, std::span<std::uint64_t> source, std::size_t length) noexcept {
            std::size_t const clamped = std::min(length, destination.size());
            std::size_t const full_words = clamped / sizeof(std::uint64_t);
            for (auto const [idx, src_word] : std::views::enumerate(source.first(full_words))) {
                std::uint64_t const word = load_word(src_word);
                auto const chunk = destination.subspan(static_cast<std::size_t>(idx) * sizeof(std::uint64_t), sizeof(std::uint64_t));
                std::memcpy(chunk.data(), &word, sizeof(word));
            }
            std::size_t const tail = clamped % sizeof(std::uint64_t);
            if (tail != 0U) {
                std::uint64_t const word = load_word(source.subspan(full_words).front());
                auto const chunk = destination.subspan(full_words * sizeof(std::uint64_t), tail);
                std::memcpy(chunk.data(), &word, tail);
            }
        }

        [[nodiscard]] auto slot_span(std::uint64_t index) noexcept -> std::span<std::uint64_t> {
            auto const slot = static_cast<std::size_t>(index & k_ring_mask);
            return std::span<std::uint64_t>(state().ring.words).subspan(
                slot * k_slot_stride_words,
                k_slot_stride_words
            );
        }

        [[nodiscard]] auto slot_stamp(std::uint64_t index) noexcept -> std::atomic_ref<std::uint64_t> {
            auto const slot = static_cast<std::size_t>(index & k_ring_mask);
            return std::atomic_ref<std::uint64_t>(state().ring.stamps.at(slot));
        }

        [[nodiscard]] auto current_thread_id() noexcept -> std::uint32_t {
            thread_local std::uint32_t cached = 0U;
            if (cached == 0U) {
                cached = static_cast<std::uint32_t>(::syscall(SYS_gettid));
            }
            return cached;
        }

        template <typename T>
        [[nodiscard]] auto pointer_word(const T* pointer) noexcept -> std::uint64_t {
            return static_cast<std::uint64_t>(std::bit_cast<std::uintptr_t>(pointer));
        }

        [[nodiscard]] auto string_pointer_word(std::string_view text) noexcept -> std::uint64_t {
            return static_cast<std::uint64_t>(std::bit_cast<std::uintptr_t>(std::to_address(text.begin())));
        }

        template <typename T>
        [[nodiscard]] auto word_pointer(std::uint64_t word) noexcept -> const T* {
            return std::bit_cast<const T*>(static_cast<std::uintptr_t>(word));
        }

        // Everything a producer hands to the ring.
        struct submission {
            severity level{severity::info};
            std::string_view text;
            std::span<const std::byte> binary;
            const char* file{nullptr};
            const char* function{nullptr};
            std::uint32_t line{0U};
            const catalog_entry* entry{nullptr};
            std::string_view label;
        };

        struct decoded_header {
            slot_base_header base{};
            slot_catalog_header catalog{};
        };

        // A record copied out of the ring by the consumer.
        struct decoded_record {
            decoded_header header{};
            std::array<char, k_max_message_bytes> text{};
            std::array<std::byte, k_max_binary_bytes> binary{};
        };

        struct consumer_buffers {
            decoded_record current{};
            decoded_record lookbehind{};
        };

        enum class read_state : std::uint8_t { ready, not_ready, lost };

        // Reads and validates only the slot header. Allows the consumer to evaluate stride and step over
        // suppressed debug records without copying payload bytes out of the ring.
        [[nodiscard]] auto read_slot_header(std::uint64_t index, decoded_header& out) noexcept -> read_state {
            auto const stamp = slot_stamp(index);
            std::uint64_t const expected = (index * 2U) + 2U;

            std::uint64_t const before = stamp.load(std::memory_order_acquire);
            if (before < expected) {
                return read_state::not_ready;
            }
            if (before > expected) {
                return read_state::lost;
            }

            auto const words = slot_span(index);
            std::array<std::uint64_t, k_base_header_words> base_words{};
            std::ranges::transform(words.first(k_base_header_words), base_words.begin(), load_word);
            out.base = std::bit_cast<slot_base_header>(base_words);
            out.base.text_len = static_cast<std::uint16_t>(
                std::min<std::size_t>(out.base.text_len, k_max_message_bytes));
            out.base.binary_len = static_cast<std::uint16_t>(
                std::min<std::size_t>(out.base.binary_len, k_max_binary_bytes));

            if (out.base.has_entry != 0U) {
                std::array<std::uint64_t, k_catalog_header_words> cat_words{};
                std::ranges::transform(
                    words.subspan(k_base_header_words, k_catalog_header_words),
                    cat_words.begin(),
                    load_word
                );
                out.catalog = std::bit_cast<slot_catalog_header>(cat_words);
            } else {
                out.catalog = {};
            }

            std::uint64_t const after = stamp.load(std::memory_order_acquire);
            return after == expected ? read_state::ready : read_state::lost;
        }

        // Copies the variable-length message and binary payload for a slot whose header was already read,
        // re-validating the stamp afterwards to ensure no producer lapped the slot during the copy.
        [[nodiscard]] auto read_slot_payload(std::uint64_t index, decoded_record& out) noexcept -> bool {
            auto const stamp = slot_stamp(index);
            std::uint64_t const expected = (index * 2U) + 2U;
            auto const words = slot_span(index);

            std::size_t const payload_offset = k_base_header_words +
                (out.header.base.has_entry != 0U ? k_catalog_header_words : 0U);
            std::size_t const text_len = out.header.base.text_len;
            std::size_t const binary_len = out.header.base.binary_len;
            std::size_t const text_words = (text_len + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t);

            if (text_len != 0U) {
                load_bytes(
                    std::as_writable_bytes(std::span<char>{out.text}),
                    words.subspan(payload_offset, k_text_words),
                    text_len
                );
            }
            if (binary_len != 0U) {
                load_bytes(
                    std::span<std::byte>{out.binary},
                    words.subspan(payload_offset + text_words, k_binary_words),
                    binary_len
                );
            }

            std::uint64_t const after = stamp.load(std::memory_order_acquire);
            return after == expected;
        }

        // Source file pointers are static, so the component of a file is remembered by pointer.
        [[nodiscard]] auto component_of_file(const char* file) noexcept -> std::string_view {
            struct cache_entry {
                const char* file{nullptr};
                std::string_view component;
            };
            static constinit std::array<cache_entry, k_component_cache_size> cache{};
            auto& entry = cache.at((std::bit_cast<std::uintptr_t>(file) >> k_pointer_align_shift) % cache.size());
            if (entry.file != file || file == nullptr) {
                entry.file = file;
                entry.component = extract_component(file != nullptr ? file : "");
            }
            return entry.component;
        }

        [[nodiscard]] auto header_component(const decoded_header& header) noexcept -> std::string_view {
            if (header.base.has_entry != 0U) {
                return {
                    word_pointer<char>(header.catalog.entry_component),
                    header.catalog.component_len
                };
            }
            return component_of_file(word_pointer<char>(header.base.file));
        }

        // Builds the consumer facing view of a validated record. The string pointers were written by the
        // producer from static storage and are only dereferenced after the stamp validation succeeded.
        struct record_view {
            detail::emitted_record record{};
            catalog_entry entry{};
        };

        void make_view(const decoded_record& decoded, std::uint64_t index, bool lookbehind, record_view& view) noexcept {
            slot_base_header const& base = decoded.header.base;
            slot_catalog_header const& cat = decoded.header.catalog;
            auto& record = view.record;
            record.level = is_valid_severity(static_cast<severity>(base.level))
                ? static_cast<severity>(base.level)
                : severity::error;
            record.text = std::string_view(decoded.text.data(), base.text_len);
            record.binary = std::span<const std::byte>(decoded.binary.data(), base.binary_len);
            record.file = word_pointer<char>(base.file);
            record.function = word_pointer<char>(base.function);
            record.line = base.line;
            record.thread_id = base.thread_id;
            record.sequence = index;
            record.monotonic_ns = state().timestamps.to_ns(base.monotonic_ns);
            record.lookbehind = lookbehind;
            if (base.has_entry != 0U) {
                view.entry = catalog_entry{
                    .message_id = std::string_view(word_pointer<char>(cat.message_id), cat.message_id_len),
                    .component = std::string_view(word_pointer<char>(cat.entry_component), cat.component_len),
                    .summary = std::string_view(word_pointer<char>(cat.entry_summary), cat.summary_len)
                };
                record.entry = &view.entry;
                record.label = std::string_view(word_pointer<char>(cat.label), cat.label_len);
                record.component = view.entry.component;
            } else {
                record.entry = nullptr;
                record.label = {};
                record.component = component_of_file(record.file);
            }
        }

        void deliver(const detail::emitted_record& record) noexcept {
            auto& ctx = state();
            std::scoped_lock const guard(ctx.sink_lock);
            if (ctx.sink.callback != nullptr) {
                ctx.sink.callback(ctx.sink.context, record);
            } else {
                detail::send_to_journald(record);
            }
        }

        // ---------------------------------------------------------------------------------------------
        // Consumer side policy engine.
        // ---------------------------------------------------------------------------------------------

        struct slot_position {
            std::uint64_t index{0U};
            std::uint64_t head{0U};
        };

        [[nodiscard]] auto snapshot_policy() noexcept -> policy {
            auto& ctx = state();
            std::scoped_lock const guard(ctx.policy_lock);
            return ctx.current_policy;
        }

        void refresh_consumer_policy() noexcept {
            auto& ctx = state();
            std::uint64_t const generation = ctx.policy_generation.load(std::memory_order_acquire);
            if (generation == ctx.consumer.seen_generation) {
                return;
            }
            {
                std::scoped_lock const guard(ctx.policy_lock);
                ctx.consumer.active_policy = ctx.current_policy;
                ctx.consumer.seen_generation = ctx.policy_generation.load(std::memory_order_relaxed);
            }
            ctx.consumer.active_has_overrides = std::ranges::any_of(
                ctx.consumer.active_policy.overrides,
                [](const component_override& entry) noexcept { return !component_name(entry).empty(); }
            );
            // A new policy restarts every stride so that every Nth counts from the activation.
            ctx.consumer.level_counters.fill(0U);
            ctx.consumer.override_counters.fill(0U);
        }

        void emit_record(const decoded_record& decoded, std::uint64_t index, bool lookbehind) noexcept {
            auto& ctx = state();
            record_view view{};
            make_view(decoded, index, lookbehind, view);
            deliver(view.record);
            ctx.consumer.emitted_at.at(static_cast<std::size_t>(index & k_ring_mask)) = index + 1U;
            ctx.ring.emitted.fetch_add(1U, std::memory_order_relaxed);
        }

        // Decides whether a record below the lookbehind trigger leaves the ring.
        [[nodiscard]] auto passes_stride(
            const decoded_header& header,
            slot_position pos
        ) noexcept -> bool {
            auto& cons = state().consumer;
            policy const& active = cons.active_policy;
            auto const level_index = static_cast<std::size_t>(
                std::min<std::uint8_t>(header.base.level, k_max_severity_index));
            std::uint32_t stride = active.stride_by_level.at(level_index);
            std::uint64_t* counter = &cons.level_counters.at(level_index);

            if (level_index == static_cast<std::size_t>(severity::debug) && cons.active_has_overrides) {
                std::string_view const comp = header_component(header);
                for (auto const [slot, entry] : std::views::enumerate(active.overrides)) {
                    auto const name = component_name(entry);
                    if (!name.empty() && name == comp) {
                        stride = entry.debug_stride;
                        counter = &cons.override_counters.at(static_cast<std::size_t>(slot));
                        break;
                    }
                }
            }

            bool const thinned_level = level_index == static_cast<std::size_t>(severity::notice) ||
                                       level_index == static_cast<std::size_t>(severity::info);
            if (thinned_level && stride != 0U) {
                std::uint64_t const backlog = pos.head > pos.index ? pos.head - pos.index : 0U;
                std::uint64_t const backlog_pct = std::min<std::uint64_t>(
                    k_max_percentage,
                    (backlog * k_max_percentage) / k_ring_capacity
                );
                if (backlog_pct > active.congestion_threshold_pct) {
                    stride = active.congestion_info_stride == 0U
                        ? 0U
                        : std::max<std::uint32_t>(stride, active.congestion_info_stride);
                }
            }

            if (stride == 0U) {
                return false;
            }
            std::uint64_t const seen = (*counter)++;
            return (seen % stride) == 0U;
        }

        // Re-emits the records skipped by the policy ahead of a trigger record in natural order.
        void flush_lookbehind(slot_position pos, decoded_record& earlier) noexcept {
            auto& ctx = state();
            policy const& active = ctx.consumer.active_policy;
            std::uint64_t const window = std::min<std::uint64_t>(active.lookbehind_slots, pos.index);
            std::uint64_t first = pos.index - window;
            first = std::max(first, ctx.ring.history_floor.load(std::memory_order_acquire));
            if (pos.head > k_ring_capacity) {
                first = std::max(first, pos.head - k_ring_capacity);
            }
            for (std::uint64_t const past : std::views::iota(first, pos.index)) {
                if (ctx.consumer.emitted_at.at(static_cast<std::size_t>(past & k_ring_mask)) == past + 1U) {
                    continue;
                }
                if (read_slot_header(past, earlier.header) == read_state::ready &&
                    read_slot_payload(past, earlier)) {
                    emit_record(earlier, past, true);
                }
            }
        }

        void process_record(
            consumer_buffers& buffers,
            slot_position pos
        ) noexcept {
            auto& ctx = state();
            auto const level_index = std::min<std::uint8_t>(buffers.current.header.base.level, k_max_severity_index);
            if (level_index <= static_cast<std::uint8_t>(ctx.consumer.active_policy.lookbehind_trigger)) {
                flush_lookbehind(pos, buffers.lookbehind);
                if (read_slot_payload(pos.index, buffers.current)) {
                    emit_record(buffers.current, pos.index, false);
                } else {
                    ctx.ring.lost.fetch_add(1U, std::memory_order_relaxed);
                }
                return;
            }
            if (passes_stride(buffers.current.header, pos)) {
                if (read_slot_payload(pos.index, buffers.current)) {
                    emit_record(buffers.current, pos.index, false);
                } else {
                    ctx.ring.lost.fetch_add(1U, std::memory_order_relaxed);
                }
            }
        }

        void advance_cursor(std::uint64_t value) noexcept {
            auto& ctx = state();
            ctx.consumer.cursor = value;
            ctx.ring.consumed.store(value, std::memory_order_release);
        }

        [[nodiscard]] auto wait_for_slot_header(std::uint64_t index, decoded_header& out) noexcept -> read_state {
            std::chrono::steady_clock::time_point started{};
            while (true) {
                read_state const result = read_slot_header(index, out);
                if (result != read_state::not_ready) {
                    return result;
                }
                if (started == std::chrono::steady_clock::time_point{}) {
                    started = std::chrono::steady_clock::now();
                }
                if (std::chrono::steady_clock::now() - started > k_slot_wait_timeout) {
                    return read_state::lost;
                }
                std::this_thread::yield();
            }
        }

        void drain_pending_batch(
            std::uint64_t head,
            consumer_buffers& buffers
        ) noexcept {
            auto& ctx = state();
            for ([[maybe_unused]] unsigned const batch : std::views::iota(0U, k_consumer_batch_limit)) {
                if (ctx.consumer.cursor >= head) {
                    break;
                }
                std::uint64_t index = ctx.consumer.cursor;
                if (head - index > k_ring_capacity) {
                    std::uint64_t const skipped = (head - k_ring_capacity) - index;
                    ctx.ring.lost.fetch_add(skipped, std::memory_order_relaxed);
                    index += skipped;
                    advance_cursor(index);
                }

                read_state const slot_state = wait_for_slot_header(index, buffers.current.header);
                if (slot_state == read_state::ready) {
                    process_record(buffers, slot_position{.index = index, .head = head});
                } else {
                    ctx.ring.lost.fetch_add(1U, std::memory_order_relaxed);
                }
                advance_cursor(index + 1U);
            }
        }

        void consumer_main(const std::stop_token& stop_token) noexcept {
            auto& ctx = state();
            ctx.timestamps.calibrate();
            unsigned idle_rounds = 0U;
            auto buffers = std::make_unique_for_overwrite<consumer_buffers>();
            while (true) {
                std::uint64_t const head = ctx.ring.head.load(std::memory_order_acquire);
                if (ctx.consumer.cursor == head) {
                    if (stop_token.stop_requested()) {
                        if (ctx.ring.head.load(std::memory_order_acquire) == ctx.consumer.cursor) {
                            return;
                        }
                        continue;
                    }
                    ++idle_rounds;
                    // Polling the head line while producers are active would bounce it between cores on
                    // every claim, so a caught up consumer sleeps instead of spinning.
                    if (idle_rounds < k_short_idle_limit) {
                        std::this_thread::sleep_for(k_short_idle_sleep);
                    } else {
                        std::this_thread::sleep_for(k_long_idle_sleep);
                    }
                    continue;
                }
                idle_rounds = 0U;

                if (now_ns() - ctx.timestamps.last_refine_ns > k_refine_interval_ns) {
                    ctx.timestamps.refine();
                }
                refresh_consumer_policy();
                drain_pending_batch(head, *buffers);
            }
        }

        struct consumer_runner {
            std::jthread worker;

            consumer_runner()
                : worker([](const std::stop_token& stop_token) noexcept { consumer_main(stop_token); }) {
                auto const handle = worker.native_handle();
                (void)::pthread_setname_np(handle, "journal-consume");
                sched_param const param{.sched_priority = 0};
                (void)::pthread_setschedparam(handle, SCHED_BATCH, &param);
            }

            ~consumer_runner() {
                if (worker.joinable()) {
                    worker.request_stop();
                    worker.join();
                }
                state().ring.state.store(k_state_stopped, std::memory_order_release);
            }

            consumer_runner(const consumer_runner&) = delete;
            auto operator=(const consumer_runner&) -> consumer_runner& = delete;
            consumer_runner(consumer_runner&&) = delete;
            auto operator=(consumer_runner&&) -> consumer_runner& = delete;
        };

        // The first call warms up the unwinder so later stack captures stay allocation free.
        void warm_up_backtrace() noexcept {
            std::array<std::uintptr_t, k_backtrace_warmup_frames> scratch{};
            (void)detail::capture_current_stacktrace(scratch);
        }

        [[nodiscard]] auto start_consumer() noexcept -> bool {
            auto& ctx = state();
            std::uint32_t expected = k_state_idle;
            if (!ctx.ring.state.compare_exchange_strong(expected, k_state_starting, std::memory_order_acq_rel)) {
                while (ctx.ring.state.load(std::memory_order_acquire) == k_state_starting) {
                    std::this_thread::yield();
                }
                return ctx.ring.state.load(std::memory_order_acquire) == k_state_running;
            }
            warm_up_backtrace();
            try {
                static consumer_runner const runner{};
                (void)runner;
            } catch (...) {
                ctx.ring.state.store(k_state_stopped, std::memory_order_release);
                return false;
            }
            ctx.ring.state.store(k_state_running, std::memory_order_release);
            return true;
        }

        [[nodiscard]] auto ensure_consumer() noexcept -> bool {
            std::uint32_t const current = state().ring.state.load(std::memory_order_acquire);
            if (current == k_state_running) {
                return true;
            }
            if (current == k_state_stopped) {
                return false;
            }
            return start_consumer();
        }

        // Used when the consumer thread is unavailable. The record leaves synchronously without policy.
        void deliver_direct(const submission& entry) noexcept {
            detail::emitted_record record{};
            catalog_entry copy{};
            record.level = entry.level;
            record.text = entry.text;
            record.binary = entry.binary.first(std::min(entry.binary.size(), k_max_binary_bytes));
            record.file = entry.file;
            record.function = entry.function;
            record.line = entry.line;
            record.thread_id = current_thread_id();
            record.monotonic_ns = now_ns();
            if (entry.entry != nullptr) {
                copy = *entry.entry;
                record.entry = &copy;
                record.label = entry.label;
                record.component = copy.component;
            } else {
                record.component = extract_component(entry.file != nullptr ? entry.file : "");
            }
            deliver(record);
        }

        // Producer hot path. One atomic claim and plain word stores. No formatting, no heap and no syscalls
        // after the consumer thread exists.
        void submit(const submission& entry) noexcept {
            if (!ensure_consumer()) {
                deliver_direct(entry);
                return;
            }

            std::size_t const text_len = std::min(entry.text.size(), k_max_message_bytes);
            std::size_t const binary_len = std::min(entry.binary.size(), k_max_binary_bytes);

            slot_base_header base{};
            base.monotonic_ns = raw_timestamp();
            base.file = pointer_word(entry.file);
            base.function = pointer_word(entry.function);
            base.line = entry.line;
            base.thread_id = current_thread_id();
            base.text_len = static_cast<std::uint16_t>(text_len);
            base.binary_len = static_cast<std::uint16_t>(binary_len);
            base.level = static_cast<std::uint8_t>(entry.level);
            base.has_entry = entry.entry != nullptr ? 1U : 0U;

            std::uint64_t const index = state().ring.head.fetch_add(1U, std::memory_order_relaxed);
            auto const stamp = slot_stamp(index);
            stamp.store((index * 2U) + 1U, std::memory_order_relaxed);

            auto const words = slot_span(index);
            auto const base_words = std::bit_cast<std::array<std::uint64_t, k_base_header_words>>(base);
            for (auto const [idx, word] : std::views::enumerate(base_words)) {
                store_word(words.subspan(static_cast<std::size_t>(idx)).front(), word);
            }

            std::size_t payload_offset = k_base_header_words;
            if (entry.entry != nullptr) {
                slot_catalog_header cat{};
                cat.label = string_pointer_word(entry.label);
                cat.message_id = string_pointer_word(entry.entry->message_id);
                cat.entry_component = string_pointer_word(entry.entry->component);
                cat.entry_summary = string_pointer_word(entry.entry->summary);
                cat.summary_len = static_cast<std::uint16_t>(std::min(entry.entry->summary.size(), k_max_u16_len));
                cat.label_len = static_cast<std::uint8_t>(std::min(entry.label.size(), k_max_u8_len));
                cat.message_id_len = static_cast<std::uint8_t>(std::min(entry.entry->message_id.size(), k_max_u8_len));
                cat.component_len = static_cast<std::uint8_t>(std::min(entry.entry->component.size(), k_max_u8_len));

                auto const cat_words = std::bit_cast<std::array<std::uint64_t, k_catalog_header_words>>(cat);
                for (auto const [idx, word] : std::views::enumerate(cat_words)) {
                    store_word(words.subspan(payload_offset + static_cast<std::size_t>(idx)).front(), word);
                }
                payload_offset += k_catalog_header_words;
            }

            std::size_t const text_words = (text_len + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t);
            if (text_len != 0U) {
                auto const text_bytes = std::as_bytes(std::span<const char>{entry.text}.first(text_len));
                store_bytes(words.subspan(payload_offset, k_text_words), text_bytes);
            }
            if (binary_len != 0U) {
                store_bytes(words.subspan(payload_offset + text_words, k_binary_words), entry.binary.first(binary_len));
            }
            stamp.store((index * 2U) + 2U, std::memory_order_release);
        }
    } // namespace

    namespace detail {

        void emit_log_record(const log_record& record) noexcept {
            submit(submission{
                .level = record.level,
                .text = record.text,
                .binary = record.binary,
                .file = record.location.file_name(),
                .function = record.location.function_name(),
                .line = record.location.line(),
                .entry = nullptr,
                .label = {}
            });
        }

        void submit_catalog_record(
            severity level,
            const catalog_entry& entry,
            std::string_view label,
            std::string_view detail_text,
            std::span<const std::byte> binary,
            std::source_location location
        ) noexcept {
            submit(submission{
                .level = level,
                .text = detail_text,
                .binary = binary,
                .file = location.file_name(),
                .function = location.function_name(),
                .line = location.line(),
                .entry = &entry,
                .label = label
            });
        }

        void set_record_sink(record_sink sink) noexcept {
            auto& ctx = state();
            std::scoped_lock const guard(ctx.sink_lock);
            ctx.sink = sink;
        }

        void reset_record_sink() noexcept {
            set_record_sink(record_sink{});
        }

        void drop_history() noexcept {
            auto& ctx = state();
            (void)flush(k_default_flush_timeout);
            ctx.ring.history_floor.store(ctx.ring.head.load(std::memory_order_acquire), std::memory_order_release);
        }

    } // namespace detail

    auto activate_policy(const policy& candidate) noexcept -> bool {
        if (!is_valid_policy(candidate)) {
            return false;
        }
        auto& ctx = state();
        std::scoped_lock const guard(ctx.policy_lock);
        ctx.current_policy = candidate;
        ctx.policy_generation.fetch_add(1U, std::memory_order_release);
        return true;
    }

    void reset_default_policy() noexcept {
        auto& ctx = state();
        std::scoped_lock const guard(ctx.policy_lock);
        ctx.current_policy = policy{};
        ctx.policy_generation.fetch_add(1U, std::memory_order_release);
    }

    auto active_policy() noexcept -> policy {
        return snapshot_policy();
    }

    auto flush(std::chrono::milliseconds timeout) noexcept -> bool {
        auto& ctx = state();
        if (ctx.ring.state.load(std::memory_order_acquire) != k_state_running) {
            return true;
        }
        std::uint64_t const target = ctx.ring.head.load(std::memory_order_acquire);
        auto const deadline = std::chrono::steady_clock::now() + timeout;
        while (ctx.ring.consumed.load(std::memory_order_acquire) < target) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(k_flush_poll_interval);
        }
        return true;
    }

    auto statistics_snapshot() noexcept -> statistics {
        auto& ctx = state();
        return statistics{
            .submitted = ctx.ring.head.load(std::memory_order_acquire),
            .emitted = ctx.ring.emitted.load(std::memory_order_acquire),
            .lost = ctx.ring.lost.load(std::memory_order_acquire)
        };
    }

} // namespace tle::journal
