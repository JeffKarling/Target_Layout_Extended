#define SD_JOURNAL_SUPPRESS_LOCATION 1
#include <journal/journal.hpp>
#include <execinfo.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <syslog.h>
#include <systemd/sd-journal.h>
#include <unistd.h>
#if defined(__x86_64__)
#include <x86intrin.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>

namespace target_layout_extended::journal {

    static_assert(to_syslog_priority(severity::emergency) == LOG_EMERG);
    static_assert(to_syslog_priority(severity::alert) == LOG_ALERT);
    static_assert(to_syslog_priority(severity::critical) == LOG_CRIT);
    static_assert(to_syslog_priority(severity::error) == LOG_ERR);
    static_assert(to_syslog_priority(severity::warning) == LOG_WARNING);
    static_assert(to_syslog_priority(severity::notice) == LOG_NOTICE);
    static_assert(to_syslog_priority(severity::info) == LOG_INFO);
    static_assert(to_syslog_priority(severity::debug) == LOG_DEBUG);

    namespace {
        inline constexpr std::size_t k_priority_buf_size = 16U;
        inline constexpr std::size_t k_msg_id_buf_size = 64U;
        inline constexpr std::size_t k_component_buf_size = 128U;
        inline constexpr std::size_t k_file_buf_size = 512U;
        inline constexpr std::size_t k_number_buf_size = 48U;
        inline constexpr std::size_t k_func_buf_size = 256U;
        inline constexpr std::size_t k_message_buf_size = 1024U;
        inline constexpr std::size_t k_binary_buf_size = k_max_binary_bytes + 16U;

        constexpr void append_string(
            std::span<char> buffer,
            std::size_t& offset,
            std::string_view text
        ) noexcept {
            if (offset >= buffer.size() || text.empty()) {
                return;
            }
            std::size_t const available = buffer.size() - offset;
            std::size_t const copy_len = std::min(text.size(), available);
            for (std::size_t idx = 0U; idx < copy_len; ++idx) {
                buffer[offset + idx] = text[idx];
            }
            offset += copy_len;
        }

        [[nodiscard]] constexpr auto format_priority_field(
            std::span<char> buffer,
            severity level
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, "PRIORITY=");
            auto const raw_level = static_cast<std::uint8_t>(level);
            char const digit = (raw_level <= 7U)
                ? static_cast<char>('0' + raw_level)
                : '3';
            if (offset < buffer.size()) {
                buffer[offset] = digit;
                ++offset;
            }
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_kv_field(
            std::span<char> buffer,
            std::string_view prefix,
            std::string_view value
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, prefix);
            append_string(buffer, offset, value);
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_number_field(
            std::span<char> buffer,
            std::string_view prefix,
            std::uint64_t value
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, prefix);
            auto const [ptr, ec] = std::to_chars(
                buffer.data() + offset,
                buffer.data() + buffer.size(),
                value
            );
            if (ec == std::errc{}) {
                offset = static_cast<std::size_t>(ptr - buffer.data());
            }
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_internal_message_field(
            std::span<char> buffer,
            std::string_view component,
            std::string_view text
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, "MESSAGE=[");
            append_string(buffer, offset, component);
            append_string(buffer, offset, "] ");
            append_string(buffer, offset, text.empty() ? "binary diagnostic event" : text);
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_catalog_message_field(
            std::span<char> buffer,
            const catalog_entry& entry,
            std::string_view prefix_label,
            std::string_view detail
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, "MESSAGE=[");
            append_string(buffer, offset, entry.component);
            append_string(buffer, offset, "] ");
            append_string(buffer, offset, entry.summary);
            append_string(buffer, offset, " (");
            append_string(buffer, offset, prefix_label);
            append_string(buffer, offset, detail);
            append_string(buffer, offset, ")");
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_binary_field(
            std::span<std::byte> buffer,
            std::span<const std::byte> payload
        ) noexcept -> std::span<std::byte> {
            constexpr std::string_view prefix = "RAW_PAYLOAD=";
            std::size_t offset = 0U;
            std::size_t const prefix_len = std::min(prefix.size(), buffer.size());
            for (std::size_t idx = 0U; idx < prefix_len; ++idx) {
                buffer[idx] = static_cast<std::byte>(prefix[idx]);
            }
            offset += prefix_len;

            std::size_t const available = buffer.size() - offset;
            std::size_t const copy_len = std::min(payload.size(), available);
            for (std::size_t idx = 0U; idx < copy_len; ++idx) {
                buffer[offset + idx] = payload[idx];
            }
            offset += copy_len;
            return buffer.first(offset);
        }

