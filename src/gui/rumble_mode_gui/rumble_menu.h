// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "gui/rumble_mode_gui/rumble_gang_chooser.h"
#include "gui/scrolling_menu.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"
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

/// The front-end sound cues the screens ask for (docs/research/frontend.md#rumble-screens).
inline constexpr int kRumbleMoveCue = 4;
inline constexpr int kRumbleConfirmCue = 8;
inline constexpr int kRumbleBackCue = 0xf;

/// The Rumble controller's layout values (`0x001fdce0`, `0x0050f4f8`-`0x0050f50c`), picked by video mode: GUI units
/// except the background's size, which is in overlay units.
///
/// Research: docs/research/frontend.md#rm-layout
struct RumbleLayout {
    float backgroundWidth = 1.15F; ///< `0x0050f4f8`; the update replaces it by the height × the picture's shape.
    float backgroundHeight = 1.1F; ///< `0x0050f4fc`.
    float titleY = 0.08F;          ///< `0x0050f500`.
    float gridY = 0.84F;           ///< `0x0050f504`: the Game Type and Choose Gangs grids.
    float untracedY = 0.805F;      ///< `0x0050f508`: not traced.
    float usageY = 0.91F;          ///< `0x0050f50c`.

    /// The column for the video flags: `flag20` first, then `flag02` (with or without 16:9), then 16:9, else the
    /// default, as the profile manager's layout picks.
    [[nodiscard]] static RumbleLayout forFlags(bool flag02, bool widescreen, bool flag20);
};

/// The background's colour `elapsedMs` of real time after the controller started: red (150, 50, 50) to green
/// (50, 150, 50) to blue (50, 50, 150) and back to red, each leg a linear blend over kRumbleBackgroundLegMs.
/// @orig 0x001f2468 RumbleController_CycleBackground (unknown)
[[nodiscard]] graphics::Rgba rumbleBackgroundColour(std::uint64_t elapsedMs);
/// How long each leg of the background's colour cycle lasts (`0x0050f3e4`).
inline constexpr std::uint64_t kRumbleBackgroundLegMs = 5000;

/// What the Rumble menu works on; each must outlive the menu.
struct RumbleMenuServices {
    GameState* state = nullptr;             ///< The set-up the screens write, the unlocks and the level table.
    RumbleData* data = nullptr;             ///< The lists the chunks build.
    const GlobalStrings* strings = nullptr; ///< The titles, usage lines, Game Type entries and message (HUD strings).
    /// Runs a chunk such as `rumble_data.lua` in the script state, whose `CfgRumble*` bindings add to `data`.
    std::function<void(std::string_view chunk)> runChunk;
    RumbleLayout layout{};                       ///< The layout values of the video mode.
    graphics::SpriteBatch* background = nullptr; ///< The background picture's batch (null: none drawn).
};

