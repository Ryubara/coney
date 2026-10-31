// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/display_preset.h"

#include <array>
#include <cstddef>

#include "core/assert.h"
#include "core/renderer_choice.h"

namespace coney {

namespace {

// The presets' names, in DisplayPreset's order; the last is Custom's.
constexpr std::array<std::string_view, 5> kPresetNames{"ps2", "xbox", "enhanced", "high", "custom"};

// The presets table (docs/engine/renderer.md#presets), one column a preset in DisplayPreset's order. Each row's value
// counts up the columns where the table gives each preset its own; the blob is on in PS2 alone, and the Xbox's fog
// and gamma ramp are kept by the presets above it.
constexpr std::array<DisplayValues, kChoosablePresets> kPresets{
    DisplayValues{},
    DisplayValues{.renderer = 1,
                  .lights = 1,
                  .shadows = 1,
                  .blobShadow = false,
                  .perPixelFog = true,
                  .gammaRamp = true,
                  .reflections = 0,
                  .antiAliasing = 0},
    DisplayValues{.renderer = 1,
                  .lights = 2,
                  .shadows = 2,
                  .blobShadow = false,
                  .perPixelFog = true,
                  .gammaRamp = true,
                  .reflections = 1,
                  .antiAliasing = 1},
    DisplayValues{.renderer = 1,
                  .lights = 3,
                  .shadows = 3,
                  .blobShadow = false,
                  .perPixelFog = true,
                  .gammaRamp = true,
                  .reflections = 2,
                  .antiAliasing = 2},
};

// Calls `field` with each of DisplayValues' members, so settle() can treat them one by one without naming each.
template <typename Field> void forEachValue(Field&& field) {
    field(&DisplayValues::renderer);
    field(&DisplayValues::lights);
    field(&DisplayValues::shadows);
    field(&DisplayValues::blobShadow);
    field(&DisplayValues::perPixelFog);
    field(&DisplayValues::gammaRamp);
    field(&DisplayValues::reflections);
    field(&DisplayValues::antiAliasing);
}

} // namespace

std::optional<DisplayPreset> parseDisplayPresetName(std::string_view name) {
    for (int i = 0; i < kChoosablePresets; ++i) {
        if (name == kPresetNames.at(static_cast<std::size_t>(i))) {
            return static_cast<DisplayPreset>(i);
        }
    }
    return std::nullopt;
}

std::string_view displayPresetName(DisplayPreset preset) { return kPresetNames.at(static_cast<std::size_t>(preset)); }

DisplayPreset presetOfSetting(int value) {
    return value >= 0 && value < kChoosablePresets ? static_cast<DisplayPreset>(value) : DisplayPreset::Custom;
}

DisplayValues presetValues(DisplayPreset preset) {
    CONEY_ASSERT(preset != DisplayPreset::Custom);
    return kPresets.at(static_cast<std::size_t>(preset));
}

DisplayPreset presetMatching(const DisplayValues& values) {
    for (int i = 0; i < kChoosablePresets; ++i) {
        if (kPresets.at(static_cast<std::size_t>(i)) == values) {
            return static_cast<DisplayPreset>(i);
        }
    }
    return DisplayPreset::Custom;
}

DisplaySettings::DisplaySettings(int& renderer) : m_renderer(renderer) {
    // The renderer may already hold a value of its own; the rest start at PS2's.
    m_settled = current();
    m_preset = static_cast<int>(presetMatching(m_settled));
    m_settledPreset = m_preset;
}

DisplayPreset DisplaySettings::preset() const { return presetOfSetting(m_preset); }

DisplayValues DisplaySettings::current() const {
    DisplayValues values = m_values;
    values.renderer = m_renderer;
    return values;
}

void DisplaySettings::settle() {
    if (m_chooseNext) {
        // `--preset` at the start: exactly the preset, over whatever the overrides file wrote.
        const DisplayPreset chosen = *m_chooseNext;
        m_chooseNext.reset();
        choose(chosen);
        return;
    }
    DisplayValues now = current();
    const DisplayPreset asked = presetOfSetting(m_preset);
    if (m_preset != m_settledPreset && asked != DisplayPreset::Custom) {
        // A preset was chosen: its values, but a value changed in the same batch stays as set.
        DisplayValues chosen = presetValues(asked);
        forEachValue([&](auto member) {
            if (now.*member != m_settled.*member) {
                chosen.*member = now.*member;
            }
        });
        now = chosen;
    }
    // The preset shown is the one the values match: Custom once a value differs from every preset's.
    store(now);
    m_preset = static_cast<int>(presetMatching(now));
    m_settledPreset = m_preset;
    m_settled = now;
}

void DisplaySettings::choose(DisplayPreset preset) {
    if (preset == DisplayPreset::Custom) {
        return;
    }
    const DisplayValues values = presetValues(preset);
    store(values);
    m_preset = static_cast<int>(preset);
    m_settledPreset = m_preset;
    m_settled = values;
}

void DisplaySettings::pin(std::optional<DisplayPreset> preset) {
    CONEY_ASSERT(preset != DisplayPreset::Custom);
    m_pinned = preset;
}

DisplayValues DisplaySettings::drawn() const { return m_pinned ? presetValues(*m_pinned) : current(); }

void DisplaySettings::store(const DisplayValues& values) {
    m_values = values;
    m_renderer = values.renderer;
}

DisplaySettings& displaySettings() {
    static DisplaySettings settings(rendererSetting());
    return settings;
}

} // namespace coney
