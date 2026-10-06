// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/menu_input.h"

#include <array>
#include <utility>

namespace coney::gui {

std::optional<MenuCommand> MenuInput::stickDirection(const Pad& pad) {
    if (pad.leftY() > kStickThreshold) {
        return MenuCommand::Up;
    }
    if (pad.leftY() < -kStickThreshold) {
        return MenuCommand::Down;
    }
    if (pad.leftX() < -kStickThreshold) {
        return MenuCommand::Left;
    }
    if (pad.leftX() > kStickThreshold) {
        return MenuCommand::Right;
    }
    return std::nullopt;
}

std::optional<MenuCommand> MenuInput::dispatch(const Pad& pad, std::uint64_t nowMs) {
    // A stick back at neutral ends the long gap.
    const std::optional<MenuCommand> stick = stickDirection(pad);
    if (!stick) {
        m_stickHeld.reset();
    }

    // The candidate: the d-pad pass (auto-repeating), then the stick, then the button pass (on release).
    std::optional<MenuCommand> candidate;
    bool fromStick = false;
    if (nowMs >= m_dpadBlockedUntilMs) {
        static constexpr std::array<std::pair<std::uint16_t, MenuCommand>, 4> kDirections{{
            {pad::kUp, MenuCommand::Up},
            {pad::kDown, MenuCommand::Down},
            {pad::kLeft, MenuCommand::Left},
            {pad::kRight, MenuCommand::Right},
        }};
        // The auto-repeating query, or the plain one after a refused move (the grid's `+0xc4`).
        const std::uint16_t dpad = m_dpadRepeat ? pad.pressedWithRepeat() : pad.pressed();
        for (const auto& [bit, command] : kDirections) {
            if ((dpad & bit) != 0) {
                candidate = command;
                break;
            }
        }
    }
    if (!candidate && stick) {
        candidate = stick;
        fromStick = true;
    }
    if (!candidate) {
        if (pad.released(pad::kCross)) {
            candidate = MenuCommand::Accept;
        } else if (pad.released(pad::kTriangle | pad::kCircle)) {
            candidate = MenuCommand::Back;
        }
    }
    if (!candidate) {
        return std::nullopt;
    }

    // The gap since the last accepted command: longer while the stick stays pushed the way it last moved.
    const std::uint64_t gap = fromStick && m_stickHeld == candidate ? kStickHoldGapMs : kCommandGapMs;
    if (m_lastCommandMs && nowMs - *m_lastCommandMs <= gap) {
        return std::nullopt;
    }
    m_lastCommandMs = nowMs;
    if (fromStick) {
        m_stickHeld = candidate;
    }
    return candidate;
}

void MenuInput::focus(std::uint64_t nowMs) {
    m_lastCommandMs.reset();
    m_stickHeld.reset();
    m_dpadBlockedUntilMs = nowMs + kFocusBlockMs;
}

} // namespace coney::gui
