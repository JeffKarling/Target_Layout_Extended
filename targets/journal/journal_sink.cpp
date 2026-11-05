#define SD_JOURNAL_SUPPRESS_LOCATION 1
#include <journal_sink.hpp>
#include <journal/journal.hpp>
#include <sys/uio.h>
#include <syslog.h>
#include <systemd/sd-journal.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>

namespace tle::journal {

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
        inline constexpr std::size_t k_lookbehind_buf_size = 24U;
        inline constexpr std::size_t k_binary_buf_size = k_max_binary_bytes + 16U;
        inline constexpr std::size_t k_max_iovec_fields = 13U;
        inline constexpr std::uint8_t k_max_severity_index = 7U;

        struct kv_pair {
            std::string_view key;
            std::string_view value;
        };

        constexpr void append_string(
            std::span<char> buffer,
            std::size_t& offset,
            std::string_view text
        ) noexcept {
            if (offset >= buffer.size() || text.empty()) {
                return;
            }
            auto const target = buffer.subspan(offset);
            std::size_t const copy_len = std::min(text.size(), target.size());
            std::ranges::copy(text.substr(0U, copy_len), target.begin());
            offset += copy_len;
        }

        [[nodiscard]] constexpr auto format_priority_field(
            std::span<char> buffer,
            severity level
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, "PRIORITY=");
            auto const raw_level = static_cast<std::uint8_t>(level);
            char const digit = (raw_level <= k_max_severity_index)
                ? static_cast<char>('0' + raw_level)
                : '3';
            if (offset < buffer.size()) {
                buffer.subspan(offset).front() = digit;
                ++offset;
            }
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_kv_field(
            std::span<char> buffer,
            const kv_pair& field
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, field.key);
            append_string(buffer, offset, field.value);
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_number_field(
            std::span<char> buffer,
            std::string_view prefix,
            std::uint64_t value
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, prefix);
            auto const tail = buffer.subspan(offset);
            auto const [ptr, ec] = std::to_chars(
                std::to_address(tail.begin()),
                std::to_address(tail.end()),
                value
            );
            if (ec == std::errc{}) {
                offset += static_cast<std::size_t>(ptr - std::to_address(tail.begin()));
            }
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_internal_message_field(
            std::span<char> buffer,
            const kv_pair& comp_and_text
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, "MESSAGE=[");
            append_string(buffer, offset, comp_and_text.key);
            append_string(buffer, offset, "] ");
            append_string(
                buffer,
                offset,
                comp_and_text.value.empty() ? "binary diagnostic event" : comp_and_text.value
            );
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_catalog_message_field(
            std::span<char> buffer,
            const catalog_entry& entry,
            const kv_pair& label_and_detail
        ) noexcept -> std::span<char> {
            std::size_t offset = 0U;
            append_string(buffer, offset, "MESSAGE=[");
            append_string(buffer, offset, entry.component);
            append_string(buffer, offset, "] ");
            append_string(buffer, offset, entry.summary);
            append_string(buffer, offset, " (");
            append_string(buffer, offset, label_and_detail.key);
            append_string(buffer, offset, label_and_detail.value);
            append_string(buffer, offset, ")");
            return buffer.first(offset);
        }

        [[nodiscard]] constexpr auto format_binary_field(
            std::span<std::byte> buffer,
            std::span<const std::byte> payload
        ) noexcept -> std::span<std::byte> {
            constexpr std::string_view k_prefix = "RAW_PAYLOAD=";
            std::size_t const prefix_len = std::min(k_prefix.size(), buffer.size());
            std::ranges::transform(
                k_prefix.substr(0U, prefix_len),
                buffer.begin(),
                [](char ch) constexpr noexcept { return static_cast<std::byte>(ch); }
            );
            std::size_t offset = prefix_len;
            auto const tail = buffer.subspan(offset);
            std::size_t const copy_len = std::min(payload.size(), tail.size());
            std::ranges::copy(payload.first(copy_len), tail.begin());
            offset += copy_len;
            return buffer.first(offset);
        }