        [[nodiscard]] consteval auto verify_constexpr_formatters() noexcept -> bool {
            std::array<char, 32U> prio_buf{};
            auto const prio = format_priority_field(prio_buf, severity::warning);
            if (std::string_view(prio.data(), prio.size()) != "PRIORITY=4") {
                return false;
            }

            std::array<char, 32U> line_buf{};
            auto const line = format_number_field(line_buf, "CODE_LINE=", 128U);
            if (std::string_view(line.data(), line.size()) != "CODE_LINE=128") {
                return false;
            }

            std::array<char, 48U> seq_buf{};
            auto const seq = format_number_field(seq_buf, "LOG_SEQ=", 1234567890123ULL);
            if (std::string_view(seq.data(), seq.size()) != "LOG_SEQ=1234567890123") {
                return false;
            }

            std::array<char, 64U> msg_buf{};
            auto const msg = format_internal_message_field(msg_buf, "journal", "ok");
            if (std::string_view(msg.data(), msg.size()) != "MESSAGE=[journal] ok") {
                return false;
            }

            std::array<std::byte, 32U> bin_buf{};
            std::array<std::byte, 2U> const sample = {std::byte{0xAA}, std::byte{0xBB}};
            auto const bin = format_binary_field(bin_buf, sample);
            return bin.size() == 14U && bin[12U] == std::byte{0xAA} && bin[13U] == std::byte{0xBB};
        }

        static_assert(verify_constexpr_formatters());

        [[nodiscard]] auto to_iovec(std::span<char> field) noexcept -> iovec {
            return iovec{
                .iov_base = field.data(),
                .iov_len = field.size()
            };
        }

        [[nodiscard]] auto to_iovec(std::span<std::byte> field) noexcept -> iovec {
            return iovec{
                .iov_base = field.data(),
                .iov_len = field.size()
            };
        }

        // Destination of the default sink. Formats one emitted record into stack buffers and sends it in a
        // single vectored call.
        void send_to_journald(const detail::emitted_record& record) noexcept {
            std::array<char, k_message_buf_size> message_buf{};
            std::array<char, k_priority_buf_size> priority_buf{};
            std::array<char, k_msg_id_buf_size> msg_id_buf{};
            std::array<char, k_component_buf_size> component_buf{};
            std::array<char, k_file_buf_size> file_buf{};
            std::array<char, k_number_buf_size> line_buf{};
            std::array<char, k_func_buf_size> func_buf{};
            std::array<char, k_number_buf_size> seq_buf{};
            std::array<char, k_number_buf_size> time_buf{};
            std::array<char, k_number_buf_size> tid_buf{};
            std::array<char, 24U> lookbehind_buf{};
            std::array<std::byte, k_binary_buf_size> binary_buf{};

            std::array<iovec, 13U> vectors{};
            std::size_t count = 0U;

            if (record.entry != nullptr) {
                vectors.at(count++) = to_iovec(format_catalog_message_field(
                    message_buf, *record.entry, record.label, record.text));
                vectors.at(count++) = to_iovec(format_priority_field(priority_buf, record.level));
                vectors.at(count++) = to_iovec(format_kv_field(msg_id_buf, "MESSAGE_ID=", record.entry->message_id));
            } else {
                vectors.at(count++) = to_iovec(format_internal_message_field(
                    message_buf, record.component, record.text));
                vectors.at(count++) = to_iovec(format_priority_field(priority_buf, record.level));
            }
            vectors.at(count++) = to_iovec(format_kv_field(component_buf, "COMPONENT=", record.component));
            vectors.at(count++) = to_iovec(format_kv_field(
                file_buf, "CODE_FILE=", record.file != nullptr ? record.file : ""));
            vectors.at(count++) = to_iovec(format_number_field(line_buf, "CODE_LINE=", record.line));
            vectors.at(count++) = to_iovec(format_kv_field(
                func_buf, "CODE_FUNC=", record.function != nullptr ? record.function : ""));
            vectors.at(count++) = to_iovec(format_number_field(seq_buf, "LOG_SEQ=", record.sequence));
            vectors.at(count++) = to_iovec(format_number_field(time_buf, "PRODUCER_MONOTONIC_NS=", record.monotonic_ns));
            vectors.at(count++) = to_iovec(format_number_field(tid_buf, "TID=", record.thread_id));
            if (record.lookbehind) {
                vectors.at(count++) = to_iovec(format_kv_field(lookbehind_buf, "LOG_LOOKBEHIND=", "1"));
            }
            if (!record.binary.empty()) {
                vectors.at(count++) = to_iovec(format_binary_field(binary_buf, record.binary));
            }

            (void)sd_journal_sendv(vectors.data(), static_cast<int>(count));
        }

        // ---------------------------------------------------------------------------------------------
        // Slot layout of the preallocated ring.
        // ---------------------------------------------------------------------------------------------

