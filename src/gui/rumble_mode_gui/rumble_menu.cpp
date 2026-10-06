// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/rumble_mode_gui/rumble_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "graphics/render_device.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// The pack a side gets when the mode has preset fighters and no gang is chosen.
constexpr std::uint16_t kNoPack = 255;

// The players (`gameMode`) of Game Type entry `id`: 0 one player, 1 co-op, 2 versus.
constexpr std::array<std::uint16_t, RumbleModeEntry::kPlayerOptions> kPlayersOfEntry{kRumbleOnePlayer, kRumbleCoop,
                                                                                     kRumbleVersus};

// Where the screens put things, in GUI units: Coney's layout, in PM_Mode's style.
constexpr float kTitleY = 0.2F;
constexpr float kListTop = 0.35F;
constexpr float kRowGap = 0.015F; // added to the grid's row pitch
constexpr float kDetailY = 0.9F;
constexpr float kSide1Y = 0.45F;
constexpr float kSide2Y = 0.6F;
constexpr float kBoxWidth = 0.8F;
constexpr float kSelectedScale = 1.15F;
constexpr graphics::Rgba kGrey{160, 160, 160, 255};

// An entry of a screen's list: one a row, grey, in the text font (Coney's layout until the Rumble screens' own look).
OptionGridItem listItem(std::string_view text, int code) {
    return OptionGridItem{
        .text = std::string(text), .code = code, .colour = kGrey, .scale = 1.0F, .fontSlot = kTextFontSlot};
}

// The Game Mode confirm's copy of `mode` into `setup`: its id, its gang size, its title after a ':', its three player
// options and, for a mode with preset fighters, all nine members of each side.
void applyMode(const RumbleModeEntry& mode, RumbleSetup& setup) {
    setup.values.at(RumbleSetup::kGameType) = mode.mode;
    setup.values.at(RumbleSetup::kGangSize) = mode.gangSize;
    setup.modeLabel = ":" + mode.title;
    setup.playerOptions = mode.playerOptions;
    setup.presetGangs = mode.presets[0] != 0;
    if (setup.presetGangs) {
        for (std::size_t i = 0; i < RumbleSetup::kGangMembers; ++i) {
            setup.values.at(RumbleSetup::kGang1Types + i) = mode.presets[0];
            setup.values.at(RumbleSetup::kGang2Types + i) = mode.presets[1];
        }
    }
}

// The Game Type confirm's write: the players, and for a preset mode no packs.
void applyPlayers(std::uint16_t players, RumbleSetup& setup) {
    setup.values.at(RumbleSetup::kGameMode) = players;
    if (setup.presetGangs) {
        setup.values.at(RumbleSetup::kGang1Pak) = kNoPack;
        setup.values.at(RumbleSetup::kGang2Pak) = kNoPack;
    }
}

// The first Game Type entry `options` offers (0 one player, 1 co-op, 2 versus); nothing when it offers none.
std::optional<std::size_t> firstPlayerOption(const std::array<bool, RumbleModeEntry::kPlayerOptions>& options) {
    for (std::size_t id = 0; id < options.size(); ++id) {
        if (options.at(id)) {
            return id;
        }
    }
    return std::nullopt;
}

// The label of arena `arena`: its level record's title, or the level's name when the title is empty.
std::string_view arenaLabel(const GameState& state, const RumbleArenaEntry& arena) {
    const LevelRecord* record = state.levels.at(arena.levelIndex);
    if (record == nullptr) {
        return {};
    }
    return record->fourthName.empty() ? std::string_view(record->name) : std::string_view(record->fourthName);
}

} // namespace

RumbleMenu::RumbleMenu(RumbleMenuServices services) : m_services(std::move(services)) {
    CONEY_ASSERT(m_services.state != nullptr && m_services.data != nullptr && m_services.strings != nullptr);
}

