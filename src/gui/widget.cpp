// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/widget.h"

namespace coney::gui {

void Widget::init() {
    if (!m_initialised) {
        // Marked first, as the original sets its flag before creating children.
        m_initialised = true;
        onInit();
    }
}

void Widget::shutdown() {
    if (m_initialised) {
        onShutdown();
        m_initialised = false;
    }
}

} // namespace coney::gui
