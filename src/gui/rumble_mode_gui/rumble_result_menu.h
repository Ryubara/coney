// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"
#include "gui/widget.h"

namespace coney::gui {

/// What the Rumble result screen ended with: the item the player accepted.
enum class RumbleResultChoice : std::uint8_t {
    Replay = 0,     ///< Id 0 (`+0x7a4`): the same match again.
    RumbleMenu = 2, ///< Id 2 (`+0x7a8`): the Rumble menu over the arena.
    Quit = 3,       ///< Id 3 (`+0x7ac`): the main menu, or the hangout in game.
};

/// The Rumble result screen (a `GameMenu`, `0x00635390`), which mode 0x14 shows when the arena script calls
/// `HUDLaunchRumbleWin(winner, reason)`.
///
/// - **Look** (open()): the winner line at (0.5, 0.35) (string 0xe0 when empty), the reason line at (0.5, 0.45), the
///   usage line 0x1c at y 0.6, and a choice grid at y 0.55: first **Replay** (0xdb, id 0) : **more** (0xdc, id 1), then
///   after more **Rumble menu** (0xdd, id 2) : **Quit** (0xde from the front end, 0xdf in game, id 3), items at scale
///   1.15 in grey `0x005fd320`.
/// - **Timing**: the choices appear kChoicesDelayMs after the screen opens and fade in over kChoicesFadeMs; input is
///   taken only after that.
/// - **Input**: accept (cue 8) on more swaps to the second grid; on the others the screen ends with that choice. Back
///   is ignored.
///
/// Coney's stand-ins (docs/research/rumble.md#open-questions): the two lines' font, scale and colour, the grids' rows
/// (one row of two, the first item with a separator) and the usage line's timing (it shows with the choices) are not on
/// the page: the lines are centred, white, in the text font at scale 1, the winner in big_font.
///
/// Research: docs/research/rumble.md#result-screen
/// @orig 0x001e0008 RumbleWin_Show (unknown)
class RumbleResultMenu : public Widget {
  public:
    /// The lines' and grid's places.
    static constexpr float kWinnerY = 0.35F;
    static constexpr float kReasonY = 0.45F;
    static constexpr float kGridY = 0.55F;
    static constexpr float kUsageY = 0.6F;
    /// The items' scale.
    static constexpr float kItemScale = 1.15F;
    /// The items' font slot: Coney's stand-in, the pause grid's.
    static constexpr int kGridFontSlot = 3;
    /// When the choices appear after the screen opens (`0x0050eec0`), and how long they fade in (`0x0050eeb8`).
    static constexpr std::uint64_t kChoicesDelayMs = 6500;
    static constexpr std::uint64_t kChoicesFadeMs = 2000;
    /// The accept cue.
    static constexpr int kAcceptCue = 8;
    /// The strings: no winner, the usage line, the items.
    static constexpr std::uint32_t kNoWinner = 0xe0;
    static constexpr std::uint32_t kUsage = 0x1c;
    static constexpr std::uint32_t kReplay = 0xdb;
    static constexpr std::uint32_t kMore = 0xdc;
    static constexpr std::uint32_t kRumbleMenu = 0xdd;
    static constexpr std::uint32_t kQuitFrontEnd = 0xde;
    static constexpr std::uint32_t kQuitInGame = 0xdf;
    /// The item ids.
    static constexpr int kReplayId = 0;
    static constexpr int kMoreId = 1;
    static constexpr int kRumbleMenuId = 2;
    static constexpr int kQuitId = 3;

    /// Shows strings from `strings` (which must outlive the menu; null shows empty texts).
    void setStrings(const GlobalStrings* strings) { m_strings = strings; }
    /// Sends front-end sound cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue) { m_playCue = std::move(playCue); }

    /// Opens the screen at `nowMs` with the two lines; `fromFrontEnd` picks the Quit item's text (`0x0063ef64`).
    /// @orig 0x001e0008 RumbleWin_Show (unknown)
    void open(std::string_view winner, std::string_view reason, bool fromFrontEnd, std::uint64_t nowMs);
    /// Hides the screen (mode 0x14's exit).
    void close() { m_open = false; }

    /// One frame: the choices' fade, then input from `frame`'s pad once they are in.
    /// @orig 0x001e0778 RumbleWin_Update (unknown)
    /// @orig 0x001e0460 RumbleWin_Input (unknown)
    void update(const GuiFrame& frame) override;
    /// Draws the lines, then the grid and the usage line once they appear.
    void render(const GuiCanvas& canvas) const override;

    /// Whether the screen is open.
    [[nodiscard]] bool isOpen() const { return m_open; }
    /// The choice once one has ended the screen; nothing while it runs.
    [[nodiscard]] std::optional<RumbleResultChoice> choice() const { return m_choice; }
    /// Whether the second grid (Rumble menu : Quit) is showing.
    [[nodiscard]] bool secondGrid() const { return m_second; }
    /// Whether the choices are showing (from kChoicesDelayMs), and whether input is taken (after their fade).
    [[nodiscard]] bool choicesShown() const { return m_choicesShown; }
    [[nodiscard]] bool takesInput() const { return m_takesInput; }
    /// The widgets.
    [[nodiscard]] const TextWidget& winner() const { return m_winner; }
    [[nodiscard]] const TextWidget& reason() const { return m_reason; }
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }
    [[nodiscard]] const UsageInfo& usage() const { return m_usage; }

  private:
    // String `id`, or empty without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const;
    // Fills the grid with the first (Replay : more) or the second (Rumble menu : Quit) pair, focused at `nowMs`.
    void fillGrid(bool second, std::uint64_t nowMs);
    // Plays `cue`, if there is a sink.
    void playCue(int cue) const;

    const GlobalStrings* m_strings = nullptr;
    std::function<void(int cue)> m_playCue;
    MenuInput m_input;
    TextWidget m_winner;
    TextWidget m_reason;
    OptionGrid m_grid;
    UsageInfo m_usage;
    std::optional<RumbleResultChoice> m_choice;
    std::uint64_t m_openedMs = 0;
    bool m_open = false;
    bool m_fromFrontEnd = false;
    bool m_second = false;
    bool m_choicesShown = false;
    bool m_takesInput = false;
};

} // namespace coney::gui
