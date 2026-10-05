// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/rumble_mode_gui/rumble_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "graphics/render_device.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// The menu's entries as a fresh boot offers them (docs/research/frontend.md#rumble-setup). The labels are the
// curated names the page lists: the page gives neither the mode list's nor the gang records' addresses, so they cannot
// be read from the player's executable yet.

// The players (value 0) each Game Type entry writes.
constexpr std::uint16_t kOnePlayer = 3;
constexpr std::uint16_t kVersus = 2;
constexpr std::uint16_t kCoop = 1;

// One entry of the mode list: its name, the mode's `RM_*` number, the fighters per side and the Game Type entries it
// offers, in order.
struct ModeEntry {
    std::string_view name;
    std::uint16_t gameType;
    std::uint16_t gangSize;
    std::span<const std::uint16_t> players;
};
constexpr std::array<std::uint16_t, 2> kOneOnOnePlayers{kOnePlayer, kVersus};
constexpr std::array<std::uint16_t, 3> kWarPartyPlayers{kOnePlayer, kCoop, kVersus};
// "1 ON 1" is `RM_Brawl1` (12), one a side; "WAR PARTY" five a side, `RM_Brawl5` (14) by its name (inferred).
constexpr std::array kModes{
    ModeEntry{"1 ON 1", 12, 1, kOneOnOnePlayers},
    ModeEntry{"WAR PARTY", 14, 5, kWarPartyPlayers},
};

// One side's gang as the gang screen writes it: its name, its pack - 1 and its nine character types.
struct GangEntry {
    std::string_view name;
    std::uint16_t pak;
    std::array<std::uint16_t, RumbleSetup::kGangMembers> types;
};
// The default pairing, read at run time: the player's BASEBALL FURIES against the computer's ORPHANS.
constexpr GangEntry kSide1Gang{"BASEBALL FURIES", 4, {91, 94, 91, 92, 93, 94, 91, 92, 93}};
constexpr GangEntry kSide2Gang{"ORPHANS", 2, {225, 226, 224, 225, 226, 227, 228, 225, 226}};

// One arena of the Choose Area screen: its name and level number.
struct AreaEntry {
    std::string_view name;
    std::uint16_t levelNumber;
};
constexpr std::array kAreas{AreaEntry{"Fight Pen", 102}};

// Where the screens put things, in GUI units: Coney's layout, in PM_Mode's style.
constexpr float kTitleY = 0.3F;
constexpr float kListTop = 0.5F;
constexpr float kRowGap = 0.08F;
constexpr float kBoxWidth = 0.8F;
constexpr float kSelectedScale = 1.15F;
constexpr graphics::Rgba kGrey{160, 160, 160, 255};

// The label of a Game Type entry writing `players`.
std::string_view playersLabel(std::uint16_t players) {
    switch (players) {
    case kOnePlayer:
        return "1 Player : Vs.";
    case kVersus:
        return "VS";
    default:
        return "COOP";
    }
}

// The Game Mode confirm: the mode's number and the gang size. (The original also copies the entry's name and three
// more fields outside the 23 values, and fills preset gangs for an entry that has them; none of the fresh boot's
// entries has.)
// @orig 0x001f8d80 RumbleGUI_ModeList_Confirm (unknown)
void applyMode(const ModeEntry& mode, RumbleSetup& setup) {
    setup.values.at(RumbleSetup::kGameType) = mode.gameType;
    setup.values.at(RumbleSetup::kGangSize) = mode.gangSize;
}

// The Game Type confirm: the players.
// @orig 0x001fd1c8 RumbleGUI_GameType_Confirm (unknown)
void applyPlayers(std::uint16_t players, RumbleSetup& setup) { setup.values.at(RumbleSetup::kGameMode) = players; }

// Copies a gang's name the way the menu does, at most 32 bytes.
// @orig 0x001fe070 RumbleMode_SetGang1Name (unknown)
// @orig 0x001fe0d8 RumbleMode_SetGang2Name (unknown)
void setGangName(std::string& name, std::string_view gang) { name = gang.substr(0, RumbleSetup::kGangNameLength); }

// The gang screen's confirm: each side's pack, nine character types and name.
// @orig 0x001ef7c0 RumbleGUI_Gangs_Confirm (unknown)
void applyGangs(const GangEntry& side1, const GangEntry& side2, RumbleSetup& setup) {
    setup.values.at(RumbleSetup::kGang1Pak) = side1.pak;
    setup.values.at(RumbleSetup::kGang2Pak) = side2.pak;
    for (std::size_t i = 0; i < RumbleSetup::kGangMembers; ++i) {
        setup.values.at(RumbleSetup::kGang1Types + i) = side1.types.at(i);
        setup.values.at(RumbleSetup::kGang2Types + i) = side2.types.at(i);
    }
    setGangName(setup.gangNames[0], side1.name);
    setGangName(setup.gangNames[1], side2.name);
}

// The last screen's confirm: the arena's level number (the original also sets "started", which the mode keeps).
// @orig 0x001ebb90 RumbleGUI_Area_Confirm (unknown)
void applyArea(const AreaEntry& area, RumbleSetup& setup) { setup.levelNumber = area.levelNumber; }

} // namespace