        // Everything the producer records about one event. Strings are stored as static storage pointers
        // plus lengths so the producer never formats or allocates. Payload bytes follow the header words.
        struct slot_header {
            std::uint64_t monotonic_ns;
            std::uint64_t file;
            std::uint64_t function;
            std::uint64_t label;
            std::uint64_t message_id;
            std::uint64_t entry_component;
            std::uint64_t entry_summary;
            std::uint32_t line;
            std::uint32_t thread_id;
            std::uint16_t text_len;
            std::uint16_t binary_len;
            std::uint16_t summary_len;
            std::uint8_t level;
            std::uint8_t has_entry;
            std::uint8_t label_len;
            std::uint8_t message_id_len;
            std::uint8_t component_len;
            std::uint8_t reserved[5];
        };

        inline constexpr std::size_t k_header_words = sizeof(slot_header) / sizeof(std::uint64_t);
        inline constexpr std::size_t k_text_words = k_max_message_bytes / sizeof(std::uint64_t);
        inline constexpr std::size_t k_binary_words = k_max_binary_bytes / sizeof(std::uint64_t);
        inline constexpr std::size_t k_slot_stride_words = 208U;
        inline constexpr std::uint64_t k_ring_mask = k_ring_capacity - 1U;

        static_assert(sizeof(slot_header) % sizeof(std::uint64_t) == 0U);
        static_assert(k_header_words + k_text_words + k_binary_words <= k_slot_stride_words);
        static_assert(std::is_trivially_copyable_v<slot_header>);
        static_assert((k_slot_stride_words * sizeof(std::uint64_t)) % 64U == 0U);

        inline constexpr std::uint32_t k_state_idle = 0U;
        inline constexpr std::uint32_t k_state_starting = 1U;
        inline constexpr std::uint32_t k_state_running = 2U;
        inline constexpr std::uint32_t k_state_stopped = 3U;

        // Shared ring state. Plain integer words are only touched through std::atomic_ref so the protocol
        // is data race free while the whole structure stays constant initialized in zeroed storage.
        struct ring_state {
            // Written by every producer. Kept alone on its cache line.
            alignas(64) std::atomic<std::uint64_t> head{0U};
            // Read by every producer and written rarely.
            alignas(64) std::atomic<std::uint32_t> state{k_state_idle};
            std::atomic<bool> stop_requested{false};
            std::atomic<std::uint64_t> history_floor{0U};
            // Written by the consumer for every record.
            alignas(64) std::atomic<std::uint64_t> consumed{0U};
            std::atomic<std::uint64_t> emitted{0U};
            std::atomic<std::uint64_t> lost{0U};
            alignas(64) std::array<std::uint64_t, k_ring_capacity> stamps{};
            alignas(64) std::array<std::uint64_t, k_ring_capacity * k_slot_stride_words> words{};
        };

        constinit ring_state g_ring{};

        struct spin_lock {
            std::atomic_flag flag{};

            void lock() noexcept {
                while (flag.test_and_set(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }

            void unlock() noexcept {
                flag.clear(std::memory_order_release);
            }
        };

        constinit spin_lock g_policy_lock{};
        constinit policy g_policy{};
        constinit std::atomic<std::uint64_t> g_policy_generation{0U};
        constinit spin_lock g_sink_lock{};
        constinit detail::record_sink g_sink{};
        constinit std::thread* g_consumer_thread = nullptr;

        void store_word(std::uint64_t& word, std::uint64_t value) noexcept {
            std::atomic_ref<std::uint64_t>(word).store(value, std::memory_order_release);
        }

        [[nodiscard]] auto load_word(std::uint64_t& word) noexcept -> std::uint64_t {
            return std::atomic_ref<std::uint64_t>(word).load(std::memory_order_acquire);
        }

        void store_bytes(std::span<std::uint64_t> destination, const void* source, std::size_t length) noexcept {
            auto const* bytes = static_cast<const std::byte*>(source);
            std::size_t const full_words = length / sizeof(std::uint64_t);
            for (std::size_t idx = 0U; idx < full_words; ++idx) {
                std::uint64_t word = 0U;
                std::memcpy(&word, bytes + (idx * sizeof(std::uint64_t)), sizeof(word));
                store_word(destination[idx], word);
            }
            std::size_t const tail = length % sizeof(std::uint64_t);
            if (tail != 0U) {
                std::uint64_t word = 0U;
                std::memcpy(&word, bytes + (full_words * sizeof(std::uint64_t)), tail);
                store_word(destination[full_words], word);
            }
        }

        void load_bytes(void* destination, std::span<std::uint64_t> source, std::size_t length) noexcept {
            auto* bytes = static_cast<std::byte*>(destination);
            std::size_t const word_count = (length + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t);
            for (std::size_t idx = 0U; idx < word_count; ++idx) {
                std::uint64_t const word = load_word(source[idx]);
                std::memcpy(bytes + (idx * sizeof(std::uint64_t)), &word, sizeof(word));
            }
        }

        [[nodiscard]] auto slot_span(std::uint64_t index) noexcept -> std::span<std::uint64_t> {
            std::size_t const slot = static_cast<std::size_t>(index & k_ring_mask);
            return std::span<std::uint64_t>(g_ring.words).subspan(slot * k_slot_stride_words, k_slot_stride_words);
        }

        [[nodiscard]] auto now_ns() noexcept -> std::uint64_t {
            auto const since_epoch = std::chrono::steady_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(since_epoch).count());
        }

