// SPDX-License-Identifier: GPL-3.0-or-later
// The display preset's tunables (docs/engine/renderer.md#presets-built): Display/Preset and the values it sets, in the
// Tunables page's Display category and the overrides file.
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "core/display_preset.h"
#include "debug/game_tunables.h"
#include "debug/tunables.h"

using coney::debug::TunableRegistry;
using coney::debug::TunableType;

TEST_CASE("the display preset and its values are Display tunables that the overrides file saves", "[debug][display]") {
    // Expected: ../engine/renderer.md#presets, Coney's own setting: Display/Preset (0 PS2, the default) and one value
    // per row of the presets table, in the Tunables page's Display category and the overrides file.
    TunableRegistry registry;
    coney::debug::registerGameTunables(registry);
    const coney::debug::Tunable* preset = registry.find("Display/Preset");
    REQUIRE(preset != nullptr);
    CHECK(preset->type() == TunableType::Int);
    CHECK(preset->defaultValue() == 0.0);
    CHECK(preset->min() == 0.0);
    CHECK(preset->max() == 4.0); // 4 is Custom, so a saved Custom reads back as Custom
    for (const char* name :
         {"Display/Renderer", "Display/Lights", "Display/Shadows", "Display/Blob shadow", "Display/Per-pixel fog",
          "Display/Gamma ramp", "Display/Reflections", "Display/Anti-aliasing"}) {
        CAPTURE(name);
        CHECK(registry.find(name) != nullptr);
    }
    // PS2 by default: every value is the PS2 preset's, not merely its own default.
    const coney::DisplayValues ps2 = coney::presetValues(coney::DisplayPreset::Ps2);
    const auto current = [&registry](const char* name) { return registry.find(name)->current(); };
    CHECK(current("Display/Preset") == 0.0);
    CHECK(current("Display/Renderer") == ps2.renderer);
    CHECK(current("Display/Lights") == ps2.lights);
    CHECK(current("Display/Shadows") == ps2.shadows);
    CHECK(current("Display/Blob shadow") == (ps2.blobShadow ? 1.0 : 0.0));
    CHECK(current("Display/Per-pixel fog") == (ps2.perPixelFog ? 1.0 : 0.0));
    CHECK(current("Display/Gamma ramp") == (ps2.gammaRamp ? 1.0 : 0.0));
    CHECK(current("Display/Reflections") == ps2.reflections);
    CHECK(current("Display/Anti-aliasing") == ps2.antiAliasing);

    // Choosing Xbox through the store: its values land once the settings settle, and the file saves the set.
    coney::DisplaySettings& settings = coney::displaySettings();
    registry.set("Display/Preset", 1.0);
    registry.applyPending();
    settings.settle();
    CHECK(settings.preset() == coney::DisplayPreset::Xbox);
    CHECK(registry.find("Display/Gamma ramp")->current() == 1.0);
    CHECK(registry.find("Display/Renderer")->current() == 1.0);
    const std::string saved = registry.saveText();
    CHECK(saved.find("Display/Preset = 1") != std::string::npos);
    CHECK(saved.find("Display/Gamma ramp = 1") != std::string::npos);

    // One value changed: Custom.
    registry.set("Display/Anti-aliasing", 1.0);
    registry.applyPending();
    settings.settle();
    CHECK(registry.find("Display/Preset")->current() == 4.0);

    // Back to the defaults for the next test in this process.
    registry.resetAll();
    registry.applyPending();
    settings.settle();
    CHECK(settings.preset() == coney::DisplayPreset::Ps2);
}