/// The Rumble menu's screens (`RumbleModeGUI/`), the content of mode 0x11: Game Mode, Game Type, Choose Gangs and
/// Choose Area. Each screen that has a list builds it when it opens by running its chunk from the disc
/// (`rumble_data.lua`, `rumble_gang.lua`, `rumble_arena.lua`), whose `CfgRumble*` calls add what is unlocked; the text
/// shown is the chunks' and the HUD strings'. Each confirm writes its part of the set-up the arena reads; back returns
/// to the screen before, and from the first screen cancels the menu.
///
/// **Look** (docs/research/frontend.md#rumble-screens): the cycling background picture (rectangle
/// kBackgroundRect, centred, its height from the layout, the aspect fix); every screen's title centred at the layout's
/// title y, size kTitleScale, `big_font`, kMenuGrey; its usage line centred at the layout's usage y. Game Mode is a
/// ScrollingMenu of at most three entries, each the mode's title (size 1.6, `big_font`) over its description, the
/// selected entry kMenuGrey and the others kDimGrey; Game Type a centred one-row OptionGrid at the layout's grid y,
/// size 1.15, kDimGrey, with message `0x77` at (0.5, kMessageY); Choose Gangs the two sides' gang names at x 0.2505 and
/// 0.7495 and "vs." between them, mid-way between the title and the usage line, size 1.2.
///
/// **Coney's choices** where the page is silent or a resource is not found yet: Game Mode's entries start at
/// kModeEntryX (where they are measured) and are stacked kModeEntryGap apart, without the wrap width, backdrops or
/// scroll arrows (sheet-table record 28 is not named); Choose Area lists its arenas' labels in a ScrollingMenu,
/// centred, instead of the framed preview pictures in rows of three; Choose Gangs has no name boxes, badges, arrows,
/// bars or 3D fighters, and its cursors keep RumbleGangChooser's rules; the Game Type entries are joined by `" : "` as
/// the message box's measured choices are; the message `0x77` is kMenuRed; a title string the strings do not hold falls
/// back to the screen's name. As before: an arena's label is its level record's title (`+0x49`); a second two-player
/// confirm with no second pad stays on the screen and shows the message again (the No 2nd Controller screen is not
/// made); the HUD player's pad drives both sides of the gang screen; back from a later screen returns to the one before
/// with its first entry selected (the Game Mode screen keeps the mode chosen).
///
/// Research: docs/research/frontend.md#rumble-setup, docs/research/frontend.md#rumble-data,
/// docs/research/frontend.md#rumble-screens
class RumbleMenu {
  public:
    /// How long the Game Type screen's message stays, in milliseconds.
    static constexpr std::uint64_t kMessageMs = 1500;
    /// The HUD strings of the Game Type screen: its three entries and its message.
    static constexpr std::uint32_t kOnePlayerString = 0x34;
    static constexpr std::uint32_t kCoopString = 0x35;
    static constexpr std::uint32_t kVersusString = 0x36;
    static constexpr std::uint32_t kPlayerTwoString = 0x77;
    /// The HUD strings of the titles, and Choose Gangs' "vs.".
    static constexpr std::uint32_t kGameModeTitle = 0x2c;
    static constexpr std::uint32_t kGameTypeTitle = 0x2e;
    static constexpr std::uint32_t kChooseGangsTitle = 0x2b;
    static constexpr std::uint32_t kChooseAreaTitle = 0x2a;
    static constexpr std::uint32_t kVersusLabel = 0x2f;
    /// The HUD strings of the usage lines (docs/research/frontend.md#rumble-screens gives which screen shows which).
    static constexpr std::uint32_t kUsageSelectBack = 0x18;
    static constexpr std::uint32_t kUsageOneEntry = 0x21;
    static constexpr std::uint32_t kUsageOneRow = 0x17;
    static constexpr std::uint32_t kUsageGangSides = 0x24;
    static constexpr std::uint32_t kUsageArenas = 0x1a;
    /// The sizes of the title, the Game Type entries and message, and the gang names.
    static constexpr float kTitleScale = 2.23F;
    static constexpr float kEntryScale = 1.15F;
    static constexpr float kGangNameScale = 1.2F;
    /// Where the Game Type message and the gang names sit.
    static constexpr float kMessageY = 0.78F;
    static constexpr std::array<float, 2> kGangNameX{0.2505F, 0.7495F};
    /// The Game Mode entries: how many show, where they start (Coney's choice, measured) and their gap (Coney's).
    static constexpr std::size_t kModeEntriesShown = 3;
    static constexpr float kModeEntryX = 0.20F;
    static constexpr float kModeEntryGap = 0.03F;
    /// The Game Mode entries' wrap width (`0x0050f464`).
    static constexpr float kModeEntryWrap = 0.66F;
    /// The Choose Area entries shown at once (Coney's list).
    static constexpr std::size_t kAreaEntriesShown = 3;
    /// The background picture's rectangle in its sheet.
    static constexpr std::size_t kBackgroundRect = 5;

