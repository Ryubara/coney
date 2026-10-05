// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/input_gate.h"

namespace coney::debug {

PortSamples InputGate::sample(std::uint64_t frame) {
    PortSamples samples = m_inner != nullptr ? m_inner->sample(frame) : PortSamples{};
    m_rawSample = samples[0];
    m_rawPad.update(m_rawSample);
    const std::uint16_t held = m_rawSample.connected ? m_rawSample.buttons : 0;

    // The chord toggles on the frame both buttons are first held together.
    const bool chord = (held & kMenuChord) == kMenuChord;
    if (chord && !m_chordHeld) {
        if (m_navigator.isOpen()) {
            m_navigator.close();
        } else {
            m_navigator.open();
            m_menuInput.reset();
        }
    }
    m_chordHeld = chord;
    const bool wasOpen = m_navigator.isOpen();

    // The menu reads port 1 while open (not on the frame the chord itself fired, which is no menu input).
    if (wasOpen && !chord) {
        m_navigator.apply(m_menuInput.read(m_rawPad));
    }
    if (wasOpen || m_navigator.isOpen() || chord) {
        // Everything held now stays hidden from the game until it is let go, even after the menu closes.
        m_hidden |= held;
    }
    m_hidden &= held;

    // What the game sees of port 1.
    if (m_navigator.isOpen()) {
        PadSample neutral;
        neutral.connected = m_rawSample.connected;
        samples[0] = neutral;
    } else if (m_hidden != 0) {
        samples[0].buttons &= static_cast<std::uint16_t>(~m_hidden);
        for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
            if ((m_hidden & pad::kPressureButtons.at(i)) != 0) {
                samples[0].pressure.at(i) = 0;
            }
        }
    }

    for (const auto& callback : m_betweenSteps) {
        callback();
    }
    return samples;
}

} // namespace coney::debug
