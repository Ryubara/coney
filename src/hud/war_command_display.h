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
/// The chosen slot's plate fades over this long after the hold, everything else over kWarCommandFadeMs.
inline constexpr std::uint32_t kWarCommandChosenFadeMs = 1500;
inline constexpr std::uint32_t kWarCommandFadeMs = 500;

/// The Warrior command (ai.md#warrior-commands) of a menu slot (`WarCommand_FromSlot`, the jump table at
/// `0x00553fe0`): 0 follow, 2 defend, 4 scatter, 3 hold, 5 wreck, 1 attack, clockwise from up; nothing for any other
/// slot.
/// @orig 0x001a8530 WarCommand_FromSlot (unknown)
[[nodiscard]] std::optional<int> warCommandOfSlot(std::size_t slot);

/// The right stick's angle as the menu reads it from its raw bytes (`x`, `y`: 0-255, y down), degrees clockwise from
/// up: nothing inside the dead zone ((x − 127)² + (y − 127)² ≤ 12,100, about 87 % of the travel); otherwise the
/// quadrant's base (0, 90, 180, 270) plus asin(|d| / 128) of one axis's offset d from 128 (x in the up-right and
/// down-left quadrants, y in the other two; 128 or more counts as 90°). **Coney's reading**: a byte of 128 or more is
/// right (x) or down (y).
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
/// sprite instance 6 is not mapped to a sheet rectangle); the name's time counts from the last time the menu changed
/// its text (opening or a new highlight), and the display closes when that time passes the name's `<DISPLAYTIME>`
/// after an order (inferred on the page); the 16:9 layouts are not built.
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

    /// R2 held, this update: shows the menu (when allowed and the human is a war chief), clears a previous order and
    /// its fade, and switches the camera's stick off; the highlight stays where it was. For a human who is not a war
    /// chief the menu hides instead.
    /// @orig 0x001a6d28 WarCommandDisplay_Open (unknown)
    void open(const Chief& chief, std::uint64_t nowMs);
    /// R2 released: the highlighted slot's command, once per opening while the menu shows and no order has been given;
    /// nothing otherwise. The menu then holds, fades and closes (update()).
    /// @orig 0x001a6c58 WarCommandDisplay_Issue (unknown)
    [[nodiscard]] std::optional<int> issue();
    /// One update at game time `nowMs` with the right stick's raw bytes: while shown, not yet ordered and not locked
    /// the stick moves the highlight (cue kWarCommandCue on `audio` at each move); a lock that arrives while open ends
    /// it as if ordered, with no command; after an order, the hold, the fade and the close.
    /// @orig 0x001a7e48 WarCommandDisplay_Update (unknown)
    /// @orig 0x001a7040 WarCommandDisplay_ReadStick (unknown)
    void update(std::uint8_t stickX, std::uint8_t stickY, const Chief& chief, std::uint64_t nowMs,
                const HudSound& audio);
    /// The chief went down: the menu closes at once.
    void close();

    /// Whether the menu is up (drawn).
    [[nodiscard]] bool shown() const { return m_shown; }
    /// Whether an order has been given since it opened.
    [[nodiscard]] bool issued() const { return m_issued; }
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
    /// Draws the six slots and the highlighted slot's name into `canvas` at game time `nowMs`.
    /// @orig 0x001a8590 WarCommandDisplay_Render (unknown)
    void render(const HudCanvas& canvas, const Look& look, std::uint64_t nowMs) const;

  private:
    // The fade of the chosen slot's plate (`chosen`) or of everything else after the hold, 1 to 0.
    [[nodiscard]] float fadeOf(bool chosen, std::uint64_t nowMs) const;

    bool m_shown = false;
    bool m_issued = false;
    std::size_t m_highlight = 0;
    bool m_cameraStickOn = true;
    int m_afterIssue = 0;                     // updates since the order
    std::optional<std::uint64_t> m_fadeStart; // when the fade began
    std::uint64_t m_textSince = 0;            // when the text was last set
    std::uint64_t m_updates = 0;              // the blink's beat
};

} // namespace coney::hud
