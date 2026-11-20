// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#include "gui/mod_ui/ui_input_mask.h"

namespace coney::gui {

PortSamples UiInputMask::sample(std::uint64_t frame) {
    PortSamples samples = m_inner != nullptr ? m_inner->sample(frame) : PortSamples{};
    for (std::size_t port = 0; port < kPadPorts; ++port) {
        PadSample& sample = samples.at(port);
        m_raw.at(port).update(sample);
        const std::uint16_t held = sample.connected ? sample.buttons : 0;
        const bool busy = m_model.takesPad(port);

        // Everything held while a screen has the port stays hidden until it is let go, even after the screen closes.
        std::uint16_t& hidden = m_hidden.at(port);
        if (busy) {
            hidden |= held;
        }
        hidden &= held;

        // What gameplay sees of the port: connected and empty while a screen has it, else less what is hidden.
        if (busy) {
            PadSample neutral;
            neutral.connected = sample.connected;
            sample = neutral;
        } else if (hidden != 0) {
            sample.buttons &= static_cast<std::uint16_t>(~hidden);
            for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
                if ((hidden & pad::kPressureButtons.at(i)) != 0) {
                    sample.pressure.at(i) = 0;
                }
            }
        }
    }
    return samples;
}

} // namespace coney::gui
