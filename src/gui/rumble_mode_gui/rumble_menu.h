// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "gui/rumble_mode_gui/rumble_gang_chooser.h"
#include "gui/text_widget.h"
#include "gui/widget.h"
#include "warriors/game_state.h"

namespace coney::gui {

/// The Rumble menu's screens, in the order QUICK RUMBLE shows them.
enum class RumbleScreen : std::uint8_t {
    GameMode,    ///< The modes `rumble_data.lua` lists: writes the mode's id, the gang size and its options.
    GameType,    ///< One player against the computer, co-op or versus: writes the players.
    ChooseGangs, ///< Each side's gang and warchief: writes the gangs' packs, character types and names.
    ChooseArea,  ///< The arenas `rumble_arena.lua` lists for the mode: launches the chosen one.
};

/// What a frame of the Rumble menu ended with.
enum class RumbleMenuResult : std::uint8_t {
    Stay,      ///< Still on a screen.
    Cancelled, ///< Backed out of the first screen.
    Started,   ///< The arena was launched: the set-up is complete.
};

/// The players (the set-up's `gameMode`) each Game Type entry writes (docs/research/frontend.md#rumble-data).
inline constexpr std::uint16_t kRumbleOnePlayer = 3; ///< Entry 0: one player against the computer.
inline constexpr std::uint16_t kRumbleCoop = 2;      ///< Entry 1: two players together.
inline constexpr std::uint16_t kRumbleVersus = 1;    ///< Entry 2: two players against each other.

/// The front-end sound cues the screens ask for (docs/research/frontend.md#rumble-data); Coney logs them.
inline constexpr int kRumbleConfirmCue = 8;
inline constexpr int kRumbleBackCue = 0xf;

/// What the Rumble menu works on; each must outlive the menu.
struct RumbleMenuServices {
    GameState* state = nullptr;             ///< The set-up the screens write, the unlocks and the level table.
    RumbleData* data = nullptr;             ///< The lists the chunks build.
    const GlobalStrings* strings = nullptr; ///< The Game Type screen's entries and message (HUD strings).
    /// Runs a chunk such as `rumble_data.lua` in the script state, whose `CfgRumble*` bindings add to `data`.
    std::function<void(std::string_view chunk)> runChunk;
};

/// The Rumble menu's screens (`RumbleModeGUI/`), the content of mode 0x11: Game Mode, Game Type, Choose Gangs and
/// Choose Area. Each screen that has a list builds it when it opens by running its chunk from the disc
/// (`rumble_data.lua`, `rumble_gang.lua`, `rumble_arena.lua`), whose `CfgRumble*` calls add what is unlocked; the text
/// shown is the chunks' and the HUD strings'. Each confirm writes its part of the set-up the arena reads; back returns
/// to the screen before, and from the first screen cancels the menu.
///
/// What the page gives, and Coney follows: the chunks, the records, the unlock checks and the stand-in types; the Game
/// Type entries (HUD strings `0x34`-`0x36`, each only when the mode offers it, writing 3, 2 and 1); the message
/// (`0x77`, 1.5 s) the first two-player confirm shows; a preset mode skipping the gangs (packs 255); the gang screen's
/// two sides, rotation and locking; the arena's launch one update after its confirm; the cues.
///
/// **Coney's choices** where it is silent: the layout (a title, the list centred, the selected entry larger and white,
/// PM_Mode's style; the mode's description and the gang screen's two sides as lines under it); the titles are the
/// screens' names as the page gives them; an arena's label is its level record's title (`+0x49`, "Fight Pen" for
/// `level102`), since the page does not say where the screen's text comes from; a second two-player confirm with no
/// second pad stays on the screen and shows the message again (the page's screen 4 is not researched); the HUD
/// player's pad drives both sides of the gang screen; back from a later screen returns to the one before with its
/// first entry selected (the Game Mode screen keeps the mode chosen).
///
/// Research: docs/research/frontend.md#rumble-setup, docs/research/frontend.md#rumble-data
class RumbleMenu {
  public:
    /// How long the Game Type screen's message stays, in milliseconds.
    static constexpr std::uint64_t kMessageMs = 1500;
    /// The HUD strings of the Game Type screen: its three entries and its message.
    static constexpr std::uint32_t kOnePlayerString = 0x34;
    static constexpr std::uint32_t kCoopString = 0x35;
    static constexpr std::uint32_t kVersusString = 0x36;
    static constexpr std::uint32_t kPlayerTwoString = 0x77;

