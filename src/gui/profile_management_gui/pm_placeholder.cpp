// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_placeholder.h"

#include <optional>

namespace coney::gui {

void PmPlaceholder::enter(ScreenFlowController& /*flow*/) {
    m_text.init();
    m_text.setText(m_name);
    m_text.centreOn(0.5F, 0.5F, m_shared.layout.textBoxWidth);
    m_shared.input.focus(m_shared.frame.timeMs);
}

int PmPlaceholder::update() {
    const GuiFrame& frame = m_shared.frame;
    int result = kStay;
    if (frame.pad != nullptr && m_shared.input.dispatch(*frame.pad, frame.timeMs) == MenuCommand::Back) {
        result = kBack;
    }
    m_text.update(frame);
    m_text.render(m_shared.canvas);
    return result;
}

} // namespace coney::gui
