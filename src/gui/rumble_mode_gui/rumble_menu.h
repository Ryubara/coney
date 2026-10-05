// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/text_widget.h"
#include "gui/widget.h"
#include "warriors/game_state.h"

namespace coney::gui {

/// The Rumble menu's screens, in the order QUICK RUMBLE shows them.
enum class RumbleScreen : std::uint8_t {
    GameMode,    ///< "1 ON 1" or "WAR PARTY": writes the mode's number and the gang size.
    GameType,    ///< One player against the computer, versus or co-op: writes the players.
    ChooseGangs, ///< Each side's gang: writes the gangs' packs, character types and names.
    ChooseArea,  ///< The arena: its confirm starts the fight with the arena's level number.
};

/// What a frame of the Rumble menu ended with.
enum class RumbleMenuResult : std::uint8_t {
    Stay,      ///< Still on a screen.
    Cancelled, ///< Backed out of the first screen.
    Started,   ///< The arena was confirmed: the set-up is complete.
};

/// What accepting every screen's first entry writes on a fresh boot: 1 ON 1, one player against the computer, the
/// BASEBALL FURIES against the ORPHANS, the Fight Pen (`level102`). The 23 values are the ones read at run time
/// (docs/research/frontend.md#rumble-setup): 3, 12, 1, 4, 2, then side 1's and side 2's nine character types.
[[nodiscard]] RumbleSetup rumbleMenuDefaults();

/// The Rumble menu's screens (`RumbleModeGUI/`), the content of mode 0x11: Game Mode, Game Type, Choose Gangs and
/// Choose Area, each a list of entries the HUD player moves through with up and down and confirms with accept. Each
/// confirm writes its part of the set-up the arena reads; back returns to the screen before, and from the first screen
/// cancels the menu.
///
/// What the page gives, and Coney follows: the screens and their order, the entries a fresh boot offers (two modes,
/// one pairing of gangs, one arena), which screen writes which of the 23 values and the values the default writes.
/// **Coney's choices** where it is silent: the layout (a title and the entries centred, the selected one larger and
/// white, PM_Mode's style); the titles are the screens' names as the page gives them; the Game Type entries other
/// than "1 Player : Vs." are labelled with the page's short names ("VS", "COOP"); versus and co-op need two pads
/// connected and are otherwise not accepted; back from a later screen returns to the one before with its first entry
/// selected (the Game Mode screen keeps the mode chosen); WAR PARTY's mode number is 14 (`RM_Brawl5`, inferred from
/// its name) and its gang size 5; left and right (the gang screen's warchief) do nothing, since only one gang per side
/// is known; no sound cues (the page names none). The "vs" title is not one of these screens: the arena's start shows
/// it (`ShowRumbleModeIntro`, docs/research/frontend.md#quick-rumble).
///
/// Research: docs/research/frontend.md#rumble-setup
class RumbleMenu {
  public:
    /// Shows the Game Mode screen with its first entry selected, taking the focus at game time `nowMs`.
    void start(std::uint64_t nowMs);

    /// One frame: reads the command `frame.pad` gives, acts on it (a confirm writes into `setup`), and advances the
    /// texts. `connectedPads` decides whether versus and co-op can be chosen.
    [[nodiscard]] RumbleMenuResult update(const GuiFrame& frame, std::size_t connectedPads, RumbleSetup& setup);

    /// Adds the current screen's sprites to `canvas`.
    void render(const GuiCanvas& canvas) const;

    /// The screen on top.
    [[nodiscard]] RumbleScreen screen() const { return m_screen; }
    /// The screen's name, for logs and the title: "Game Mode", "Game Type", "Choose Gangs" or "Choose Area".
    [[nodiscard]] std::string_view screenName() const;
    /// The current screen's entries.
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }

  private:
    // Builds `screen`'s title and entries, selecting entry `selected`, and takes the focus at `nowMs`.
    void show(RumbleScreen screen, std::size_t selected, std::uint64_t nowMs);
    // Acts on the confirmed entry `code` of the current screen; returns Started after the arena.
    RumbleMenuResult confirm(int code, std::size_t connectedPads, RumbleSetup& setup, std::uint64_t nowMs);

    MenuInput m_input;
    TextWidget m_title;
    TextWidget m_rival; // the gang screen's second side, under the first
    OptionGrid m_grid;
    RumbleScreen m_screen = RumbleScreen::GameMode;
    std::size_t m_mode = 0; // the mode list entry chosen on the Game Mode screen
};

} // namespace coney::gui