void RumbleMenu::start(std::uint64_t nowMs) {
    m_mode = 0;
    m_launchPending.reset();
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

std::vector<int> RumbleMenu::takeCues() { return std::exchange(m_cues, {}); }

RumbleMenuResult RumbleMenu::update(const GuiFrame& frame, std::size_t connectedPads) {
    // The arena confirmed on the last update is launched now, before any input (the Choose Area update, 0x001ebc10).
    if (const std::optional<std::size_t> pending = std::exchange(m_launchPending, std::nullopt)) {
        launchArena(*pending);
        return RumbleMenuResult::Started;
    }

    // The Game Type message goes after its time.
    if (m_screen == RumbleScreen::GameType && m_messageUntilMs != 0 && frame.timeMs >= m_messageUntilMs) {
        m_messageUntilMs = 0;
        m_detail.setText({});
    }

    // This frame's command, handed to the screen's input.
    RumbleMenuResult result = RumbleMenuResult::Stay;
    if (const std::optional<MenuCommand> command =
            frame.pad != nullptr ? m_input.dispatch(*frame.pad, frame.timeMs) : std::nullopt) {
        switch (m_screen) {
        case RumbleScreen::GameMode:
            result = onModeInput(*command, frame.timeMs);
            break;
        case RumbleScreen::GameType:
            result = onPlayersInput(*command, connectedPads, frame.timeMs);
            break;
        case RumbleScreen::ChooseGangs:
            result = onGangsInput(*command, frame.timeMs);
            break;
        case RumbleScreen::ChooseArea:
            result = onAreaInput(*command, frame.timeMs);
            break;
        }
    }
    m_title.update(frame);
    m_side1.update(frame);
    m_detail.update(frame);
    m_grid.update(frame);
    return result;
}

void RumbleMenu::render(const GuiCanvas& canvas) const {
    m_title.render(canvas);
    m_grid.render(canvas);
    m_side1.render(canvas);
    m_detail.render(canvas);
}

void RumbleMenu::show(RumbleScreen screen, std::size_t selected, std::uint64_t nowMs) {
    // An empty list in PM_Mode's style; each screen fills it.
    m_screen = screen;
    m_grid.init();
    m_grid.setup(OptionGridSetup{.y = kListTop, .centreX = 0.5F, .rowGap = kRowGap});
    m_side1.setText({});
    m_detail.setText({});
    m_detail.centreOn(0.5F, kDetailY, kBoxWidth);
    m_detail.style().colour = kGrey;
    m_detail.style().scale = 1.0F;

    switch (screen) {
    case RumbleScreen::GameMode:
        showModes();
        break;
    case RumbleScreen::GameType:
        showPlayers();
        break;
    case RumbleScreen::ChooseGangs:
        showGangs();
        break;
    case RumbleScreen::ChooseArea:
        showAreas();
        break;
    }
    if (m_grid.items() > 0) {
        m_grid.select(selected < m_grid.items() ? selected : 0);
    }
    if (screen == RumbleScreen::GameMode && m_grid.items() > 0) {
        m_detail.setText(m_services.data->modes.at(m_grid.selected()).description);
    }

    m_title.setText(screenName());
    m_title.centreOn(0.5F, kTitleY, kBoxWidth);
    m_title.style().fontSlot = kBigFontSlot;
    m_grid.takeFocus(m_input, nowMs);
}

// @orig 0x001f85c0 RM_GameMode_Init (RM_GameMode.cpp)
void RumbleMenu::showModes() {
    m_services.data->modes.clear();
    m_services.runChunk(kRumbleModeChunk);
    const std::vector<RumbleModeEntry>& modes = m_services.data->modes;
    for (std::size_t i = 0; i < modes.size(); ++i) {
        m_grid.addItem(listItem(modes.at(i).title, static_cast<int>(i)));
    }
}

// @orig 0x001fc5b0 RM_NumPlayers_Init (RM_NumPlayers.cpp)
void RumbleMenu::showPlayers() {
    constexpr std::array<std::uint32_t, RumbleModeEntry::kPlayerOptions> kLabels{kOnePlayerString, kCoopString,
                                                                                 kVersusString};
    for (std::size_t id = 0; id < kLabels.size(); ++id) {
        if (setup().playerOptions.at(id)) {
            m_grid.addItem(listItem(m_services.strings->get(kLabels.at(id)), static_cast<int>(id)));
        }
    }
    m_messageShown = false;
    m_messageUntilMs = 0;
}

// @orig 0x001ecae0 RM_ChooseGangs_Init (RM_ChooseGangs.cpp)
void RumbleMenu::showGangs() {
    m_services.data->gangs.clear();
    m_services.runChunk(kRumbleGangChunk);
    m_gangs.start(m_services.data->gangs);
    refreshGangLines();
}

// @orig 0x001eb0c8 RM_ChooseArea_Init (RM_ChooseArea.cpp)
void RumbleMenu::showAreas() {
    m_services.data->arenas.clear();
    m_services.runChunk(kRumbleArenaChunk);
    const std::vector<RumbleArenaEntry>& arenas = m_services.data->arenas;
    for (std::size_t i = 0; i < arenas.size(); ++i) {
        m_grid.addItem(listItem(arenaLabel(*m_services.state, arenas.at(i)), static_cast<int>(i)));
    }
    m_launchPending.reset();
}

// Accept copies the entry into the set-up and opens the Game Type screen; back leaves the menu.
// @orig 0x001f8d80 RM_GameMode_OnInput (RM_GameMode.cpp)
RumbleMenuResult RumbleMenu::onModeInput(MenuCommand command, std::uint64_t nowMs) {
    if (command == MenuCommand::Back) {
        m_cues.push_back(kRumbleBackCue);
        return RumbleMenuResult::Cancelled;
    }
    const std::optional<int> chosen = m_grid.handle(command);
    if (m_grid.items() > 0) {
        m_detail.setText(m_services.data->modes.at(m_grid.selected()).description);
    }
    if (!chosen) {
        return RumbleMenuResult::Stay;
    }
    m_mode = static_cast<std::size_t>(*chosen);
    applyMode(m_services.data->modes.at(m_mode), setup());
    m_cues.push_back(kRumbleConfirmCue);
    show(RumbleScreen::GameType, 0, nowMs);
    return RumbleMenuResult::Stay;
}

// Accept writes the players: one player at once; co-op and versus first show the message, then need a second pad.
// @orig 0x001fd1c8 RM_NumPlayers_OnInput (RM_NumPlayers.cpp)
RumbleMenuResult RumbleMenu::onPlayersInput(MenuCommand command, std::size_t connectedPads, std::uint64_t nowMs) {
    if (command == MenuCommand::Back) {
        show(RumbleScreen::GameMode, m_mode, nowMs);
        return RumbleMenuResult::Stay;
    }
    const std::optional<int> chosen = m_grid.handle(command);
    if (!chosen) {
        return RumbleMenuResult::Stay;
    }
    const auto id = static_cast<std::size_t>(*chosen);
    if (id != 0 && (!m_messageShown || connectedPads < 2)) {
        // The message for player 2 (and Coney's stand-in for the no-second-controller screen).
        m_messageShown = true;
        m_messageUntilMs = nowMs + kMessageMs;
        m_detail.setText(m_services.strings->get(kPlayerTwoString));
        return RumbleMenuResult::Stay;
    }
    applyPlayers(kPlayersOfEntry.at(id), setup());
    show(setup().presetGangs ? RumbleScreen::ChooseArea : RumbleScreen::ChooseGangs, 0, nowMs);
    return RumbleMenuResult::Stay;
}

// Up and down choose the active side's gang, left and right its warchief (not in co-op), accept locks the side and,
// with both locked, writes the gangs; back unlocks, or with nothing locked returns to the Game Type screen.
// @orig 0x001ef7c0 RM_ChooseGangs_OnInput (RM_ChooseGangs.cpp)
RumbleMenuResult RumbleMenu::onGangsInput(MenuCommand command, std::uint64_t nowMs) {
    switch (command) {
    case MenuCommand::Up:
        m_gangs.move(-1);
        break;
    case MenuCommand::Down:
        m_gangs.move(1);
        break;
    case MenuCommand::Left:
    case MenuCommand::Right:
        if (setup().values.at(RumbleSetup::kGameMode) != kRumbleCoop) {
            m_gangs.rotate(command == MenuCommand::Left);
        }
        break;
    case MenuCommand::Accept:
        if (m_gangs.lock()) {
            m_gangs.apply(setup());
            show(RumbleScreen::ChooseArea, 0, nowMs);
            return RumbleMenuResult::Stay;
        }
        break;
    case MenuCommand::Back:
        if (!m_gangs.unlock()) {
            show(RumbleScreen::GameType, 0, nowMs);
            return RumbleMenuResult::Stay;
        }
        break;
    }
    refreshGangLines();
    return RumbleMenuResult::Stay;
}

// Accept marks the arena for the launch on the next update; back returns to the screen before.
// @orig 0x001eb9f8 RM_ChooseArea_OnInput (RM_ChooseArea.cpp)
RumbleMenuResult RumbleMenu::onAreaInput(MenuCommand command, std::uint64_t nowMs) {
    if (command == MenuCommand::Back) {
        m_cues.push_back(kRumbleBackCue);
        show(setup().presetGangs ? RumbleScreen::GameType : RumbleScreen::ChooseGangs, 0, nowMs);
        return RumbleMenuResult::Stay;
    }
    if (const std::optional<int> chosen = m_grid.handle(command)) {
        m_launchPending = static_cast<std::size_t>(*chosen);
        m_cues.push_back(kRumbleConfirmCue);
    }
    return RumbleMenuResult::Stay;
}

// @orig 0x001ebb90 RM_ChooseArea_Launch (RM_ChooseArea.cpp)
void RumbleMenu::launchArena(std::size_t entry) {
    const RumbleArenaEntry& arena = m_services.data->arenas.at(entry);
    // The level number comes from the arena's level record, as the original reads it.
    const LevelRecord* record = m_services.state->levels.at(arena.levelIndex);
    setup().levelNumber = record != nullptr ? static_cast<int>(record->number) : arena.levelNumber;
}

void RumbleMenu::refreshGangLines() {
    // Side 1's gang above side 2's; the side choosing is white and larger, a locked or waiting side grey.
    const std::array<TextWidget*, RumbleGangChooser::kSides> lines{&m_side1, &m_detail};
    constexpr std::array<float, RumbleGangChooser::kSides> kLineY{kSide1Y, kSide2Y};
    for (std::size_t side = 0; side < lines.size(); ++side) {
        const RumbleGangEntry* gang = m_gangs.gang(side);
        TextWidget& line = *lines.at(side);
        line.setText(gang != nullptr ? std::string_view(gang->name) : std::string_view{});
        line.centreOn(0.5F, kLineY.at(side), kBoxWidth);
        const bool choosing = side == m_gangs.activeSide() && !m_gangs.locked(side);
        line.style().colour = choosing ? graphics::kWhite : kGrey;
        line.style().scale = choosing ? kSelectedScale : 1.0F;
    }
}

std::optional<RumbleSetup> rumbleMenuDefaults(const RumbleMenuServices& services, int levelNumber) {
    CONEY_ASSERT(services.state != nullptr && services.data != nullptr);
    RumbleSetup& setup = services.state->rumble;
    setup = RumbleSetup{};

    // The Game Mode screen's first entry and the Game Type screen's first.
    services.data->modes.clear();
    services.runChunk(kRumbleModeChunk);
    if (services.data->modes.empty()) {
        return std::nullopt;
    }
    applyMode(services.data->modes.front(), setup);
    const std::optional<std::size_t> players = firstPlayerOption(setup.playerOptions);
    if (!players) {
        return std::nullopt;
    }
    applyPlayers(kPlayersOfEntry.at(*players), setup);

    // Each side's first gang, as the gang screen starts them, unless the mode has preset fighters.
    if (!setup.presetGangs) {
        services.data->gangs.clear();
        services.runChunk(kRumbleGangChunk);
        if (services.data->gangs.empty()) {
            return std::nullopt;
        }
        RumbleGangChooser chooser;
        chooser.start(services.data->gangs);
        // Side 1's lock passes to side 2; side 2's completes the pair.
        chooser.lock();
        chooser.lock();
        chooser.apply(setup);
    }
    setup.levelNumber = levelNumber;
    return setup;
}

} // namespace coney::gui
