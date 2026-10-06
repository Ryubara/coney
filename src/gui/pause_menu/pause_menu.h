// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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
#include "gui/pause_menu/pause_items.h"
#include "gui/pause_menu/yes_no_box.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"
#include "gui/widget.h"

namespace coney::gui {

/// What the pause menu was closed with: what `PauseMenu_Toggle` acts on after it pops the mode (the menu's choice
/// fields `+0x1b70`-`+0x1b9c`).
enum class PauseOutcome : std::uint8_t {
    Resume,              ///< Nothing chosen: back to play.
    RestartLevel,        ///< `+0x1b74`: the level from checkpoint 1.
    RestartCheckpoint,   ///< `+0x1b78`: the level from the current checkpoint.
    QuitToHangout,       ///< `+0x1b70` with `+0x1b94`: Lua `runNextMission(0)`.
    QuitToMainMenu,      ///< `+0x1b70` alone or with `+0x1b9c`: the front end.
    QuitToRumbleQuick,   ///< `+0x1b98`: Lua `PauseGoToRMIQuick`.
    QuitToRumbleHangout, ///< `+0x1b98`: Lua `PauseGoToRMIHangout`.
};

/// Which screen of the pause menu fills the space above the grid.
enum class PauseScreen : std::uint8_t {
    Grid,       ///< Only the grid (and the mission's title).
    Objectives, ///< `+0x1ba8`: the three objective lists.
    Options,    ///< `+0x3a78`.
    Controls,   ///< `+0x3a7c`.
    Restart,    ///< `+0x1bb8`: Restart Level / Restart Last Check Point.
    Quit,       ///< `+0x1bb4`: the two quit destinations.
};

/// What the pause menu is opened with.
struct PauseMenuSetup {
    int level = 0;              ///< The level's number (`level99` is 99).
    bool openOnOptions = false; ///< The game state's `+0x11c` (`ShowOptionMenu`): open on the Options screen.
    /// Which Rumble quit `0x001fe1a8` picks (`PauseGoToRMIHangout` when set, else `PauseGoToRMIQuick`), and whether
    /// the Rumble Quit screen offers To Hangout (set) or To Main Menu. Coney's stand-in: the caller's guess.
    bool rumbleFromHangout = false;
    std::array<std::vector<std::string>, 3> objectives{}; ///< Current, bonus and overview lists (`HUDSetObjective`).
};

/// The pause menu (the story one, `0x0062e790`): the menu mode 0xa opens over the paused game.
///
/// - **Opening** (open()): the world is tinted to black over kTintMs (worldTint()); the background fades in over
///   kBackgroundFadeMs while its colour cycles (150, 50, 50) → (50, 50, 150) → (50, 150, 50) → … over kCycleLegMs a
///   leg; the top header shows the mission's title; the grid (pauseGrid()) is centred on x 0.5 from y 0.86 with the
///   first item selected; the usage line is at (0.5, 0.97).
/// - **Input** (update()): while closing nothing is taken. START closes from anywhere once kStartDelayMs have passed
///   since opening. Every other command goes to the Yes/No box when it is open, else to the open screen (back
///   returns to the grid; up and down move the screen's selection, cue 4 or 0xe at an end; accept acts), else to the
///   grid: accept plays cue 8 and selects the item, back plays cue 0xf and resumes, moves are the grid's.
/// - **Items**: Objectives opens the lists (selection 0, or 2 in a Rumble level); Stats opens nothing (see below);
///   Options and Controls open their screens; Restart opens Restart Level / Restart Last Check Point (the second
///   selected), whose accept asks 0x105 or 0x103, and in a Rumble level Replay asks 0x105 at once; Resume closes; Quit
///   asks 0x102, after a choice of two destinations when the level offers the hangout or is a Rumble level. Yes to a
///   question closes with that outcome; No and back close the box and forget the choice.
/// - **Closing**: the menu's alpha falls from 255 to 0 over kFadeOutMs, and kHoldMs after it reaches 0 the menu is
///   closed() and outcome() says what to do.
///
/// Coney's stand-ins (docs/research/pause.md#coneys-implementation): the Stats screen is never available (Coney has no
/// character stats), so Stats opens nothing; the Options and Controls screens are lists of their entries' names that
/// do nothing on accept (`OptionMenu.cpp`, `ControlMenuHUD.cpp` not researched); the headers (`CircledTextHeader`),
/// the objective lists and the Restart and Quit screens are centred texts at Coney's positions and colours; the menu
/// waits for no sound before it closes; the co-op quit (Player 2 / All) is not offered (Coney has one player).
///
/// Research: docs/research/pause.md
/// @orig 0x001db3c0 PauseMenu_Construct (unknown)
class PauseMenu : public Widget {
  public:
    /// The world's tint to black when the menu opens (`0x0050ee5c`, 1.4 s).
    static constexpr std::uint64_t kTintMs = 1400;
    /// The background's fade-in.
    static constexpr std::uint64_t kBackgroundFadeMs = 3000;
    /// One leg of the background's colour cycle (`0x0050ee88`).
    static constexpr std::uint64_t kCycleLegMs = 5000;
    /// The background's three colours (`0x00635380`).
    static constexpr std::array<graphics::Rgba, 3> kCycleColours{{
        {150, 50, 50, 255},
        {50, 50, 150, 255},
        {50, 150, 50, 255},
    }};
    /// START closes the menu only this long after it opened (`0x0050ee8c`).
    static constexpr std::uint64_t kStartDelayMs = 1500;
    /// The menu's fade out (`0x0050ee80`) and the hold after it (`0x0050ee84`).
    static constexpr std::uint64_t kFadeOutMs = 1500;
    static constexpr std::uint64_t kHoldMs = 500;
    /// The background: sheet record 12 (Coney loads it by its name hash) rectangle 5, size 1.05, depth 7,000.
    static constexpr std::size_t kBackgroundRect = 5;
    static constexpr float kBackgroundSize = 1.05F;
    static constexpr float kBackgroundDepth = 7000.0F;
    /// The headers' y (current, bonus, overview).
    static constexpr std::array<float, 3> kHeaderY{0.22F, 0.46F, 0.68F};
    /// The grid: first row's y, font scale, font slot.
    static constexpr float kGridY = 0.86F;
    static constexpr float kGridScale = 1.15F;
    static constexpr int kGridFontSlot = 3;
    /// The usage line's position.
    static constexpr float kUsageX = 0.5F;
    static constexpr float kUsageY = 0.97F;
    /// Cues: a move in a screen, a refused move, accept, back.
    static constexpr int kMoveCue = 4;
    static constexpr int kRefusedCue = 0xe;
    static constexpr int kAcceptCue = 8;
    static constexpr int kBackCue = 0xf;

