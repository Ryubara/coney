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
#include "graphics/font.h"
#include "graphics/render_device.h"
#include "gui/colour_table.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// The pack a side gets when the mode has preset fighters and no gang is chosen.
constexpr std::uint16_t kNoPack = 255;

// The players (`gameMode`) of Game Type entry `id`: 0 one player, 1 co-op, 2 versus.
constexpr std::array<std::uint16_t, RumbleModeEntry::kPlayerOptions> kPlayersOfEntry{kRumbleOnePlayer, kRumbleCoop,
                                                                                     kRumbleVersus};

// The background's three colours, in the order of the cycle (`0x0063ee70`-`78`).
constexpr std::array<graphics::Rgba, 3> kBackgroundColours{
    graphics::Rgba{150, 50, 50, 255}, graphics::Rgba{50, 150, 50, 255}, graphics::Rgba{50, 50, 150, 255}};

// A Game Mode entry's text (`0x00557880`): the title large in big_font, the description under it.
std::string modeEntryText(const RumbleModeEntry& mode) {
    return "<SIZE 1.6><BIGFONT>" + mode.title + "</BIGFONT><SIZE 1.3><CR><SIZE 1.0>" + mode.description;
}

// A Game Type entry: centred in the one-row grid, dim grey, size 1.15.
OptionGridItem playerItem(std::string_view text, int code) {
    return OptionGridItem{
        .text = std::string(text), .code = code, .colour = kDimGrey, .scale = RumbleMenu::kEntryScale};
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

// One channel blended from `from` to `to` by `t` (0 to 1).
std::uint8_t blend(std::uint8_t from, std::uint8_t to, float t) {
    return static_cast<std::uint8_t>(static_cast<float>(from) +
                                     (static_cast<float>(to) - static_cast<float>(from)) * t);
}

} // namespace

RumbleLayout RumbleLayout::forFlags(bool flag02, bool widescreen, bool flag20) {
    // The table's columns: default, 0x04, 0x20, 0x02, 0x02 + 0x04.
    if (flag20) {
        return RumbleLayout{0.74F, 0.74F, 0.08F, 0.84F, 0.805F, 0.91F};
    }
    if (flag02) {
        return widescreen ? RumbleLayout{1.55F, 0.95F, 0.14F, 0.805F, 0.76F, 0.86F}
                          : RumbleLayout{1.1F, 0.8F, 0.18F, 0.77F, 0.74F, 0.82F};
    }
    if (widescreen) {
        return RumbleLayout{1.45F, 1.2F, 0.03F, 0.88F, 0.83F, 0.96F};
    }
    return RumbleLayout{};
}

graphics::Rgba rumbleBackgroundColour(std::uint64_t elapsedMs) {
    // Which leg of the cycle, and how far along it.
    const std::uint64_t leg = (elapsedMs / kRumbleBackgroundLegMs) % kBackgroundColours.size();
    const float t = static_cast<float>(elapsedMs % kRumbleBackgroundLegMs) / static_cast<float>(kRumbleBackgroundLegMs);
    const graphics::Rgba from = kBackgroundColours.at(leg);
    const graphics::Rgba to = kBackgroundColours.at((leg + 1) % kBackgroundColours.size());
    return graphics::Rgba{blend(from.r, to.r, t), blend(from.g, to.g, t), blend(from.b, to.b, t), 255};
}

RumbleMenu::RumbleMenu(RumbleMenuServices services)
    : m_services(std::move(services)), m_background(m_services.background, kBackgroundRect) {
    CONEY_ASSERT(m_services.state != nullptr && m_services.data != nullptr && m_services.strings != nullptr);
}

