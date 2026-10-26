// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/armies_game_over_menu.h"

#include <algorithm>
#include <string>

#include "gui/pause_menu/pause_menu.h"
#include "gui/text_layout.h"

namespace coney::gui {

std::string_view ArmiesGameOverMenu::string(std::uint32_t id) const {
    return m_strings != nullptr ? m_strings->get(id) : "";
}

void ArmiesGameOverMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void ArmiesGameOverMenu::open(std::uint64_t nowMs) {
    // Fails-first stub: the port is put back by the next commit.
    static_cast<void>(nowMs);
}

void ArmiesGameOverMenu::pressStart(bool haveCredit, std::uint64_t nowMs) {
    // Fails-first stub: the port is put back by the next commit.
    static_cast<void>(haveCredit);
    static_cast<void>(nowMs);
}

void ArmiesGameOverMenu::startEnd(std::uint64_t nowMs) {
    m_ending = true;
    m_endStartMs = nowMs;
    m_title.style().y = kEndY;
    m_title.style().scale = kEndScale;
    m_title.setText(string(kEndString));
    m_usage.place(0.5F, kEndUsageY, false);
    m_usage.setLegend(string(kEndUsageString));
    m_title.setFade(0.0F);
    m_grid.setFade(0.0F);
    m_usage.setFade(0.0F);
}

void ArmiesGameOverMenu::update(const GuiFrame& frame) {
    // Fails-first stub: the port is put back by the next commit.
    static_cast<void>(frame);
}

void ArmiesGameOverMenu::render(const GuiCanvas& canvas) const {
    if (!m_open || m_choice || !visible()) {
        return;
    }
    m_title.render(canvas);
    if (m_ending) {
        m_grid.render(canvas);
    } else {
        m_digit.render(canvas);
    }
    m_usage.render(canvas);
}

} // namespace coney::gui
