// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#include "gui/mod_ui/ui_flow_screen.h"

namespace coney::gui {

int UiFlowScreen::update() {
    const GuiFrame frame = m_frame ? m_frame() : GuiFrame{};
    if (frame.pad != nullptr) {
        m_model.step(m_slot, *frame.pad, frame.timeMs);
    }
    // The flow gets the menu back once the slot's last mod screen has closed.
    return m_model.top(m_slot) ? kStay : kBack;
}

} // namespace coney::gui