    /// Shows strings from `strings` (which must outlive the menu; null shows empty texts).
    void setStrings(const GlobalStrings* strings) { m_strings = strings; }
    /// Sends front-end sound cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue);
    /// Draws the background through `batch` (not owned; null: no background).
    void setBackgroundBatch(graphics::SpriteBatch* batch) { m_background.setBatch(batch); }

    /// Opens the menu at game time `nowMs` as `setup` says.
    /// @orig 0x001dbee0 PauseMenu_Open (unknown)
    void open(const PauseMenuSetup& setup, std::uint64_t nowMs);

    /// One frame: the background, the closing fade, then the input from `frame`'s pad (the pausing player's).
    /// @orig 0x001ddcf8 PauseMenu_Update (unknown)
    void update(const GuiFrame& frame) override;

    /// Draws the background, the headers, the open screen, the Yes/No box, the grid and the usage line.
    /// @orig 0x001df700 PauseMenu_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

    /// How far the world is tinted to black at `nowMs`, 0 to 1.
    [[nodiscard]] float worldTint(std::uint64_t nowMs) const;

    /// Whether the menu is fading out (`+0x1b80`).
    [[nodiscard]] bool closing() const { return m_closing; }
    /// Whether the fade out and its hold are over (`+0x1b7c`): the mode should pop.
    [[nodiscard]] bool closed() const { return m_closed; }
    /// What the menu was closed with.
    [[nodiscard]] PauseOutcome outcome() const { return m_outcome; }
    /// The open screen.
    [[nodiscard]] PauseScreen screen() const { return m_screen; }
    /// The selection inside the open screen (`+0x3a74`).
    [[nodiscard]] std::size_t selection() const { return m_selection; }
    /// The grid.
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }
    /// The Yes/No box.
    [[nodiscard]] const YesNoBox& yesNo() const { return m_yesNo; }
    /// The usage line.
    [[nodiscard]] const UsageInfo& usage() const { return m_usage; }
    /// The background widget.
    [[nodiscard]] const BaseWidget& background() const { return m_background; }
    /// The header widgets (current, bonus, overview).
    [[nodiscard]] const std::array<TextWidget, 3>& headers() const { return m_headers; }
    /// The menu's alpha (`+0x3b9c`), 255 open.
    [[nodiscard]] float alpha() const { return m_alpha; }
    /// The texts of the open screen's lines, as drawn (for tests and the log).
    [[nodiscard]] std::vector<std::string> screenLines() const;

