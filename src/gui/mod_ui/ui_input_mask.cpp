// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mod_ui/ui_input_mask.h"

namespace coney::gui {

PortSamples UiInputMask::sample(std::uint64_t frame) {
    return m_inner != nullptr ? m_inner->sample(frame) : PortSamples{};
}

} // namespace coney::gui