RumbleSetup rumbleMenuDefaults() {
    RumbleSetup setup;
    applyMode(kModes[0], setup);
    applyPlayers(kModes[0].players[0], setup);
    applyGangs(kSide1Gang, kSide2Gang, setup);
    applyArea(kAreas[0], setup);
    return setup;
}

void RumbleMenu::start(std::uint64_t nowMs) {
    m_mode = 0;
    show(RumbleScreen::GameMode, 0, nowMs);
}

std::string_view RumbleMenu::screenName() const {
    switch (m_screen) {
    case RumbleScreen::GameMode:
        return "Game Mode";
    case RumbleScreen::GameType:
        return "Game Type";
    case RumbleScreen::ChooseGangs:
        return "Choose Gangs";
    case RumbleScreen::ChooseArea:
        return "Choose Area";
    }
    return {};
}

RumbleMenuResult RumbleMenu::update(const GuiFrame& frame, std::size_t connectedPads, RumbleSetup& setup) {
    RumbleMenuResult result = RumbleMenuResult::Stay;
    const std::optional<MenuCommand> command =
        frame.pad != nullptr ? m_input.dispatch(*frame.pad, frame.timeMs) : std::nullopt;
    if (command == MenuCommand::Back) {
        // Back: the screen before, or out of the menu from the first.
        switch (m_screen) {
        case RumbleScreen::GameMode:
            result = RumbleMenuResult::Cancelled;
            break;
        case RumbleScreen::GameType:
            show(RumbleScreen::GameMode, m_mode, frame.timeMs);
            break;
        case RumbleScreen::ChooseGangs:
            show(RumbleScreen::GameType, 0, frame.timeMs);
            break;
        case RumbleScreen::ChooseArea:
            show(RumbleScreen::ChooseGangs, 0, frame.timeMs);
            break;
        }
    } else if (command) {
        if (const std::optional<int> chosen = m_grid.handle(*command)) {
            result = confirm(*chosen, connectedPads, setup, frame.timeMs);
        }
    }
    m_title.update(frame);
    m_rival.update(frame);
    m_grid.update(frame);
    return result;
}

void RumbleMenu::render(const GuiCanvas& canvas) const {
    m_title.render(canvas);
    m_grid.render(canvas);
    m_rival.render(canvas);
}

void RumbleMenu::show(RumbleScreen screen, std::size_t selected, std::uint64_t nowMs) {
    m_screen = screen;
    m_grid.init();
    m_grid.setup(OptionGridLayout{
        .centreX = 0.5F,
        .top = kListTop,
        .rowGap = kRowGap,
        .boxWidth = kBoxWidth,
        .scale = 1.0F,
        .selectedScale = kSelectedScale,
        .colour = kGrey,
        .selectedColour = graphics::kWhite,
    });
    m_rival.setText({});

    // Each screen's entries, the code of each being what its confirm needs.
    switch (screen) {
    case RumbleScreen::GameMode:
        for (std::size_t i = 0; i < kModes.size(); ++i) {
            m_grid.addItem(kModes.at(i).name, static_cast<int>(i));
        }
        break;
    case RumbleScreen::GameType:
        for (const std::uint16_t players : kModes.at(m_mode).players) {
            m_grid.addItem(playersLabel(players), players);
        }
        break;
    case RumbleScreen::ChooseGangs:
        // One pairing is known: side 1's gang is the entry, side 2's is shown under it.
        m_grid.addItem(kSide1Gang.name, 0);
        m_rival.setText(kSide2Gang.name);
        m_rival.centreOn(0.5F, kListTop + 2.0F * kRowGap, kBoxWidth);
        m_rival.style().colour = kGrey;
        break;
    case RumbleScreen::ChooseArea:
        for (std::size_t i = 0; i < kAreas.size(); ++i) {
            m_grid.addItem(kAreas.at(i).name, static_cast<int>(i));
        }
        break;
    }
    m_grid.select(selected < m_grid.items() ? selected : 0);

    m_title.setText(screenName());
    m_title.centreOn(0.5F, kTitleY, kBoxWidth);
    m_title.style().fontSlot = kBigFontSlot;
    m_grid.takeFocus(m_input, nowMs);
}

RumbleMenuResult RumbleMenu::confirm(int code, std::size_t connectedPads, RumbleSetup& setup, std::uint64_t nowMs) {
    switch (m_screen) {
    case RumbleScreen::GameMode:
        m_mode = static_cast<std::size_t>(code);
        applyMode(kModes.at(m_mode), setup);
        show(RumbleScreen::GameType, 0, nowMs);
        break;
    case RumbleScreen::GameType: {
        // Versus and co-op need a second player's pad.
        const auto players = static_cast<std::uint16_t>(code);
        if (players != kOnePlayer && connectedPads < 2) {
            break;
        }
        applyPlayers(players, setup);
        show(RumbleScreen::ChooseGangs, 0, nowMs);
        break;
    }
    case RumbleScreen::ChooseGangs:
        applyGangs(kSide1Gang, kSide2Gang, setup);
        show(RumbleScreen::ChooseArea, 0, nowMs);
        break;
    case RumbleScreen::ChooseArea:
        applyArea(kAreas.at(static_cast<std::size_t>(code)), setup);
        return RumbleMenuResult::Started;
    }
    return RumbleMenuResult::Stay;
}

} // namespace coney::gui
