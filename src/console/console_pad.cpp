// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/console_pad.h"

#include <array>
#include <utility>

namespace coney::console {

namespace {

// The left stick's byte past which it counts as a d-pad press for the picker: well outside the dead zone, so a stick
// at rest or barely touched moves nothing.
constexpr std::uint8_t kStickLow = 48;
constexpr std::uint8_t kStickHigh = 208;
// The left stick's bytes in PadSample::sticks.
constexpr std::size_t kLeftX = 2;
constexpr std::size_t kLeftY = 3;

// The buttons the picker reads from `sample`: the d-pad, cross and circle, with the left stick pushed far read as the
// d-pad.
std::uint16_t navigationButtons(const PadSample& sample) {
    std::uint16_t buttons = sample.buttons & (pad::kDpad | pad::kCross | pad::kCircle);
    const std::uint8_t x = sample.sticks[kLeftX];
    const std::uint8_t y = sample.sticks[kLeftY];
    buttons |= x <= kStickLow ? pad::kLeft : (x >= kStickHigh ? pad::kRight : 0);
    buttons |= y <= kStickLow ? pad::kUp : (y >= kStickHigh ? pad::kDown : 0);
    return buttons;
}

// Each button the picker takes, with its press, in the order they are applied when several come in one frame.
constexpr std::array<std::pair<std::uint16_t, PickerInput>, 6> kPickerButtons{{
    {pad::kUp, PickerInput::Up},
    {pad::kDown, PickerInput::Down},
    {pad::kLeft, PickerInput::Left},
    {pad::kRight, PickerInput::Right},
    {pad::kCross, PickerInput::Accept},
    {pad::kCircle, PickerInput::Back},
}};

} // namespace

PortSamples ConsolePad::sample(std::uint64_t frame) {
    PortSamples samples = m_inner != nullptr ? m_inner->sample(frame) : PortSamples{};
    PadSample& port1 = samples[0];
    const std::uint16_t held = port1.connected ? port1.buttons : 0;
    const std::uint16_t navigation = port1.connected ? navigationButtons(port1) : 0;
    m_held = held;

    // The chord, on the frame both buttons are first held together; hidden from the game while held.
    const bool chord = (held & kConsoleChord) == kConsoleChord;
    const bool toggled = chord && !m_chordHeld;
    m_chordHeld = chord;
    if (chord) {
        m_hidden |= kConsoleChord;
    }
    if (toggled) {
        setOpen(!m_open, OpenedBy::Pad);
    } else if (m_open) {
        // The picker takes the presses new this frame; circle on its commands closes the console.
        const auto pressed = static_cast<std::uint16_t>(navigation & ~m_navigation);
        for (const auto& [button, input] : kPickerButtons) {
            if ((pressed & button) != 0 && m_open && !m_picker.apply(input)) {
                setOpen(false, OpenedBy::Pad);
            }
        }
    }
    m_navigation = navigation;

    if (m_open) {
        // The game sees port 1 plugged in with nothing pressed.
        const bool connected = port1.connected;
        port1 = PadSample{};
        port1.connected = connected;
        return samples;
    }
    // Closed: what is still hidden stays so until let go.
    m_hidden &= held;
    port1.buttons &= static_cast<std::uint16_t>(~m_hidden);
    return samples;
}

void ConsolePad::setOpen(bool open, OpenedBy by) {
    if (open && !m_open) {
        m_openedBy = by;
        if (by == OpenedBy::Pad) {
            m_picker.reset();
        }
    }
    if (!open && m_open) {
        // Whatever is held as it closes (the circle or the chord that closed it) never presses anything in play.
        m_hidden |= m_held;
    }
    m_open = open;
}

} // namespace coney::console