        // Producer timestamp. On x86_64 this is the raw time stamp counter, which costs a few nanoseconds
        // against roughly 25 for the vDSO clock. The consumer converts it to CLOCK_MONOTONIC nanoseconds.
        [[nodiscard]] inline auto raw_timestamp() noexcept -> std::uint64_t {
#if defined(__x86_64__)
            return __rdtsc();
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
#if defined(__x86_64__)
                base_raw = raw_timestamp();
                base_ns = now_ns();
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
                refine();
#else
                base_ns = 0U;
#endif
            }

            void refine() noexcept {
#if defined(__x86_64__)
                std::uint64_t const raw = raw_timestamp();
                std::uint64_t const ns = now_ns();
                if (raw > base_raw && ns > base_ns) {
                    ns_per_tick = static_cast<double>(ns - base_ns) / static_cast<double>(raw - base_raw);
                }
                last_refine_ns = ns;
#endif
            }

            [[nodiscard]] auto to_ns(std::uint64_t raw) const noexcept -> std::uint64_t {
#if defined(__x86_64__)
                if (raw < base_raw) {
                    return base_ns - static_cast<std::uint64_t>(static_cast<double>(base_raw - raw) * ns_per_tick);
                }
                return base_ns + static_cast<std::uint64_t>(static_cast<double>(raw - base_raw) * ns_per_tick);
#else
                return raw;
#endif
            }
        };

        constinit timestamp_converter g_timestamps{};

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

        template <typename T>
        [[nodiscard]] auto word_pointer(std::uint64_t word) noexcept -> const T* {
            return std::bit_cast<const T*>(static_cast<std::uintptr_t>(word));
        }

        // Everything a producer hands to the ring.
        struct submission {
            severity level{severity::info};
            std::string_view text{};
            std::span<const std::byte> binary{};
            const char* file{nullptr};
            const char* function{nullptr};
            std::uint32_t line{0U};
            const catalog_entry* entry{nullptr};
            std::string_view label{};
        };

        // A record copied out of the ring by the consumer.
        struct decoded_record {
            slot_header header{};
            std::array<char, k_max_message_bytes> text{};
            std::array<std::byte, k_max_binary_bytes> binary{};
        };

        enum class read_state : std::uint8_t { ready, not_ready, lost };

        // Copies one record out of the ring and validates it against the slot stamp. A stamp of
        // 2 * index + 2 marks a fully written record and an odd stamp marks a writer in progress.
        [[nodiscard]] auto read_slot(std::uint64_t index, decoded_record& out) noexcept -> read_state {
            std::size_t const slot = static_cast<std::size_t>(index & k_ring_mask);
            std::atomic_ref<std::uint64_t> const stamp(g_ring.stamps.at(slot));
            std::uint64_t const expected = (index * 2U) + 2U;

            std::uint64_t const before = stamp.load(std::memory_order_acquire);
            if (before < expected) {
                return read_state::not_ready;
            }
            if (before > expected) {
                return read_state::lost;
            }

            auto const words = slot_span(index);
            std::array<std::uint64_t, k_header_words> header_words{};
            for (std::size_t idx = 0U; idx < k_header_words; ++idx) {
                header_words.at(idx) = load_word(words[idx]);
            }
            std::memcpy(&out.header, header_words.data(), sizeof(slot_header));
            out.header.text_len = static_cast<std::uint16_t>(
                std::min<std::size_t>(out.header.text_len, k_max_message_bytes));
            out.header.binary_len = static_cast<std::uint16_t>(
                std::min<std::size_t>(out.header.binary_len, k_max_binary_bytes));
            std::size_t const text_words = (out.header.text_len + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t);
            load_bytes(out.text.data(), words.subspan(k_header_words, k_text_words), out.header.text_len);
            load_bytes(out.binary.data(), words.subspan(k_header_words + text_words, k_binary_words), out.header.binary_len);

            std::uint64_t const after = stamp.load(std::memory_order_acquire);
            return after == expected ? read_state::ready : read_state::lost;
        }

