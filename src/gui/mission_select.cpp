// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mission_select.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "graphics/overlay_camera.h"
#include "gui/colour_table.h"
#include "gui/game_stats.h"
#include "gui/text_layout.h"

namespace coney::gui {

MissionSelectLayout MissionSelectLayout::forFlags(bool flag02, bool widescreen, bool flag20) {
    MissionSelectLayout layout;
    layout.column = mapColumn(flag02, widescreen, flag20);
    // Coney's choice: the entries' circles' y offset (`0x0050e868`: -0.002, 0.008 with 16:9) is not kept, as Coney's
    // CircledText places its circles on the text's row.
    if (flag02 && widescreen) {
        layout.x = 0.843F;
        layout.y = 0.153F;
        layout.textX = 0.508F;
        layout.textY = 0.067F;
        layout.tile = 0.25F;
        layout.circleW = 0.04F;
        layout.circleH = 0.056F;
        layout.background = 1.6F;
        layout.backgroundHeight = 0.88F;
        layout.logo = 0.6F;
        layout.logoWidth = 2.0F;
        layout.marker = 0.012F;
        layout.usageX = -0.08F;
        layout.usageY = 0.76F;
        layout.alternative = true;
        layout.stepDivisor = CircledText::kStepDivisor02;
        return layout;
    }
    if (flag02) {
        layout.x = 0.785F;
        layout.y = 0.173F;
        layout.textX = 0.43F;
        layout.textY = 0.057F;
        layout.tile = 0.27F;
        layout.circleW = 0.04F;
        layout.circleH = 0.056F;
        layout.background = 1.05F;
        layout.backgroundHeight = 0.82F;
        layout.logo = 0.6F;
        layout.logoWidth = 2.0F;
        layout.marker = 0.01F;
        layout.usageX = 0.045F;
        layout.usageY = 0.75F;
        layout.stepDivisor = CircledText::kStepDivisor02;
        return layout;
    }
    if (flag20) {
        // Progressive keeps the static values, which differ from the default mode's, with the alternative sheet.
        layout.x = 0.78F;
        layout.y = 0.0772F;
        layout.textX = 0.415F;
        layout.textY = 0.065F;
        layout.tile = 0.415F;
        layout.circleW = 0.05F;
        layout.circleH = 0.07F;
        layout.background = 1.05F;
        layout.usageX = 0.03F;
        layout.usageY = 0.83F;
        layout.alternative = true;
        return layout;
    }
    if (widescreen) {
        layout.x = 0.852F;
        layout.y = 0.022F;
        layout.textX = 0.52F;
        layout.textY = 0.083F;
        layout.circleOffset = 0.01F;
        layout.tile = 0.423F;
        layout.circleW = 0.057F;
        layout.circleH = 0.08F;
        layout.background = 1.64F;
        layout.backgroundHeight = 1.2F;
        layout.usageX = -0.1F;
        layout.usageY = 0.85F;
        layout.alternative = true;
        return layout;
    }
    return layout;
}

void MissionSelectScreen::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

MissionSelectMission& MissionSelectScreen::current() { return m_areas.at(m_cursor).missions.at(m_list.selected()); }

void MissionSelectScreen::open(std::vector<MissionSelectArea> areas, std::uint64_t nowMs) {
    m_areas = std::move(areas);
    m_open = true;
    m_cancelled = false;
    m_chosen = false;
    m_chosenLevel = 0;
    m_chosenCheckpoint = 1;
    m_start = Start::None;
    m_startMs = 0;
    m_shown.reset();
    m_missionOpen = false;
    m_refused = false;
    m_cursor = 0;
    // The map (grey 144), the gang logo (white, with a shadow) and the halo (rectangle 2, 3 in the other sheet), all
    // placed by each update.
    m_map.setBatch(m_batches.frame);
    m_logo.setBatch(m_batches.logos);
    m_halo.setBatch(m_batches.frame);
    m_halo.setRect(m_layout.alternative ? 3 : 2);
    // A marker per area.
    m_markers.clear();
    m_markers.reserve(m_areas.size());
    for (std::size_t i = 0; i < m_areas.size(); ++i) {
        m_markers.push_back(std::make_unique<BaseWidget>(m_batches.frame, kMarkerRect));
    }
    // The title: the text's offset as its size, circles 0.055 square, the back rectangle 1 and circles 3 (2 in the
    // other sheet), Font_Size(1.25), no circled characters.
    // Coney's choice: x is the left edge, as Coney's CircledText takes it (the original's takes the centre).
    m_title.setBack(m_batches.frame, 1);
    m_title.setCircleSprite(m_batches.frame, m_layout.alternative ? 2 : 3);
    m_title.setup(CircledTextSetup{.x = m_layout.x - (m_layout.textX / 2.0F),
                                   .y = m_layout.y,
                                   .width = m_layout.textX,
                                   .height = m_layout.textY,
                                   .circleWidth = kTitleCircle,
                                   .circleHeight = kTitleCircle,
                                   .circleOffset = m_layout.circleOffset,
                                   .textSize = kTitleSize,
                                   .stepDivisor = m_layout.stepDivisor});
    // Coney's choice: the title has no circled characters (the slot +0x94 call after its set-up is not traced).
    m_title.setCircled({});
    // The usage line, left-aligned at the layout's point, showing global string 0x20.
    m_usage.place(m_layout.usageX, m_layout.usageY, true);
    m_usage.setLegend(m_strings != nullptr ? m_strings->get(kUsageString) : std::string_view{});
    m_letter.setup(TextWidgetSetup{.colour = colourAt(0x005fd328), .fontSlot = CircledText::kFontSlot});
    // The first area shown, then the area menu takes the focus.
    showArea();
    m_menuFocused = true;
    m_input.focus(nowMs);
    m_input.setDpadRepeat(true);
}

void MissionSelectScreen::close() {
    m_open = false;
    m_entries.clear();
    m_markers.clear();
    m_areas.clear();
}

void MissionSelectScreen::showArea() {
    m_refused = false;
    fillArea();
}

void MissionSelectScreen::fillArea() {
    m_entries.clear();
    if (m_cursor >= m_areas.size()) {
        m_list.setup(ScrollingMenuSetup{.visible = 0, .moveCue = kMoveCue, .playCue = m_playCue}, 0);
        return;
    }
    const MissionSelectArea& area = m_areas.at(m_cursor);
    // An entry per mission: its name after its circled label, the circles in its colour, on a black back (rectangle
    // 1, circles 3, or 2 in the other sheet), Font_Size(0.92), stacked down from below the title by each update.
    for (const MissionSelectMission& mission : area.missions) {
        auto entry = std::make_unique<CircledText>();
        entry->setBack(m_batches.frame, 1);
        entry->setCircleSprite(m_batches.frame, m_layout.alternative ? 2 : 3);
        entry->setup(CircledTextSetup{.x = m_layout.x - (m_layout.textX / 2.0F),
                                      .y = m_layout.y,
                                      .width = m_layout.textX,
                                      .height = m_layout.textY,
                                      .circleWidth = m_layout.circleW,
                                      .circleHeight = m_layout.circleH,
                                      .circleOffset = m_layout.circleOffset,
                                      .textSize = kEntrySize,
                                      .stepDivisor = m_layout.stepDivisor});
        entry->setText(mission.name);
        entry->setCircled(mission.label);
        entry->setBackColour(graphics::kBlack);
        entry->setCircleColour(mission.circleColour);
        m_entries.push_back(std::move(entry));
    }
    // The area's list keeps its selection (the original keeps a menu per area); every mission shows.
    m_list.setup(ScrollingMenuSetup{.visible = area.missions.size(), .moveCue = kMoveCue, .playCue = m_playCue},
                 area.missions.size());
    m_list.select(area.selected);
}

void MissionSelectScreen::nextArea() {
    if (m_cursor + 1 < m_areas.size()) {
        ++m_cursor;
        showArea();
        playCue(kMoveCue);
        return;
    }
    playCue(kRefusedCue);
    m_refused = true;
}

void MissionSelectScreen::prevArea() {
    if (m_cursor == 0) {
        playCue(kRefusedCue);
        m_refused = true;
        return;
    }
    --m_cursor;
    showArea();
    playCue(kMoveCue);
}

void MissionSelectScreen::onMenuCommand(MenuCommand command, std::uint64_t nowMs) {
    switch (command) {
    case MenuCommand::Up:
        nextArea();
        return;
    case MenuCommand::Down:
        prevArea();
        return;
    case MenuCommand::Left:
    case MenuCommand::Right:
        return;
    case MenuCommand::Accept:
        // The focus moves to the area's list. Coney's choice: nothing without a mission to list (the original never
        // opens with none).
        if (m_cursor >= m_areas.size() || m_areas.at(m_cursor).missions.empty()) {
            return;
        }
        m_menuFocused = false;
        m_list.takeFocus(m_input, nowMs);
        playCue(kAcceptCue);
        return;
    case MenuCommand::Back:
        m_cancelled = true;
        return;
    }
}

bool MissionSelectScreen::onListCommand(MenuCommand command, std::uint64_t nowMs) {
    switch (command) {
    case MenuCommand::Down: {
        // With a mission open, the next checkpoint letter within its sections; else the list's own move.
        if (!m_missionOpen) {
            return false;
        }
        MissionSelectMission& mission = current();
        if (mission.letter + 1 >= mission.checkpoints) {
            playCue(kRefusedCue);
            m_refused = true;
            return true;
        }
        ++mission.letter;
        playCue(kMoveCue);
        return true;
    }
    case MenuCommand::Up: {
        if (!m_missionOpen) {
            return false;
        }
        MissionSelectMission& mission = current();
        if (mission.letter == 0) {
            playCue(kRefusedCue);
            m_refused = true;
            return true;
        }
        --mission.letter;
        playCue(kMoveCue);
        return true;
    }
    case MenuCommand::Accept:
        // The first accept opens the selected mission (its tile highlighted), the second starts it.
        if (!m_missionOpen) {
            m_missionOpen = true;
            current().open = true;
        } else {
            // The start stamps no time: the choice waits for the clock against Init's 0 (`+0x6a0`).
            m_start = Start::Waiting;
        }
        playCue(kAcceptCue);
        return true;
    case MenuCommand::Back:
        // With none open the focus goes back to the area menu; either way the mission closes.
        if (!m_missionOpen) {
            m_menuFocused = true;
            m_input.focus(nowMs);
            m_input.setDpadRepeat(true);
        }
        m_missionOpen = false;
        current().open = false;
        playCue(kBackCue);
        return true;
    case MenuCommand::Left:
    case MenuCommand::Right:
        return false;
    }
    return false;
}

void MissionSelectScreen::handle(MenuCommand command, std::uint64_t nowMs) {
    // No command once the screen is done or while a start waits (the original reads no pad then).
    if (!m_open || done() || m_start != Start::None) {
        return;
    }
    if (m_menuFocused) {
        onMenuCommand(command, nowMs);
        return;
    }
    // The list's handler first; what it leaves goes to the list's own moves.
    if (!onListCommand(command, nowMs) && (command == MenuCommand::Up || command == MenuCommand::Down)) {
        (void)m_list.handle(command, nowMs);
        m_areas.at(m_cursor).selected = m_list.selected();
    }
}

void MissionSelectScreen::onAccept() {
    const MissionSelectMission& mission = current();
    playCue(kAcceptCue);
    m_chosenLevel = mission.level;
    m_chosenCheckpoint = static_cast<int>(mission.letter) + 1;
    m_chosen = true;
}

void MissionSelectScreen::update(const GuiFrame& frame) {
    if (!m_open || done()) {
        return;
    }
    // While a start waits, only the clock: the choice on the first frame the clock is more than kStartDelayMs past
    // the start stamp, which only open() sets (to 0), so in play on the next frame. Coney's stand-in: the clock is
    // the game time the screen is given, not the real-time clock since start-up.
    if (m_start != Start::None) {
        if (m_start == Start::Waiting && frame.timeMs - m_startMs > kStartDelayMs) {
            m_start = Start::Accepted;
            onAccept();
        }
        return;
    }
    if (m_cursor >= m_areas.size()) {
        return;
    }
    // A newly shown area starts the pulse again and names the title.
    if (m_shown != m_cursor) {
        m_shown = m_cursor;
        m_pulseUp = false;
        m_pulseStartMs = frame.timeMs;
        m_title.setText(m_areas.at(m_cursor).name);
    }
    // The pad: the area menu's d-pad repeats unless the last move was refused.
    if (frame.pad != nullptr) {
        if (m_menuFocused) {
            m_input.setDpadRepeat(!m_refused);
        }
        if (const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, frame.timeMs)) {
            handle(*command, frame.timeMs);
        }
    }
    const MissionSelectArea& area = m_areas.at(m_cursor);
    // The map: its width a GUI width (Coney's reading, as the statistics screen's background) by its height.
    m_map.setup(BaseWidgetSetup{.x = 0.5F,
                                .y = 0.5F,
                                .height = m_layout.backgroundHeight,
                                .width = graphics::OverlayCamera::guiWidthToOverlay(m_layout.background),
                                .colour = kMapGrey});
    // The area's gang logo at its point, sized by its texture rectangle × the logo's size (× the width factor).
    // The sprite word's rectangle: its low half.
    const std::size_t logoRect = area.logoWord & 0xffffU;
    m_logo.setRect(logoRect);
    graphics::UvRect uv{};
    if (m_batches.logos != nullptr && logoRect < m_batches.logos->sheet().page.rects.size()) {
        uv = m_batches.logos->sheet().page.rect(logoRect);
    }
    m_logo.setup(BaseWidgetSetup{.x = area.logo.x,
                                 .y = area.logo.y,
                                 .height = (uv.v1 - uv.v0) * m_layout.logo,
                                 .width = (uv.u1 - uv.u0) * m_layout.logo * m_layout.logoWidth,
                                 .colour = graphics::kWhite,
                                 .shadow = true});
    // The pulse: one way each kPulseMs, from 2 down to 1, then from 1 up to 2.
    std::uint64_t elapsed = frame.timeMs - m_pulseStartMs;
    if (elapsed >= kPulseMs) {
        elapsed = 0;
        m_pulseStartMs = frame.timeMs;
        m_pulseUp = !m_pulseUp;
    }
    const float share = static_cast<float>(elapsed) / static_cast<float>(kPulseMs);
    m_pulse = m_pulseUp ? 1.0F + share : 2.0F - share;
    // Every marker at its area's point in the menus' grey; the shown one pulsing, with the halo around it in the
    // area's halo colour × the map's grey.
    for (std::size_t i = 0; i < m_markers.size(); ++i) {
        const bool shown = i == m_cursor;
        m_markers.at(i)->setup(BaseWidgetSetup{.x = m_areas.at(i).marker.x,
                                               .y = m_areas.at(i).marker.y,
                                               .height = m_layout.marker * (shown ? m_pulse : 1.0F),
                                               .colour = kMenuGrey});
    }
    // A channel of the halo colour × the map's grey / 255, the fraction dropped (`Float_ToUInt`).
    const auto tint = [](std::uint8_t colour, std::uint8_t grey) {
        return static_cast<std::uint8_t>(static_cast<float>(colour) * 0.003921569F * static_cast<float>(grey));
    };
    m_halo.setup(BaseWidgetSetup{.x = area.marker.x,
                                 .y = area.marker.y,
                                 .height = m_layout.marker * m_pulse * kHaloScale,
                                 .colour = graphics::Rgba{tint(area.halo.r, kMapGrey.r), tint(area.halo.g, kMapGrey.g),
                                                          tint(area.halo.b, kMapGrey.b), area.halo.a}});
    m_title.update(frame);
    // The list: the entries stacked from below the title, an item gap apart; the open mission's letter under its
    // entry (the tile's stand-in, #1451).
    float y = m_layout.y + m_layout.textY + m_layout.listGap;
    m_letter.setVisible(false);
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        CircledText& entry = *m_entries.at(i);
        entry.moveTo(y);
        entry.setFocused(!m_menuFocused && i == m_list.selected());
        entry.update(frame);
        const MissionSelectMission& mission = area.missions.at(i);
        if (mission.open) {
            m_letter.setVisible(true);
            m_letter.style().x = m_layout.x - (m_layout.textX / 2.0F) + m_layout.circleW;
            m_letter.style().y = y + m_layout.textY;
            m_letter.setText(std::string(1, static_cast<char>('A' + mission.letter)));
            y += m_layout.textY;
        }
        y += m_layout.textY + m_layout.listGap;
    }
    m_letter.update(frame);
    m_usage.update(frame);
}

void MissionSelectScreen::render(const GuiCanvas& canvas) const {
    if (!m_open || !m_shown) {
        return;
    }
    m_map.render(canvas);
    m_logo.render(canvas);
    m_halo.render(canvas);
    for (const std::unique_ptr<BaseWidget>& marker : m_markers) {
        marker->render(canvas);
    }
    // The title and the area's list. Coney's stand-in for the open mission's tile is its letter (#1451).
    m_title.render(canvas);
    for (const std::unique_ptr<CircledText>& entry : m_entries) {
        entry->render(canvas);
    }
    if (m_letter.visible()) {
        m_letter.render(canvas);
    }
    m_usage.render(canvas);
}

} // namespace coney::gui
