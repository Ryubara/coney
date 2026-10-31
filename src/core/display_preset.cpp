// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/display_preset.h"

#include "core/renderer_choice.h"

namespace coney {

// Stubs for the test commit: they do what Coney did before presets, which had none.

std::optional<DisplayPreset> parseDisplayPresetName(std::string_view /*name*/) { return std::nullopt; }

std::string_view displayPresetName(DisplayPreset /*preset*/) { return "ps2"; }

DisplayPreset presetOfSetting(int /*value*/) { return DisplayPreset::Ps2; }

DisplayValues presetValues(DisplayPreset /*preset*/) { return {}; }

DisplayPreset presetMatching(const DisplayValues& /*values*/) { return DisplayPreset::Ps2; }

DisplaySettings::DisplaySettings(int& renderer) : m_renderer(renderer) {}

DisplayPreset DisplaySettings::preset() const { return presetOfSetting(m_preset); }

DisplayValues DisplaySettings::current() const {
    DisplayValues values = m_values;
    values.renderer = m_renderer;
    return values;
}

void DisplaySettings::settle() {}

void DisplaySettings::choose(DisplayPreset /*preset*/) {}

void DisplaySettings::pin(std::optional<DisplayPreset> /*preset*/) {}

DisplayValues DisplaySettings::drawn() const { return current(); }

void DisplaySettings::store(const DisplayValues& values) {
    m_values = values;
    m_renderer = values.renderer;
}

DisplaySettings& displaySettings() {
    static DisplaySettings settings(rendererSetting());
    return settings;
}

} // namespace coney
