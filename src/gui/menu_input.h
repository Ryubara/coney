// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "core/pad.h"

namespace coney::gui {

/// What a menu widget is told to do: the six commands the menus' input dispatch sends to a widget's handler.
enum class MenuCommand : std::uint8_t {
    Up,     ///< d-pad up, or the left stick pushed up past half way.
    Down,   ///< d-pad down, or the left stick pushed down past half way.
    Left,   ///< d-pad left, or the left stick pushed left past half way.
    Right,  ///< d-pad right, or the left stick pushed right past half way.
    Accept, ///< cross, when it is let go.
    Back,   ///< triangle or circle, when it is let go.
};

/// Turns one player's pad record into menu commands, at most one a frame: the d-pad through the auto-repeating query
/// (or the left stick past ±0.5), then cross and triangle or circle on their **release**. A command is accepted only
/// when more than kCommandGapMs have passed since the last accepted one; while the stick stays pushed the same way the
/// gap is kStickHoldGapMs, until the stick returns to neutral.
///
/// Coney's reading where the page leaves the details open: one candidate a frame, in the order d-pad, stick, accept,
/// back (the original runs its d-pad pass before its button pass); a candidate refused by the gap is dropped, so a
/// release that comes too soon after the last command is lost, as the original's one-sample released query would lose
/// it; the stick's direction is up or down before left or right when both axes are past half way.
///
/// Research: docs/research/frontend.md#input
class MenuInput {
  public:
    /// The least time between two accepted commands, in milliseconds (a command needs more than this).
    static constexpr std::uint64_t kCommandGapMs = 110;
    /// The gap between two commands from a stick held the same way, in milliseconds.
    static constexpr std::uint64_t kStickHoldGapMs = 400;
    /// How long taking focus blocks the d-pad, in milliseconds.
    static constexpr std::uint64_t kFocusBlockMs = 20;
    /// How far the left stick must be pushed (in [-1, 1]) to count as a direction.
    static constexpr float kStickThreshold = 0.5F;

    /// The command `pad` gives at game time `nowMs` (milliseconds), or nothing. Call it once a frame, after the pad
    /// update, with a time that never goes down.
    /// @orig 0x001e95c0 MenuInput_Dispatch (unknown)
    [[nodiscard]] std::optional<MenuCommand> dispatch(const Pad& pad, std::uint64_t nowMs);

    /// What a widget does when it takes focus: forgets the last command and the held stick, and ignores the d-pad
    /// until kFocusBlockMs after `nowMs`.
    void focus(std::uint64_t nowMs);

    /// Whether the d-pad auto-repeats (the default) or counts only fresh presses: an option grid turns repeat off after
    /// a move that could not leave its item, so holding against an end does not refuse again and again, and back on
    /// after a move (`OptionGrid` `+0xc4`).
    void setDpadRepeat(bool repeat) { m_dpadRepeat = repeat; }

  private:
    // The direction the left stick is pushed past kStickThreshold, if any.
    [[nodiscard]] static std::optional<MenuCommand> stickDirection(const Pad& pad);

    std::optional<std::uint64_t> m_lastCommandMs; // when the last command was accepted; none since focus
    std::optional<MenuCommand> m_stickHeld;       // the stick direction that gave the last command, until neutral
    std::uint64_t m_dpadBlockedUntilMs = 0;       // the d-pad is ignored before this time
    bool m_dpadRepeat = true;                     // the d-pad through the auto-repeating query
};

} // namespace coney::gui
