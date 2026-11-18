// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/console_pad.h"

namespace coney::console {

PortSamples ConsolePad::sample(std::uint64_t frame) {
    return m_inner != nullptr ? m_inner->sample(frame) : PortSamples{};
}

void ConsolePad::setOpen(bool open, OpenedBy by) {
    m_open = open;
    m_openedBy = by;
}

} // namespace coney::console