    /// A menu over `services` (CONEY_ASSERT that the state, the data and the strings are set).
    explicit RumbleMenu(RumbleMenuServices services);
    ~RumbleMenu() = default;
    RumbleMenu(const RumbleMenu&) = delete;
    RumbleMenu& operator=(const RumbleMenu&) = delete;
    RumbleMenu(RumbleMenu&&) = delete;
    RumbleMenu& operator=(RumbleMenu&&) = delete;

    /// Shows the Game Mode screen with its first entry selected, taking the focus at game time `nowMs`; the
    /// background's colour cycle starts here.
    /// @orig 0x001f1e00 RumbleController_Init (unknown)
    void start(std::uint64_t nowMs);

    /// One frame: launches an arena confirmed the frame before, reads the command `frame.pad` gives and acts on it (a
    /// confirm writes into the state's set-up), and advances the texts and the background. `connectedPads` decides
    /// whether two players can be chosen.
    [[nodiscard]] RumbleMenuResult update(const GuiFrame& frame, std::size_t connectedPads);

    /// Adds the background and the current screen's sprites to `canvas` and the background's batch, placing the Game
    /// Mode entries with the canvas's fonts.
    void render(const GuiCanvas& canvas);

    /// The screen on top.
    [[nodiscard]] RumbleScreen screen() const { return m_screen; }
    /// The screen's name, for logs: "Game Mode", "Game Type", "Choose Gangs" or "Choose Area".
    [[nodiscard]] std::string_view screenName() const;
    /// The current screen's list, as shown: the mode titles, the Game Type entries or the arena labels (empty on the
    /// gang screen).
    [[nodiscard]] const std::vector<std::string>& entries() const { return m_entries; }
    /// The selected entry of the current screen's list.
    [[nodiscard]] std::size_t selectedEntry() const;
    /// The Game Type screen's entries.
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }
    /// The Game Mode and Choose Area screens' list.
    [[nodiscard]] const ScrollingMenu& list() const { return m_list; }
    /// The text of shown entry `row` (0 the first visible) of a ScrollingMenu screen (CONEY_ASSERT that it exists).
    [[nodiscard]] const TextWidget& entryText(std::size_t row) const;
    /// The gang screen's state.
    [[nodiscard]] const RumbleGangChooser& gangs() const { return m_gangs; }
    /// The title, the usage line, and the Game Type screen's message.
    [[nodiscard]] const TextWidget& title() const { return m_title; }
    [[nodiscard]] const UsageInfo& usage() const { return m_usage; }
    [[nodiscard]] const TextWidget& message() const { return m_message; }
    /// Draws the background picture into `batch` from now on (null: none); for a sheet loaded after the menu was made.
    void setBackgroundBatch(graphics::SpriteBatch* batch) { m_background.setBatch(batch); }
    /// The background picture.
    [[nodiscard]] const BaseWidget& background() const { return m_background; }
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
    // The gang screen's names and usage line, after a change of side or gang.
    void refreshGangs();
    // The ScrollingMenu screen's visible entries: their texts and colours.
    void refreshEntries();
    // Sets the usage line to HUD string `id`.
    void setUsage(std::uint32_t id);
    // HUD string `id`, or `fallback` when the strings do not hold it.
    [[nodiscard]] std::string_view text(std::uint32_t id, std::string_view fallback = {}) const;
    // The set-up the screens write.
    [[nodiscard]] RumbleSetup& setup() { return m_services.state->rumble; }

    RumbleMenuServices m_services;
    MenuInput m_input;
    BaseWidget m_background;
    TextWidget m_title;
    UsageInfo m_usage;
    TextWidget m_message;                  // the Game Type message
    std::array<TextWidget, 3> m_gangNames; // side 1, side 2 and "vs."
    std::array<TextWidget, kModeEntriesShown> m_entryTexts;
    OptionGrid m_grid;
    ScrollingMenu m_list;
    std::vector<std::string> m_entries;
    RumbleGangChooser m_gangs;
    RumbleScreen m_screen = RumbleScreen::GameMode;
    std::uint64_t m_startMs = 0;                // when the background's cycle started
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
