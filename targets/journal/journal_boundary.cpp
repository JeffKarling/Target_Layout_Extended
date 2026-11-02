#include <journal/journal.hpp>
#include <execinfo.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>
#include <system_error>

namespace target_layout_extended::journal {

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
                frame_buffer.subspan(idx).front() = std::bit_cast<std::uintptr_t>(raw.at(idx));
            }
            return count;
        }

    } // namespace detail

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