        // Source file pointers are static, so the component of a file is remembered by pointer.
        [[nodiscard]] auto component_of_file(const char* file) noexcept -> std::string_view {
            struct cache_entry {
                const char* file{nullptr};
                std::string_view component{};
            };
            static constinit std::array<cache_entry, 16U> cache{};
            auto& entry = cache.at((std::bit_cast<std::uintptr_t>(file) >> 4U) % cache.size());
            if (entry.file != file || file == nullptr) {
                entry.file = file;
                entry.component = extract_component(file != nullptr ? file : "");
            }
            return entry.component;
        }

        // Builds the consumer facing view of a validated record. The string pointers were written by the
        // producer from static storage and are only dereferenced after the stamp validation succeeded.
        struct record_view {
            detail::emitted_record record{};
            catalog_entry entry{};
        };

        void make_view(const decoded_record& decoded, std::uint64_t index, bool lookbehind, record_view& view) noexcept {
            slot_header const& header = decoded.header;
            auto& record = view.record;
            record.level = is_valid_severity(static_cast<severity>(header.level))
                ? static_cast<severity>(header.level)
                : severity::error;
            record.text = std::string_view(decoded.text.data(), header.text_len);
            record.binary = std::span<const std::byte>(decoded.binary.data(), header.binary_len);
            record.file = word_pointer<char>(header.file);
            record.function = word_pointer<char>(header.function);
            record.line = header.line;
            record.thread_id = header.thread_id;
            record.sequence = index;
            record.monotonic_ns = g_timestamps.to_ns(header.monotonic_ns);
            record.lookbehind = lookbehind;
            if (header.has_entry != 0U) {
                view.entry = catalog_entry{
                    .message_id = std::string_view(word_pointer<char>(header.message_id), header.message_id_len),
                    .component = std::string_view(word_pointer<char>(header.entry_component), header.component_len),
                    .summary = std::string_view(word_pointer<char>(header.entry_summary), header.summary_len)
                };
                record.entry = &view.entry;
                record.label = std::string_view(word_pointer<char>(header.label), header.label_len);
                record.component = view.entry.component;
            } else {
                record.entry = nullptr;
                record.label = {};
                record.component = component_of_file(record.file);
            }
        }

        void deliver(const detail::emitted_record& record) noexcept {
            std::lock_guard<spin_lock> const guard(g_sink_lock);
            if (g_sink.callback != nullptr) {
                g_sink.callback(g_sink.context, record);
            } else {
                send_to_journald(record);
            }
        }

        // ---------------------------------------------------------------------------------------------
        // Consumer side policy engine.
        // ---------------------------------------------------------------------------------------------

        struct consumer_context {
            std::uint64_t cursor{0U};
            std::array<std::uint64_t, 8U> level_counters{};
            std::array<std::uint64_t, k_policy_override_count> override_counters{};
            std::array<std::uint64_t, k_ring_capacity> emitted_at{};
        };

        constinit consumer_context g_consumer{};

        [[nodiscard]] auto snapshot_policy() noexcept -> policy {
            std::lock_guard<spin_lock> const guard(g_policy_lock);
            return g_policy;
        }

        void emit_record(const decoded_record& decoded, std::uint64_t index, bool lookbehind) noexcept {
            record_view view{};
            make_view(decoded, index, lookbehind, view);
            deliver(view.record);
            g_consumer.emitted_at.at(static_cast<std::size_t>(index & k_ring_mask)) = index + 1U;
            g_ring.emitted.fetch_add(1U, std::memory_order_relaxed);
        }

