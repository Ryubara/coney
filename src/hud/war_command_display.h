// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"

namespace coney::hud {

/// The command menu's six slots (WarCommandDisplay, panel `+0x1ef0`).
inline constexpr std::size_t kWarCommandSlots = 6;
/// The interface cue played each time the highlight moves.
inline constexpr int kWarCommandCue = 0x20;
/// Updates the menu stays as it is after an order, before the camera's stick comes back and the fade starts.
inline constexpr int kWarCommandHoldUpdates = 10;
/// The chosen slot's icon fades over this long after the hold, every other sprite (its plate and backing too) over
/// kWarCommandFadeMs.
inline constexpr std::uint32_t kWarCommandChosenFadeMs = 1500;
inline constexpr std::uint32_t kWarCommandFadeMs = 500;

/// The Warrior command (ai-gangs.md#warrior-commands) of a menu slot (`WarCommand_FromSlot`, the jump table at
/// `0x00553fe0`): 0 follow, 2 defend, 4 scatter, 3 hold, 5 wreck, 1 attack, clockwise from up; nothing for any other
/// slot.
/// @orig 0x001a8530 WarCommand_FromSlot (unknown)
[[nodiscard]] std::optional<int> warCommandOfSlot(std::size_t slot);

/// The right stick's angle as the menu reads it from its raw bytes (`x`, `y`: 0-255, y down), degrees clockwise from
/// up: nothing inside the dead zone ((x − 127)² + (y − 127)² ≤ 12,100, about 87 % of the travel); otherwise the
/// quadrant's base (0, 90, 180, 270) plus asin(|d| / 128) of one axis's offset d from 128 (x in the up-right and
/// down-left quadrants, y in the other two; 128 or more counts as 90°). A byte of exactly 128 counts as up (y) for the
/// quadrant and on both sides for the axis read, as WarCommandDisplay_ReadStick (0x001a7040) tests them.
/// @orig 0x001a7040 WarCommandDisplay_ReadStick (unknown)
[[nodiscard]] std::optional<float> warCommandStickAngle(std::uint8_t x, std::uint8_t y);

/// The slot the menu highlights for a stick at `angle` (warCommandStickAngle()) with `current` highlighted: the
/// current slot's sector reaches 11.25° further into each neighbour; outside it the slot whose sector holds the angle;
/// straight left or right (within 2.25°, between slots 1 and 2 and between 4 and 5) keeps `current`.
[[nodiscard]] std::size_t warCommandSlotAt(float angle, std::size_t current);

/// One player's Warrior command menu (`WarCommandDisplay`, `CommandHUD`): held open with R2, steered with the right
/// stick, an order given when R2 comes up (docs/research/hud.md#warrior-command-menu). It keeps the state and draws
/// the six slots and the highlighted slot's name; whoever owns the pad and the game state reads the stick into it,
/// passes the order to the dispatcher (0x0041c4e0) and switches the camera's stick as cameraStickOn() says.
///
/// **Coney's choices**: the slots' backing and plate are plain squares (the original's sprite word `0xd0100` of
/// sprite instance 6 is not mapped to a sheet rectangle); the 16:9 layouts are not built. The name's time counts from
/// the last picking update (the original sets the text on each), and the display closes when that time passes the
/// name's `<DISPLAYTIME>` after an order.
class WarCommandDisplay {
  public:
    /// What the menu needs to know about its chief each update.
    struct Chief {
        bool allowed = true;     ///< The display may open (`HUDShowWarCommand`, `+0x155c`).
        bool warChief = true;    ///< The human leads a crew.
        bool menuLocked = false; ///< The player's menu is locked (game state `+0x42e` + player).
        /// The highlighted slot's name's `<DISPLAYTIME>` (hint_box.h markupTimesOf()); none: it never expires.
        std::optional<std::uint32_t> nameDisplayMs;
    };

