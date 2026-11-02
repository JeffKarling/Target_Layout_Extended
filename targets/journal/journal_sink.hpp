#pragma once

#include <journal/journal.hpp>

namespace target_layout_extended::journal::detail {

    // Private helper that formats an emitted record into stack buffers and sends it to systemd-journald.
    void send_to_journald(const emitted_record& record) noexcept;

} // namespace target_layout_extended::journal::detail
