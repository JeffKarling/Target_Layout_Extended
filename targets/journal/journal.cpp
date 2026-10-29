#define SD_JOURNAL_SUPPRESS_LOCATION 1
#include <journal/journal.hpp>
#include <systemd/sd-journal.h>
#include <syslog.h>
#include <exception>
#include <stdexcept>
#include <string>
#include <system_error>

namespace target_layout_extended::journal {

    namespace {
        void send_to_journal(
            int priority,
            const catalog_entry& entry,
            std::string_view detail,
            std::source_location location
        ) noexcept {
            std::string const msg_id_field = "MESSAGE_ID=" + std::string(entry.message_id);
            std::string const component_field = "COMPONENT=" + std::string(entry.component);
            std::string const file_field = "CODE_FILE=" + std::string(location.file_name());
            std::string const line_field = "CODE_LINE=" + std::to_string(location.line());
            std::string const func_field = "CODE_FUNC=" + std::string(location.function_name());
            std::string const message_field =
                "MESSAGE=[" + std::string(entry.component) + "] " +
                std::string(entry.summary) + " (" + std::string(detail) + ")";
            std::string const priority_field = "PRIORITY=" + std::to_string(priority);

            (void)sd_journal_send(
                "%s", message_field.c_str(),
                "%s", priority_field.c_str(),
                "%s", msg_id_field.c_str(),
                "%s", component_field.c_str(),
                "%s", file_field.c_str(),
                "%s", line_field.c_str(),
                "%s", func_field.c_str(),
                nullptr
            );
        }
    } // namespace

    void log_info(
        const catalog_entry& entry,
        std::string_view detail,
        std::source_location location
    ) noexcept {
        send_to_journal(LOG_INFO, entry, detail, location);
    }

    void log_error(
        const catalog_entry& entry,
        std::string_view detail,
        std::source_location location
    ) noexcept {
        send_to_journal(LOG_ERR, entry, detail, location);
    }

    auto catch_boundary_exception(
        const catalog_entry& entry,
        std::source_location location
    ) noexcept -> bool {
        try {
            std::exception_ptr const current = std::current_exception();
            if (current) {
                std::rethrow_exception(current);
            }
            send_to_journal(LOG_ERR, entry, "catch_boundary_exception invoked without active exception", location);
        } catch (const std::invalid_argument& ex) {
            send_to_journal(LOG_ERR, entry, std::string("std::invalid_argument: ") + ex.what(), location);
        } catch (const std::out_of_range& ex) {
            send_to_journal(LOG_ERR, entry, std::string("std::out_of_range: ") + ex.what(), location);
        } catch (const std::system_error& ex) {
            send_to_journal(LOG_ERR, entry, std::string("std::system_error: ") + ex.what(), location);
        } catch (const std::exception& ex) {
            send_to_journal(LOG_ERR, entry, std::string("std::exception: ") + ex.what(), location);
        } catch (...) {
            send_to_journal(LOG_CRIT, entry, "non-standard unknown exception", location);
        }
        return false;
    }

} // namespace target_layout_extended::journal