        // Decides whether a record below the lookbehind trigger leaves the ring.
        [[nodiscard]] auto passes_stride(
            const policy& active,
            const decoded_record& decoded,
            std::uint64_t index,
            std::uint64_t head
        ) noexcept -> bool {
            auto const level_index = static_cast<std::size_t>(std::min<std::uint8_t>(decoded.header.level, 7U));
            std::uint32_t stride = active.stride_by_level.at(level_index);
            std::uint64_t* counter = &g_consumer.level_counters.at(level_index);

            bool const has_overrides = !component_name(active.overrides.front()).empty() ||
                                       !component_name(active.overrides.at(1U)).empty() ||
                                       !component_name(active.overrides.at(2U)).empty() ||
                                       !component_name(active.overrides.at(3U)).empty();
            if (level_index == static_cast<std::size_t>(severity::debug) && has_overrides) {
                record_view view{};
                make_view(decoded, index, false, view);
                for (std::size_t slot = 0U; slot < active.overrides.size(); ++slot) {
                    auto const& entry = active.overrides.at(slot);
                    auto const name = component_name(entry);
                    if (!name.empty() && name == view.record.component) {
                        stride = entry.debug_stride;
                        counter = &g_consumer.override_counters.at(slot);
                        break;
                    }
                }
            }

            bool const thinned_level = level_index == static_cast<std::size_t>(severity::notice) ||
                                       level_index == static_cast<std::size_t>(severity::info);
            if (thinned_level && stride != 0U) {
                std::uint64_t const backlog = head > index ? head - index : 0U;
                std::uint64_t const backlog_pct = std::min<std::uint64_t>(100U, (backlog * 100U) / k_ring_capacity);
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
        void flush_lookbehind(const policy& active, std::uint64_t index, std::uint64_t head) noexcept {
            std::uint64_t const window = std::min<std::uint64_t>(active.lookbehind_slots, index);
            std::uint64_t first = index - window;
            first = std::max(first, g_ring.history_floor.load(std::memory_order_acquire));
            if (head > k_ring_capacity) {
                first = std::max(first, head - k_ring_capacity);
            }
            for (std::uint64_t past = first; past < index; ++past) {
                if (g_consumer.emitted_at.at(static_cast<std::size_t>(past & k_ring_mask)) == past + 1U) {
                    continue;
                }
                decoded_record earlier{};
                if (read_slot(past, earlier) == read_state::ready) {
                    emit_record(earlier, past, true);
                }
            }
        }

        void process_record(const policy& active, const decoded_record& decoded, std::uint64_t index, std::uint64_t head) noexcept {
            auto const level_index = std::min<std::uint8_t>(decoded.header.level, 7U);
            if (level_index <= static_cast<std::uint8_t>(active.lookbehind_trigger)) {
                flush_lookbehind(active, index, head);
                emit_record(decoded, index, false);
                return;
            }
            if (passes_stride(active, decoded, index, head)) {
                emit_record(decoded, index, false);
            }
        }

        void advance_cursor(std::uint64_t value) noexcept {
            g_consumer.cursor = value;
            g_ring.consumed.store(value, std::memory_order_release);
        }

        void consumer_main() noexcept {
            ::pthread_setname_np(::pthread_self(), "journal-consume");
            g_timestamps.calibrate();
            unsigned idle_rounds = 0U;
            std::uint64_t seen_generation = 0U;
            auto decoded = std::make_unique_for_overwrite<decoded_record>();
            for (;;) {
                std::uint64_t head = g_ring.head.load(std::memory_order_acquire);
                if (g_consumer.cursor == head) {
                    if (g_ring.stop_requested.load(std::memory_order_acquire)) {
                        if (g_ring.head.load(std::memory_order_acquire) == g_consumer.cursor) {
                            return;
                        }
                        continue;
                    }
                    ++idle_rounds;
                    // Polling the head line while producers are active would bounce it between cores on
                    // every claim, so a caught up consumer sleeps instead of spinning.
                    if (idle_rounds < 256U) {
                        std::this_thread::sleep_for(std::chrono::microseconds{50});
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds{1});
                    }
                    continue;
                }
                idle_rounds = 0U;

                if (now_ns() - g_timestamps.last_refine_ns > 500'000'000U) {
                    g_timestamps.refine();
                }
                policy const active = snapshot_policy();
                std::uint64_t const generation = g_policy_generation.load(std::memory_order_acquire);
                if (generation != seen_generation) {
                    // A new policy restarts every stride so that every Nth counts from the activation.
                    g_consumer.level_counters.fill(0U);
                    g_consumer.override_counters.fill(0U);
                    seen_generation = generation;
                }
                for (unsigned batch = 0U; batch < 64U && g_consumer.cursor < head; ++batch) {
                    std::uint64_t index = g_consumer.cursor;
                    if (head - index > k_ring_capacity) {
                        std::uint64_t const skipped = (head - k_ring_capacity) - index;
                        g_ring.lost.fetch_add(skipped, std::memory_order_relaxed);
                        index += skipped;
                        advance_cursor(index);
                    }

                    std::chrono::steady_clock::time_point started{};
                    read_state state = read_state::not_ready;
                    while (true) {
                        state = read_slot(index, *decoded);
                        if (state != read_state::not_ready) {
                            break;
                        }
                        if (started == std::chrono::steady_clock::time_point{}) {
                            started = std::chrono::steady_clock::now();
                        }
                        if (std::chrono::steady_clock::now() - started > std::chrono::milliseconds{20}) {
                            state = read_state::lost;
                            break;
                        }
                        std::this_thread::yield();
                    }

                    if (state == read_state::ready) {
                        process_record(active, *decoded, index, head);
                    } else {
                        g_ring.lost.fetch_add(1U, std::memory_order_relaxed);
                    }
                    advance_cursor(index + 1U);
                }
            }
        }

        void shutdown_consumer() noexcept {
            if (g_ring.state.load(std::memory_order_acquire) != k_state_running) {
                return;
            }
            g_ring.stop_requested.store(true, std::memory_order_release);
            if (g_consumer_thread != nullptr) {
                if (g_consumer_thread->joinable()) {
                    g_consumer_thread->join();
                }
                delete g_consumer_thread;
                g_consumer_thread = nullptr;
            }
            g_ring.state.store(k_state_stopped, std::memory_order_release);
        }

        // The first call warms up the unwinder so later stack captures stay allocation free.
        void warm_up_backtrace() noexcept {
            std::array<void*, 4U> scratch{};
            (void)::backtrace(scratch.data(), static_cast<int>(scratch.size()));
        }

        [[nodiscard]] auto start_consumer() noexcept -> bool {
            std::uint32_t expected = k_state_idle;
            if (!g_ring.state.compare_exchange_strong(expected, k_state_starting, std::memory_order_acq_rel)) {
                while (g_ring.state.load(std::memory_order_acquire) == k_state_starting) {
                    std::this_thread::yield();
                }
                return g_ring.state.load(std::memory_order_acquire) == k_state_running;
            }
            try {
                warm_up_backtrace();
                g_consumer_thread = new std::thread(consumer_main);
                (void)std::atexit(shutdown_consumer);
                g_ring.state.store(k_state_running, std::memory_order_release);
                return true;
            } catch (...) {
                g_consumer_thread = nullptr;
                g_ring.state.store(k_state_stopped, std::memory_order_release);
                return false;
            }
        }

        [[nodiscard]] auto ensure_consumer() noexcept -> bool {
            std::uint32_t const current = g_ring.state.load(std::memory_order_acquire);
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

            slot_header header{};
            header.monotonic_ns = raw_timestamp();
            header.file = pointer_word(entry.file);
            header.function = pointer_word(entry.function);
            header.line = entry.line;
            header.thread_id = current_thread_id();
            header.level = static_cast<std::uint8_t>(entry.level);
            std::size_t const text_len = std::min(entry.text.size(), k_max_message_bytes);
            std::size_t const binary_len = std::min(entry.binary.size(), k_max_binary_bytes);
            header.text_len = static_cast<std::uint16_t>(text_len);
            header.binary_len = static_cast<std::uint16_t>(binary_len);
            if (entry.entry != nullptr) {
                header.has_entry = 1U;
                header.label = pointer_word(entry.label.data());
                header.label_len = static_cast<std::uint8_t>(std::min<std::size_t>(entry.label.size(), 255U));
                header.message_id = pointer_word(entry.entry->message_id.data());
                header.message_id_len = static_cast<std::uint8_t>(std::min<std::size_t>(entry.entry->message_id.size(), 255U));
                header.entry_component = pointer_word(entry.entry->component.data());
                header.component_len = static_cast<std::uint8_t>(std::min<std::size_t>(entry.entry->component.size(), 255U));
                header.entry_summary = pointer_word(entry.entry->summary.data());
                header.summary_len = static_cast<std::uint16_t>(std::min<std::size_t>(entry.entry->summary.size(), 65535U));
            }

            std::uint64_t const index = g_ring.head.fetch_add(1U, std::memory_order_relaxed);
            std::size_t const slot = static_cast<std::size_t>(index & k_ring_mask);
            std::atomic_ref<std::uint64_t> const stamp(g_ring.stamps.at(slot));
            stamp.store((index * 2U) + 1U, std::memory_order_relaxed);

            auto const words = slot_span(index);
            std::array<std::uint64_t, k_header_words> header_words{};
            std::memcpy(header_words.data(), &header, sizeof(slot_header));
            for (std::size_t idx = 0U; idx < k_header_words; ++idx) {
                store_word(words[idx], header_words.at(idx));
            }
            std::size_t const text_words = (text_len + sizeof(std::uint64_t) - 1U) / sizeof(std::uint64_t);
            if (text_len != 0U) {
                store_bytes(words.subspan(k_header_words, k_text_words), entry.text.data(), text_len);
            }
            if (binary_len != 0U) {
                store_bytes(words.subspan(k_header_words + text_words, k_binary_words), entry.binary.data(), binary_len);
            }
            stamp.store((index * 2U) + 2U, std::memory_order_release);
        }
    } // namespace

