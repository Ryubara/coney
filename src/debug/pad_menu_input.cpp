// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/pad_menu_input.h"

#include <cstddef>
#include <utility>

namespace coney::debug {

bool PadMenuInput::fires(int held) {
    if (held == 1) {
        return true;
    }
    if (held < kRepeatDelay) {
        return false;
    }
    const int interval = held >= kFastestAfter ? 1 : held >= kFastAfter ? 2 : kRepeatInterval;
    return (held - kRepeatDelay) % interval == 0;
}

int PadMenuInput::multiplierFor(int held) {
    if (held >= kTurboAfter) {
        return 10;
    }
    if (held >= kFastestAfter) {
        return 5;
    }
    return held >= kFastAfter ? 2 : 1;
}

MenuInputFrame PadMenuInput::read(const Pad& pad) {
    MenuInputFrame frame;
    if (pad.held(pad::kL2)) {
        frame.stepSize = StepSize::Fine;
    } else if (pad.held(pad::kR2)) {
        frame.stepSize = StepSize::Coarse;
    }

    // The directions, each from the d-pad or the stick; count how long each is held.
    const std::array<bool, 4> down{
        pad.held(pad::kUp) || pad.leftY() > kStickThreshold,
        pad.held(pad::kDown) || pad.leftY() < -kStickThreshold,
        pad.held(pad::kLeft) || pad.leftX() < -kStickThreshold,
        pad.held(pad::kRight) || pad.leftX() > kStickThreshold,
    };
    static constexpr std::array<MenuAction, 4> kDirections{MenuAction::Up, MenuAction::Down, MenuAction::Left,
                                                           MenuAction::Right};
    for (std::size_t i = 0; i < down.size(); ++i) {
        m_held.at(i) = down.at(i) ? m_held.at(i) + 1 : 0;
        if (!frame.action && down.at(i) && fires(m_held.at(i))) {
            frame.action = kDirections.at(i);
            frame.multiplier = multiplierFor(m_held.at(i));
        }
    }
    if (frame.action) {
        return frame;
    }

    // The buttons, on the step they go down.
    static constexpr std::array<std::pair<std::uint16_t, MenuAction>, 6> kButtons{{
        {pad::kCross, MenuAction::Accept},
        {pad::kCircle, MenuAction::Back},
        {pad::kSquare, MenuAction::Pin},
        {pad::kTriangle, MenuAction::Reset},
        {pad::kL1, MenuAction::PageUp},
        {pad::kR1, MenuAction::PageDown},
    }};
    for (const auto& [bit, action] : kButtons) {
        if (pad.pressed(bit)) {
            frame.action = action;
            break;
        }
    }
    return frame;
}

} // namespace coney::debug
