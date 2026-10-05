// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "core/pad.h"
#include "debug/menu_model.h"

namespace coney::debug {

/// What the pad asks of the pad menu in one step.
enum class MenuAction : std::uint8_t {
    Up,       ///< d-pad up or the left stick pushed up: the previous item (or, editing text, the next character).
    Down,     ///< d-pad down or the left stick pushed down.
    Left,     ///< d-pad left or the left stick pushed left: decrease the value (or move the text cursor left).
    Right,    ///< d-pad right or the left stick pushed right: increase.
    Accept,   ///< cross: choose, toggle, enter a submenu, start or finish typing.
    Back,     ///< circle: leave the page (or cancel typing); at the root, close the menu.
    Pin,      ///< square: pin or unpin the item (editing text: delete the character).
    Reset,    ///< triangle: return the value to its default.
    PageUp,   ///< L1: a page of items up (editing text: an older history line).
    PageDown, ///< R1: a page of items down (editing text: a newer history line).
};

/// One step's reading of the pad for the pad menu: at most one action, how much a held direction has accelerated,
/// and the step size the shoulder triggers pick.
struct MenuInputFrame {
    std::optional<MenuAction> action;
    int multiplier = 1; ///< For Left and Right on a number: 1, then 2, 5 and 10 the longer it is held.
    StepSize stepSize = StepSize::Normal; ///< L2 held: fine; R2 held: coarse.
};

/// Turns a pad record into the pad menu's actions, counting in simulation steps (never real time), so a scripted run
/// navigates the same way every time.
///
/// - The four directions come from the d-pad or the left stick pushed past kStickThreshold (a partial deflection
///   counts). A direction fires on the step it is pressed, then, held, after kRepeatDelay steps every
///   kRepeatInterval steps, every 2 steps after kFastAfter and every step after kFastestAfter: hold-to-repeat that
///   speeds up.
/// - Left and Right on a number also grow the change: the multiplier is 1, then 2 after kFastAfter, 5 after
///   kFastestAfter and 10 after kTurboAfter steps held.
/// - The buttons fire on the step they go down. One action a step: directions first (up, down, left, right), then
///   cross, circle, square, triangle, L1, R1.
///
/// The menu's buttons are Coney's own (docs/guides/debug-menu.md#controls).
class PadMenuInput {
  public:
    /// Steps a direction is held before it repeats: 0.4 s.
    static constexpr int kRepeatDelay = 12;
    /// Steps between repeats at first.
    static constexpr int kRepeatInterval = 4;
    /// Steps held after which repeats come every 2 steps and the multiplier is 2: 1.5 s.
    static constexpr int kFastAfter = 45;
    /// Steps held after which repeats come every step and the multiplier is 5: 3 s.
    static constexpr int kFastestAfter = 90;
    /// Steps held after which the multiplier is 10: 5 s.
    static constexpr int kTurboAfter = 150;
    /// How far the left stick must be pushed (in [-1, 1]) to count as a direction.
    static constexpr float kStickThreshold = 0.5F;

    /// Reads one step of `pad` (updated for this step). Call it once per step.
    [[nodiscard]] MenuInputFrame read(const Pad& pad);

    /// Forgets the held directions, so a direction still held when the menu opens does not repeat at once.
    void reset() { m_held.fill(0); }

  private:
    // Whether a direction held for `held` steps fires this step.
    [[nodiscard]] static bool fires(int held);
    // The multiplier for a direction held `held` steps.
    [[nodiscard]] static int multiplierFor(int held);

    std::array<int, 4> m_held{}; // steps each direction (up, down, left, right) has been held
};

} // namespace coney::debug