        [[nodiscard]] consteval auto verify_constexpr_formatters() noexcept -> bool {
            constexpr std::size_t k_test_buf_size = 64U;
            constexpr std::uint64_t k_test_line = 128U;
            constexpr std::uint64_t k_test_seq = 1234567890123ULL;
            constexpr std::byte k_sample_a{0xAA};
            constexpr std::byte k_sample_b{0xBB};
            constexpr std::size_t k_expected_bin_len = 14U;
            constexpr std::size_t k_first_payload_idx = 12U;
            constexpr std::size_t k_second_payload_idx = 13U;

            std::array<char, k_test_buf_size> prio_buf{};
            auto const prio = format_priority_field(prio_buf, severity::warning);
            if (std::string_view(prio.data(), prio.size()) != "PRIORITY=4") {
                return false;
            }

            std::array<char, k_test_buf_size> line_buf{};
            auto const line = format_number_field(line_buf, "CODE_LINE=", k_test_line);
            if (std::string_view(line.data(), line.size()) != "CODE_LINE=128") {
                return false;
            }

            std::array<char, k_test_buf_size> seq_buf{};
            auto const seq = format_number_field(seq_buf, "LOG_SEQ=", k_test_seq);
            if (std::string_view(seq.data(), seq.size()) != "LOG_SEQ=1234567890123") {
                return false;
            }

            std::array<char, k_test_buf_size> msg_buf{};
            auto const msg = format_internal_message_field(msg_buf, kv_pair{.key = "journal", .value = "ok"});
            if (std::string_view(msg.data(), msg.size()) != "MESSAGE=[journal] ok") {
                return false;
            }

            std::array<std::byte, k_test_buf_size> bin_buf{};
            std::array<std::byte, 2U> const sample = {k_sample_a, k_sample_b};
            auto const bin = format_binary_field(bin_buf, sample);
            return bin.size() == k_expected_bin_len &&
                   bin.subspan(k_first_payload_idx).front() == k_sample_a &&
                   bin.subspan(k_second_payload_idx).front() == k_sample_b;
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

        struct formatted_buffers {
            std::array<char, k_message_buf_size> message{};
            std::array<char, k_priority_buf_size> priority{};
            std::array<char, k_msg_id_buf_size> msg_id{};
            std::array<char, k_component_buf_size> component{};
            std::array<char, k_file_buf_size> file{};
            std::array<char, k_number_buf_size> line{};
            std::array<char, k_func_buf_size> func{};
            std::array<char, k_number_buf_size> seq{};
            std::array<char, k_number_buf_size> time{};
            std::array<char, k_number_buf_size> tid{};
            std::array<char, k_lookbehind_buf_size> lookbehind{};
            std::array<std::byte, k_binary_buf_size> binary{};
        };
    } // namespace

    namespace detail {

        void send_to_journald(const emitted_record& record) noexcept {
            formatted_buffers bufs{};
            std::array<iovec, k_max_iovec_fields> vectors{};
            std::size_t count = 0U;

            if (record.entry != nullptr) {
                vectors.at(count++) = to_iovec(format_catalog_message_field(
                    bufs.message,
                    *record.entry,
                    kv_pair{.key = record.label, .value = record.text}
                ));
                vectors.at(count++) = to_iovec(format_priority_field(bufs.priority, record.level));
                vectors.at(count++) = to_iovec(format_kv_field(
                    bufs.msg_id,
                    kv_pair{.key = "MESSAGE_ID=", .value = record.entry->message_id}
                ));
            } else {
                vectors.at(count++) = to_iovec(format_internal_message_field(
                    bufs.message,
                    kv_pair{.key = record.component, .value = record.text}
                ));
                vectors.at(count++) = to_iovec(format_priority_field(bufs.priority, record.level));
            }
            vectors.at(count++) = to_iovec(format_kv_field(
                bufs.component,
                kv_pair{.key = "COMPONENT=", .value = record.component}
            ));
            vectors.at(count++) = to_iovec(format_kv_field(
                bufs.file,
                kv_pair{.key = "CODE_FILE=", .value = record.file != nullptr ? record.file : ""}
            ));
            vectors.at(count++) = to_iovec(format_number_field(bufs.line, "CODE_LINE=", record.line));
            vectors.at(count++) = to_iovec(format_kv_field(
                bufs.func,
                kv_pair{.key = "CODE_FUNC=", .value = record.function != nullptr ? record.function : ""}
            ));
            vectors.at(count++) = to_iovec(format_number_field(bufs.seq, "LOG_SEQ=", record.sequence));
            vectors.at(count++) = to_iovec(format_number_field(bufs.time, "PRODUCER_MONOTONIC_NS=", record.monotonic_ns));
            vectors.at(count++) = to_iovec(format_number_field(bufs.tid, "TID=", record.thread_id));
            if (record.lookbehind) {
                vectors.at(count++) = to_iovec(format_kv_field(
                    bufs.lookbehind,
                    kv_pair{.key = "LOG_LOOKBEHIND=", .value = "1"}
                ));
            }
            if (!record.binary.empty()) {
                vectors.at(count++) = to_iovec(format_binary_field(bufs.binary, record.binary));
            }

            (void)sd_journal_sendv(vectors.data(), static_cast<int>(count));
        }

    } // namespace detail

} // namespace tle::journal