void RumbleMenu::start(std::uint64_t nowMs) {
    m_mode = 0;
    m_launchPending.reset();
    m_startMs = nowMs;
    // The background (controller Init): centred, the layout's height, its width from the picture's shape with the
    // aspect fix (the update replaces the layout's width so).
    m_background.setup(BaseWidgetSetup{.x = 0.5F,
                                       .y = 0.5F,
                                       .height = m_services.layout.backgroundHeight,
                                       .colour = rumbleBackgroundColour(0),
                                       .aspectFix = true});
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

std::size_t RumbleMenu::selectedEntry() const {
    return m_screen == RumbleScreen::GameType ? m_grid.selected() : m_list.selected();
}

const TextWidget& RumbleMenu::entryText(std::size_t row) const {
    CONEY_ASSERT(row < m_entryTexts.size());
    return m_entryTexts.at(row);
}

std::vector<int> RumbleMenu::takeCues() { return std::exchange(m_cues, {}); }

RumbleMenuResult RumbleMenu::update(const GuiFrame& frame, std::size_t connectedPads) {
    // The background's colour cycle runs on whatever screen is up.
    m_background.setColour(rumbleBackgroundColour(frame.timeMs - m_startMs));

    // The arena confirmed on the last update is launched now, before any input (the Choose Area update, 0x001ebc10).
    if (const std::optional<std::size_t> pending = std::exchange(m_launchPending, std::nullopt)) {
        launchArena(*pending);
        return RumbleMenuResult::Started;
    }

    // The Game Type message goes after its time.
    if (m_screen == RumbleScreen::GameType && m_messageUntilMs != 0 && frame.timeMs >= m_messageUntilMs) {
        m_messageUntilMs = 0;
        m_message.setText({});
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
    m_usage.update(frame);
    m_message.update(frame);
    for (TextWidget& name : m_gangNames) {
        name.update(frame);
    }
    for (TextWidget& entry : m_entryTexts) {
        entry.update(frame);
    }
    m_grid.update(frame);
    return result;
}

void RumbleMenu::render(const GuiCanvas& canvas) {
    m_background.render(canvas);
    m_title.render(canvas);
    m_usage.render(canvas);
    switch (m_screen) {
    case RumbleScreen::GameType:
        m_grid.render(canvas);
        m_message.render(canvas);
        break;
    case RumbleScreen::ChooseGangs:
        for (const TextWidget& name : m_gangNames) {
            name.render(canvas);
        }
        break;
    case RumbleScreen::GameMode:
    case RumbleScreen::ChooseArea: {
        // The shown entries stacked, the block centred on the screen's middle (Game Mode: its top at 0.5 − h / 2 +
        // 0.02, `0x001f8f98`); each entry's y is its first line's centre.
        const std::size_t shown = m_list.visibleCount();
        std::array<float, kModeEntriesShown> heights{};
        float total = 0.0F;
        for (std::size_t row = 0; row < shown; ++row) {
            heights.at(row) = m_entryTexts.at(row).layout(canvas).height;
            total += heights.at(row) + (row > 0 ? kModeEntryGap : 0.0F);
        }
        float top = 0.5F - total / 2.0F + (m_screen == RumbleScreen::GameMode ? 0.02F : 0.0F);
        for (std::size_t row = 0; row < shown; ++row) {
            TextWidget& entry = m_entryTexts.at(row);
            // A Game Mode entry's first line is its title at size 1.6.
            const float firstLineHeight =
                graphics::fontMetrics(entry.style().scale * (m_screen == RumbleScreen::GameMode ? 1.6F : 1.0F)).height;
            entry.style().y = top + firstLineHeight / 2.0F;
            entry.render(canvas);
            top += heights.at(row) + kModeEntryGap;
        }
        break;
    }
    }
}

void RumbleMenu::show(RumbleScreen screen, std::size_t selected, std::uint64_t nowMs) {
    m_screen = screen;
    m_entries.clear();
    m_list.setup(ScrollingMenuSetup{}, 0);
    m_grid.init();
    m_message.setText({});
    m_usage.setText({});
    for (TextWidget& name : m_gangNames) {
        name.setText({});
    }
    for (TextWidget& entry : m_entryTexts) {
        entry.setText({});
    }

    // Every screen's title (centred, size 2.23, big_font, grey) and usage line (centred).
    m_title.init();
    m_title.setup(TextWidgetSetup{.x = 0.5F,
                                  .y = m_services.layout.titleY,
                                  .scale = kTitleScale,
                                  .colour = kMenuGrey,
                                  .alignment = TextAlignment::Centre,
                                  .fontSlot = kBigFontSlot});
    m_usage.init();
    m_usage.place(0.5F, m_services.layout.usageY, false);

    switch (screen) {
    case RumbleScreen::GameMode:
        m_title.setText(text(kGameModeTitle, screenName()));
        showModes();
        break;
    case RumbleScreen::GameType:
        m_title.setText(text(kGameTypeTitle, screenName()));
        showPlayers();
        break;
    case RumbleScreen::ChooseGangs:
        m_title.setText(text(kChooseGangsTitle, screenName()));
        showGangs();
        break;
    case RumbleScreen::ChooseArea:
        m_title.setText(text(kChooseAreaTitle, screenName()));
        showAreas();
        break;
    }

    // The list's selection and focus: the grid on Game Type, the ScrollingMenu on Game Mode and Choose Area.
    if (screen == RumbleScreen::GameType) {
        if (m_grid.items() > 0) {
            m_grid.select(selected < m_grid.items() ? selected : 0);
        }
        m_grid.takeFocus(m_input, nowMs);
    } else {
        m_list.select(selected);
        m_list.takeFocus(m_input, nowMs);
        refreshEntries();
    }
}

// @orig 0x001f85c0 RM_GameMode_Init (RM_GameMode.cpp)
void RumbleMenu::showModes() {
    m_services.data->modes.clear();
    m_services.runChunk(kRumbleModeChunk);
    for (const RumbleModeEntry& mode : m_services.data->modes) {
        m_entries.push_back(mode.title);
    }
    m_list.setup(ScrollingMenuSetup{.visible = kModeEntriesShown,
                                    .moveCue = kRumbleMoveCue,
                                    .playCue = [this](int cue) { m_cues.push_back(cue); }},
                 m_entries.size());
    setUsage(kUsageSelectBack);
}

// @orig 0x001fc5b0 RM_NumPlayers_Init (RM_NumPlayers.cpp)
void RumbleMenu::showPlayers() {
    constexpr std::array<std::uint32_t, RumbleModeEntry::kPlayerOptions> kLabels{kOnePlayerString, kCoopString,
                                                                                 kVersusString};
    // The entries the mode offers, in one centred row.
    std::vector<OptionGridItem> items;
    for (std::size_t id = 0; id < kLabels.size(); ++id) {
        if (setup().playerOptions.at(id)) {
            const std::string_view label = text(kLabels.at(id));
            m_entries.emplace_back(label);
            items.push_back(playerItem(label, static_cast<int>(id)));
        }
    }
    // A " : " between neighbours, as the message box's one-row choices show it.
    for (std::size_t i = 0; i + 1 < items.size(); ++i) {
        items.at(i).separator = true;
    }
    m_grid.setup(OptionGridSetup{.y = m_services.layout.gridY,
                                 .rows = {items.size()},
                                 .centreX = 0.5F,
                                 .moveCue = kRumbleMoveCue,
                                 .playCue = [this](int cue) { m_cues.push_back(cue); }});
    for (OptionGridItem& item : items) {
        m_grid.addItem(std::move(item));
    }
    // The message for player 2, hidden until a two-player confirm.
    m_message.init();
    m_message.setup(TextWidgetSetup{.x = 0.5F,
                                    .y = kMessageY,
                                    .scale = kEntryScale,
                                    .colour = kMenuRed,
                                    .alignment = TextAlignment::Centre,
                                    .fontSlot = kBigFontSlot});
    m_messageShown = false;
    m_messageUntilMs = 0;
    setUsage(m_grid.items() == 1 ? kUsageOneEntry : kUsageOneRow);
}

// @orig 0x001ecae0 RM_ChooseGangs_Init (RM_ChooseGangs.cpp)
void RumbleMenu::showGangs() {
    m_services.data->gangs.clear();
    m_services.runChunk(kRumbleGangChunk);
    m_gangs.start(m_services.data->gangs);
    // The names mid-way between the title's bottom and the usage line.
    const float titleBottom = m_services.layout.titleY + graphics::fontMetrics(kTitleScale).height / 2.0F;
    const float middle = (titleBottom + m_services.layout.usageY) / 2.0F;
    constexpr std::array<float, 3> kX{kGangNameX[0], kGangNameX[1], 0.5F};
    for (std::size_t i = 0; i < m_gangNames.size(); ++i) {
        m_gangNames.at(i).init();
        m_gangNames.at(i).setup(TextWidgetSetup{.x = kX.at(i),
                                                .y = middle,
                                                .scale = kGangNameScale,
                                                .colour = kMenuGrey,
                                                .alignment = TextAlignment::Centre,
                                                .fontSlot = kBigFontSlot});
    }
    m_gangNames[2].setText(text(kVersusLabel, "vs."));
    refreshGangs();
}

// @orig 0x001eb0c8 RM_ChooseArea_Init (RM_ChooseArea.cpp)
void RumbleMenu::showAreas() {
    m_services.data->arenas.clear();
    m_services.runChunk(kRumbleArenaChunk);
    for (const RumbleArenaEntry& arena : m_services.data->arenas) {
        m_entries.emplace_back(arenaLabel(*m_services.state, arena));
    }
    m_list.setup(ScrollingMenuSetup{.visible = kAreaEntriesShown,
                                    .moveCue = kRumbleMoveCue,
                                    .playCue = [this](int cue) { m_cues.push_back(cue); }},
                 m_entries.size());
    m_launchPending.reset();
    // Rows of three arenas: one arena, fewer than two rows, or more.
    constexpr std::size_t kArenasPerRow = 3;
    if (m_entries.size() == 1) {
        setUsage(kUsageOneEntry);
    } else if (m_entries.size() <= kArenasPerRow) {
        setUsage(kUsageOneRow);
    } else {
        setUsage(kUsageArenas);
    }
}

// Accept copies the entry into the set-up and opens the Game Type screen; back leaves the menu.
// @orig 0x001f8d80 RM_GameMode_OnInput (RM_GameMode.cpp)
RumbleMenuResult RumbleMenu::onModeInput(MenuCommand command, std::uint64_t nowMs) {
    if (command == MenuCommand::Back) {
        m_cues.push_back(kRumbleBackCue);
        return RumbleMenuResult::Cancelled;
    }
    const std::optional<std::size_t> chosen = m_list.handle(command, nowMs);
    refreshEntries();
    if (!chosen) {
        return RumbleMenuResult::Stay;
    }
    m_mode = *chosen;
    applyMode(m_services.data->modes.at(m_mode), setup());
    m_cues.push_back(kRumbleConfirmCue);
    show(RumbleScreen::GameType, 0, nowMs);
    return RumbleMenuResult::Stay;
}

// Accept writes the players: one player at once; co-op and versus first show the message, then need a second pad.
// @orig 0x001fd1c8 RM_NumPlayers_OnInput (RM_NumPlayers.cpp)
RumbleMenuResult RumbleMenu::onPlayersInput(MenuCommand command, std::size_t connectedPads, std::uint64_t nowMs) {
    if (command == MenuCommand::Back) {
        m_cues.push_back(kRumbleBackCue);
        show(RumbleScreen::GameMode, m_mode, nowMs);
        return RumbleMenuResult::Stay;
    }
    const std::optional<int> chosen = m_grid.handle(command);
    if (!chosen) {
        return RumbleMenuResult::Stay;
    }
    const auto id = static_cast<std::size_t>(*chosen);
    if (id != 0 && (!m_messageShown || connectedPads < 2)) {
        // The message for player 2 (and Coney's stand-in for the no-second-controller screen); no cue.
        m_messageShown = true;
        m_messageUntilMs = nowMs + kMessageMs;
        m_message.setText(text(kPlayerTwoString));
        return RumbleMenuResult::Stay;
    }
    m_cues.push_back(kRumbleConfirmCue);
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
        m_cues.push_back(kRumbleMoveCue);
        break;
    case MenuCommand::Down:
        m_gangs.move(1);
        m_cues.push_back(kRumbleMoveCue);
        break;
    case MenuCommand::Left:
    case MenuCommand::Right:
        if (setup().values.at(RumbleSetup::kGameMode) != kRumbleCoop) {
            m_gangs.rotate(command == MenuCommand::Left);
            m_cues.push_back(kRumbleMoveCue);
        }
        break;
    case MenuCommand::Accept:
        m_cues.push_back(kRumbleConfirmCue);
        if (m_gangs.lock()) {
            m_gangs.apply(setup());
            show(RumbleScreen::ChooseArea, 0, nowMs);
            return RumbleMenuResult::Stay;
        }
        break;
    case MenuCommand::Back:
        m_cues.push_back(kRumbleBackCue);
        if (!m_gangs.unlock()) {
            show(RumbleScreen::GameType, 0, nowMs);
            return RumbleMenuResult::Stay;
        }
        break;
    }
    refreshGangs();
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
    const std::optional<std::size_t> chosen = m_list.handle(command, nowMs);
    refreshEntries();
    if (chosen) {
        m_launchPending = chosen;
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

void RumbleMenu::refreshGangs() {
    // Each side's gang name; the side choosing grey, a locked or waiting side dim.
    for (std::size_t side = 0; side < RumbleGangChooser::kSides; ++side) {
        const RumbleGangEntry* gang = m_gangs.gang(side);
        TextWidget& name = m_gangNames.at(side);
        name.setText(gang != nullptr ? std::string_view(gang->name) : std::string_view{});
        const bool choosing = side == m_gangs.activeSide() && !m_gangs.locked(side);
        name.style().colour = choosing ? kMenuGrey : kDimGrey;
    }
    // The usage line: 0x18 in co-op or while side 2 is active, else 0x24.
    const bool coop = setup().values.at(RumbleSetup::kGameMode) == kRumbleCoop;
    setUsage(coop || m_gangs.activeSide() == 1 ? kUsageSelectBack : kUsageGangSides);
}

void RumbleMenu::refreshEntries() {
    // The visible window of the list: each entry's text, the selected one grey and the others dim.
    const std::size_t first = m_list.firstVisible();
    const std::size_t shown = m_list.visibleCount();
    const bool modes = m_screen == RumbleScreen::GameMode;
    for (std::size_t row = 0; row < m_entryTexts.size(); ++row) {
        TextWidget& entry = m_entryTexts.at(row);
        if (row >= shown) {
            entry.setText({});
            continue;
        }
        const std::size_t index = first + row;
        const std::string markup = modes ? modeEntryText(m_services.data->modes.at(index)) : m_entries.at(index);
        if (entry.text() != markup) {
            entry.init();
            entry.setup(TextWidgetSetup{.x = modes ? kModeEntryX : 0.5F,
                                        .y = 0.5F,
                                        .scale = 1.0F,
                                        .colour = kDimGrey,
                                        .alignment = modes ? TextAlignment::Left : TextAlignment::Centre,
                                        .fontSlot = modes ? kTextFontSlot : kBigFontSlot,
                                        .wrapWidth = modes ? kModeEntryWrap : 0.0F});
            entry.setText(markup);
        }
        entry.style().colour = index == m_list.selected() ? kMenuGrey : kDimGrey;
    }
}

void RumbleMenu::setUsage(std::uint32_t id) { m_usage.setLegend(text(id)); }

std::string_view RumbleMenu::text(std::uint32_t id, std::string_view fallback) const {
    const std::string_view found = m_services.strings->get(id);
    return found.empty() ? fallback : found;
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