    /// R2 held, this update: a menu that is not allowed is left as it is; an allowed one is shown for a war chief,
    /// a previous order, its fade and its blink cleared and the camera's stick switched off (the highlight and the lit
    /// flag stay as they were), and hidden for a human who is not one.
    /// @orig 0x001a6d28 WarCommandDisplay_Open (unknown)
    void open(const Chief& chief, std::uint64_t nowMs);
    /// R2 released: the highlighted slot's command, once per opening while the menu shows and no order has been given
    /// (the slot is lit from then on); nothing otherwise. The menu then holds, fades and closes (update()); after an
    /// order of a disabled command it draws nothing (render()). The caller stores it as the player's last command
    /// and gives it to the dispatcher (script::giveWarriorCommand()).
    /// @orig 0x001a6c58 WarCommandDisplay_Issue (unknown)
    [[nodiscard]] std::optional<int> issue();
    /// One update at game time `nowMs` with the right stick's raw bytes: while shown, not yet ordered and not locked
    /// the stick past the dead zone lights the highlighted slot and moves the highlight (cue kWarCommandCue on `audio`
    /// at each move); a lock that arrives while open ends it as if ordered, with no command; after an order, the hold,
    /// the fade from the hold's last update, the chosen slot's blink and the close once the name's time has run out.
    /// Whether the menu is allowed is kept from `chief` for render().
    /// @orig 0x001a7e48 WarCommandDisplay_Update (unknown)
    /// @orig 0x001a7040 WarCommandDisplay_ReadStick (unknown)
    void update(std::uint8_t stickX, std::uint8_t stickY, const Chief& chief, std::uint64_t nowMs,
                const HudSound& audio);
    /// The chief went down: the menu closes at once.
    void close();
    /// The name's text shuts down (`MessageHUD_Shutdown`, from `WarCommandDisplay_SetAllowed`): it is not drawn
    /// until the next update that picks a slot sets it again.
    void shutName() { m_nameShut = true; }

    /// Whether the menu is up (drawn).
    [[nodiscard]] bool shown() const { return m_shown; }
    /// Whether an order has been given since it opened.
    [[nodiscard]] bool issued() const { return m_issued; }
    /// Whether the highlighted slot is drawn lit (`+0x1560`): set by the stick past the dead zone or an order, cleared
    /// when the menu closes once its name's time has run out.
    [[nodiscard]] bool lit() const { return m_lit; }
    /// The highlighted slot.
    [[nodiscard]] std::size_t highlight() const { return m_highlight; }
    /// Whether the camera's right stick may turn the camera (`0x0050b1b0[pad]`): off from the opening until the
    /// hold after an order ends.
    [[nodiscard]] bool cameraStickOn() const { return m_cameraStickOn; }

    /// The texts and switches render() reads: the name of slot `n` (`GSTRING.COMMAND`, entry 6 while all commands are
    /// locked, 7 while the highlighted one is disabled), and whether command `c` is enabled.
    struct Look {
        std::function<std::string(std::size_t entry)> name;
        std::function<bool(int command)> enabled;
        bool allLocked = false;
        float centreX = 0.5F; ///< 0.5; 0.23 / 0.76 for players 1 and 2 in a two-player level numbered 100 or more.
    };
    /// Draws the six slots and the highlighted slot's name into `canvas` at game time `nowMs`: nothing while hidden or
    /// not allowed, or after an order of a disabled command; the lit slot's plate gold and backing grey, the others'
    /// plate grey and backing black, a disabled command's icon dark; the name grey while picking (dark for a disabled
    /// command) and gold after the order.
    /// @orig 0x001a8590 WarCommandDisplay_Render (unknown)
    void render(const HudCanvas& canvas, const Look& look, std::uint64_t nowMs) const;

  private:
    // The fade of the chosen slot's plate (`chosen`) or of everything else after the hold, 1 to 0.
    [[nodiscard]] float fadeOf(bool chosen, std::uint64_t nowMs) const;

    bool m_shown = false;    // +0x1554
    bool m_issued = false;   // +0x1548
    bool m_allowed = true;   // +0x155c as the last update saw it
    bool m_lit = false;      // +0x1560
    bool m_nameShut = false; // the name's text shut down (MessageHUD_Shutdown)
    std::size_t m_highlight = 0;
    bool m_cameraStickOn = true;
    int m_afterIssue = 0;                     // updates since the order, up to the hold (+0x1568)
    std::optional<std::uint64_t> m_fadeStart; // when the fade began
    std::uint64_t m_textSince = 0;            // when the text was last set (every picking update)
    int m_blink = 0;                          // the blink counter (+0x1578)
    bool m_blinkHidden = false;               // the chosen slot hidden by the blink this update
};

} // namespace coney::hud