    /// The Options and Controls screens' entries (Coney's stand-in lists, docs/research/pause.md#the-items-screens).
    static constexpr std::array<std::string_view, 7> kOptionsEntries{
        "Lighting", "Camera", "Audio", "Video", "Subtitles", "Vibration", "Restore Default"};
    static constexpr std::array<std::string_view, 2> kControlsEntries{"Controller", "Tutorial"};

  private:
    // The quit destination the Quit screen offers at index i.
    enum class QuitChoice : std::uint8_t { Hangout, MainMenu, RumbleMode };

    // String `id`, or empty without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const;
    // Plays `cue`, if there is a sink.
    void playCue(int cue) const;
    // Starts the fade out at `nowMs` (`+0x1b80` = 1).
    void startClosing(std::uint64_t nowMs);
    // The fade-in, colour cycle and closing fade at `nowMs`.
    void animate(std::uint64_t nowMs);
    // One command without a screen or box open.
    void onGridCommand(MenuCommand command, std::uint64_t nowMs);
    // One command with a screen open.
    void onScreenCommand(MenuCommand command, std::uint64_t nowMs);
    // One command to the Yes/No box.
    void onBoxCommand(MenuCommand command, std::uint64_t nowMs);
    // Acts on grid item `code`.
    void selectItem(PauseItem item, std::uint64_t nowMs);
    // Opens `screen` with selection `selection`.
    void openScreen(PauseScreen screen, std::size_t selection);
    // Back to the grid.
    void closeScreen();
    // Opens the Yes/No box asking string `question`.
    void ask(std::uint32_t question);
    // How many entries the open screen's selection runs over.
    [[nodiscard]] std::size_t screenEntries() const;
    // Rebuilds the headers and screen lines for the open screen and selection.
    void refreshScreen();
    // Adds a screen line.
    void addLine(std::string_view text, float y, graphics::Rgba colour, int fontSlot, float scale);

    const GlobalStrings* m_strings = nullptr;
    std::function<void(int cue)> m_playCue;
    PauseMenuSetup m_setup;
    MenuInput m_input;
    BaseWidget m_background{nullptr, kBackgroundRect};
    std::array<TextWidget, 3> m_headers;
    std::vector<std::unique_ptr<TextWidget>> m_lines; // the open screen's lines; pointers, a widget cannot move
    OptionGrid m_grid;
    UsageInfo m_usage;
    YesNoBox m_yesNo;
    std::vector<QuitChoice> m_quitChoices;

    PauseScreen m_screen = PauseScreen::Grid;
    std::size_t m_selection = 0;
    PauseOutcome m_outcome = PauseOutcome::Resume;
    PauseOutcome m_pending = PauseOutcome::Resume; // the choice the open Yes/No box confirms
    std::uint64_t m_openMs = 0;                    // `+0x3ba4`, `+0x3b90`, `+0x3ba8`
    std::uint64_t m_fadeOutMs = 0;                 // `+0x3b98`
    float m_alpha = 255.0F;
    bool m_closing = false;
    bool m_closed = false;
    bool m_open = false;
};

} // namespace coney::gui