    namespace detail {

        auto capture_current_stacktrace(
            std::span<std::uintptr_t> frame_buffer
        ) noexcept -> std::size_t {
            std::array<void*, k_max_stacktrace_frames> raw{};
            std::size_t const wanted = std::min(raw.size(), frame_buffer.size());
            if (wanted == 0U) {
                return 0U;
            }
            int const captured = ::backtrace(raw.data(), static_cast<int>(wanted));
            std::size_t const count = captured > 0 ? static_cast<std::size_t>(captured) : 0U;
            for (std::size_t idx = 0U; idx < count; ++idx) {
                frame_buffer[idx] = std::bit_cast<std::uintptr_t>(raw.at(idx));
            }
            return count;
        }

        void emit_log_record(const log_record& record) noexcept {
            submit(submission{
                .level = record.level,
                .text = record.text,
                .binary = record.binary,
                .file = record.location.file_name(),
                .function = record.location.function_name(),
                .line = record.location.line()
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
            std::lock_guard<spin_lock> const guard(g_sink_lock);
            g_sink = sink;
        }

        void reset_record_sink() noexcept {
            set_record_sink(record_sink{});
        }

        void drop_history() noexcept {
            (void)flush(std::chrono::milliseconds{5000});
            g_ring.history_floor.store(g_ring.head.load(std::memory_order_acquire), std::memory_order_release);
        }

    } // namespace detail

    auto activate_policy(const policy& candidate) noexcept -> bool {
        if (!is_valid_policy(candidate)) {
            return false;
        }
        std::lock_guard<spin_lock> const guard(g_policy_lock);
        g_policy = candidate;
        g_policy_generation.fetch_add(1U, std::memory_order_release);
        return true;
    }

    void reset_default_policy() noexcept {
        std::lock_guard<spin_lock> const guard(g_policy_lock);
        g_policy = policy{};
        g_policy_generation.fetch_add(1U, std::memory_order_release);
    }

    auto active_policy() noexcept -> policy {
        return snapshot_policy();
    }

    auto flush(std::chrono::milliseconds timeout) noexcept -> bool {
        if (g_ring.state.load(std::memory_order_acquire) != k_state_running) {
            return true;
        }
        std::uint64_t const target = g_ring.head.load(std::memory_order_acquire);
        auto const deadline = std::chrono::steady_clock::now() + timeout;
        while (g_ring.consumed.load(std::memory_order_acquire) < target) {
            if (std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds{20});
        }
        return true;
    }

    auto statistics_snapshot() noexcept -> statistics {
        return statistics{
            .submitted = g_ring.head.load(std::memory_order_acquire),
            .emitted = g_ring.emitted.load(std::memory_order_acquire),
            .lost = g_ring.lost.load(std::memory_order_acquire)
        };
    }

    auto catch_boundary_exception(
        const catalog_entry& default_entry,
        std::source_location location
    ) noexcept -> bool {
        std::array<std::uintptr_t, k_max_stacktrace_frames> fallback_frames{};
        auto const capture_fallback_bytes = [&fallback_frames]() noexcept -> std::span<const std::byte> {
            std::size_t const frame_count = detail::capture_current_stacktrace(fallback_frames);
            return std::as_bytes(std::span<const std::uintptr_t>{fallback_frames.data(), frame_count});
        };

        try {
            std::exception_ptr const current = std::current_exception();
            if (current) {
                std::rethrow_exception(current);
            }
            detail::submit_catalog_record(
                severity::error,
                default_entry,
                "",
                "catch_boundary_exception invoked without active exception",
                capture_fallback_bytes(),
                location
            );
        } catch (const catalog_exception& ex) {
            auto const trace_payload = ex.trace_bytes().empty()
                ? capture_fallback_bytes()
                : ex.trace_bytes();
            detail::submit_catalog_record(
                severity::error,
                ex.entry(),
                "catalog_exception: ",
                ex.what(),
                trace_payload,
                ex.where()
            );
        } catch (const std::invalid_argument& ex) {
            detail::submit_catalog_record(
                severity::error,
                default_entry,
                "std::invalid_argument: ",
                ex.what(),
                capture_fallback_bytes(),
                location
            );
        } catch (const std::out_of_range& ex) {
            detail::submit_catalog_record(
                severity::error,
                default_entry,
                "std::out_of_range: ",
                ex.what(),
                capture_fallback_bytes(),
                location
            );
        } catch (const std::system_error& ex) {
            detail::submit_catalog_record(
                severity::error,
                default_entry,
                "std::system_error: ",
                ex.what(),
                capture_fallback_bytes(),
                location
            );
        } catch (const std::exception& ex) {
            detail::submit_catalog_record(
                severity::error,
                default_entry,
                "std::exception: ",
                ex.what(),
                capture_fallback_bytes(),
                location
            );
        } catch (...) {
            detail::submit_catalog_record(
                severity::critical,
                default_entry,
                "",
                "non-standard unknown exception",
                capture_fallback_bytes(),
                location
            );
        }
        return false;
    }

} // namespace target_layout_extended::journal