    /// A menu over `services` (CONEY_ASSERT that the state, the data and the strings are set).
    explicit RumbleMenu(RumbleMenuServices services);

    /// Shows the Game Mode screen with its first entry selected, taking the focus at game time `nowMs`.
    void start(std::uint64_t nowMs);

    /// One frame: launches an arena confirmed the frame before, reads the command `frame.pad` gives and acts on it (a
    /// confirm writes into the state's set-up), and advances the texts. `connectedPads` decides whether two players can
    /// be chosen.
    [[nodiscard]] RumbleMenuResult update(const GuiFrame& frame, std::size_t connectedPads);

    /// Adds the current screen's sprites to `canvas`.
    void render(const GuiCanvas& canvas) const;

    /// The screen on top.
    [[nodiscard]] RumbleScreen screen() const { return m_screen; }
    /// The screen's name, for logs and the title: "Game Mode", "Game Type", "Choose Gangs" or "Choose Area".
    [[nodiscard]] std::string_view screenName() const;
    /// The current screen's entries (empty on the gang screen, which shows its two sides instead).
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }
    /// The gang screen's state.
    [[nodiscard]] const RumbleGangChooser& gangs() const { return m_gangs; }
    /// The line under the list: the selected mode's description, the Game Type message or the gang screen's side 2.
    [[nodiscard]] const TextWidget& detail() const { return m_detail; }
    /// Takes the sound cues asked for since the last call, oldest first.
    [[nodiscard]] std::vector<int> takeCues();

  private:
    // Builds `screen`, selecting entry `selected`, and takes the focus at `nowMs`.
    void show(RumbleScreen screen, std::size_t selected, std::uint64_t nowMs);
    // The Game Mode screen: runs the mode chunk and lists the modes.
    void showModes();
    // The Game Type screen: the entries the chosen mode offers.
    void showPlayers();
    // The Choose Gangs screen: runs the gang chunk and starts both sides.
    void showGangs();
    // The Choose Area screen: runs the arena chunk for the chosen mode and lists the arenas.
    void showAreas();
    // Each screen's input: returns the frame's result.
    RumbleMenuResult onModeInput(MenuCommand command, std::uint64_t nowMs);
    RumbleMenuResult onPlayersInput(MenuCommand command, std::size_t connectedPads, std::uint64_t nowMs);
    RumbleMenuResult onGangsInput(MenuCommand command, std::uint64_t nowMs);
    RumbleMenuResult onAreaInput(MenuCommand command, std::uint64_t nowMs);
    // Writes the level number of arena list entry `entry`: the end of the menu.
    void launchArena(std::size_t entry);
    // Shows the gang screen's two sides in the title lines.
    void refreshGangLines();
    // The set-up the screens write.
    [[nodiscard]] RumbleSetup& setup() { return m_services.state->rumble; }

    RumbleMenuServices m_services;
    MenuInput m_input;
    TextWidget m_title;
    TextWidget m_side1;  // the gang screen's side 1
    TextWidget m_detail; // the mode's description, the Game Type message, or the gang screen's side 2
    OptionGrid m_grid;
    RumbleGangChooser m_gangs;
    RumbleScreen m_screen = RumbleScreen::GameMode;
    std::size_t m_mode = 0;                     // the mode list entry chosen on the Game Mode screen
    bool m_messageShown = false;                // `+0x90`: the Game Type screen showed its message
    std::uint64_t m_messageUntilMs = 0;         // when the message goes
    std::optional<std::size_t> m_launchPending; // `+0xc4`: the arena confirmed, launched on the next update
    std::vector<int> m_cues;
};

/// The set-up the Rumble menu leaves when every screen's first entry is accepted, for a level run without the menu
/// (`--play-level` of an arena): runs the mode and gang chunks through `services` as the screens would, takes the
/// first mode, its first Game Type entry, each side's first gang (side 1 the list's first, side 2 its second) and the
/// arena `levelNumber`. Nothing when the chunks list no mode or, for a mode without presets, no gang. Writes into
/// `services.state->rumble` as the screens do, and returns it.
[[nodiscard]] std::optional<RumbleSetup> rumbleMenuDefaults(const RumbleMenuServices& services, int levelNumber);

} // namespace coney::gui
