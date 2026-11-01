// SPDX-License-Identifier: GPL-3.0-or-later
// The display presets (docs/engine/renderer.md#presets): the table of each preset's values, Custom on a changed value,
// and the pin that keeps test mode on the PS2 picture.
#include <optional>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "core/display_preset.h"

using coney::DisplayPreset;
using coney::DisplaySettings;
using coney::DisplayValues;
using coney::presetMatching;
using coney::presetValues;

TEST_CASE("each display preset sets the values of the presets table", "[display][presets]") {
    // Expected: ../engine/renderer.md#presets, Coney's own design: one column of the table per preset.
    // PS2 is today's picture: every option off, so its values are the defaults.
    const DisplayValues ps2 = presetValues(DisplayPreset::Ps2);
    CHECK(ps2 == DisplayValues{});
    CHECK(ps2.renderer == 0);
    CHECK(ps2.blobShadow);
    CHECK_FALSE(ps2.perPixelFog);
    CHECK_FALSE(ps2.gammaRamp);

    const DisplayValues xbox = presetValues(DisplayPreset::Xbox);
    CHECK(xbox == DisplayValues{.renderer = 1,
                                .lights = 1,
                                .shadows = 1,
                                .blobShadow = false,
                                .perPixelFog = true,
                                .gammaRamp = true,
                                .reflections = 0,
                                .antiAliasing = 0});
    const DisplayValues enhanced = presetValues(DisplayPreset::Enhanced);
    CHECK(enhanced == DisplayValues{.renderer = 1,
                                    .lights = 2,
                                    .shadows = 2,
                                    .blobShadow = false,
                                    .perPixelFog = true,
                                    .gammaRamp = true,
                                    .reflections = 1,
                                    .antiAliasing = 1});
    const DisplayValues high = presetValues(DisplayPreset::High);
    CHECK(high == DisplayValues{.renderer = 1,
                                .lights = 3,
                                .shadows = 3,
                                .blobShadow = false,
                                .perPixelFog = true,
                                .gammaRamp = true,
                                .reflections = 2,
                                .antiAliasing = 2});
}

TEST_CASE("a preset's values read back as that preset and any other values as Custom", "[display][presets]") {
    // Expected: ../engine/renderer.md#presets, Coney's own design: Custom is the player's own set, not a fifth preset.
    for (const DisplayPreset preset :
         {DisplayPreset::Ps2, DisplayPreset::Xbox, DisplayPreset::Enhanced, DisplayPreset::High}) {
        CHECK(presetMatching(presetValues(preset)) == preset);
    }
    DisplayValues xboxWithSmaa = presetValues(DisplayPreset::Xbox);
    xboxWithSmaa.antiAliasing = 1;
    CHECK(presetMatching(xboxWithSmaa) == DisplayPreset::Custom);
    DisplayValues ps2WithoutBlob;
    ps2WithoutBlob.blobShadow = false;
    CHECK(presetMatching(ps2WithoutBlob) == DisplayPreset::Custom);
}

TEST_CASE("the preset names are the ones the preset option takes", "[display][presets]") {
    // Expected: ../guides/enhancements.md#where-the-settings-live, Coney's own option: --preset ps2|xbox|enhanced|high
    CHECK(coney::parseDisplayPresetName("ps2") == std::optional<DisplayPreset>(DisplayPreset::Ps2));
    CHECK(coney::parseDisplayPresetName("xbox") == std::optional<DisplayPreset>(DisplayPreset::Xbox));
    CHECK(coney::parseDisplayPresetName("enhanced") == std::optional<DisplayPreset>(DisplayPreset::Enhanced));
    CHECK(coney::parseDisplayPresetName("high") == std::optional<DisplayPreset>(DisplayPreset::High));
    CHECK_FALSE(coney::parseDisplayPresetName("custom").has_value());
    CHECK_FALSE(coney::parseDisplayPresetName("PS2").has_value());
    CHECK_FALSE(coney::parseDisplayPresetName("").has_value());
    CHECK(coney::displayPresetName(DisplayPreset::Enhanced) == std::string_view("enhanced"));
    CHECK(coney::displayPresetName(DisplayPreset::Custom) == std::string_view("custom"));
    // Display/Preset's values: 0 to 3 the presets, the rest Custom.
    CHECK(coney::presetOfSetting(0) == DisplayPreset::Ps2);
    CHECK(coney::presetOfSetting(3) == DisplayPreset::High);
    CHECK(coney::presetOfSetting(4) == DisplayPreset::Custom);
    CHECK(coney::presetOfSetting(-1) == DisplayPreset::Custom);
}

TEST_CASE("choosing a preset writes its values and changing one value afterwards shows Custom", "[display][presets]") {
    // Expected: ../engine/renderer.md#presets, Coney's own design: a changed value shows the setting as Custom.
    int renderer = 0;
    DisplaySettings settings(renderer);
    CHECK(settings.preset() == DisplayPreset::Ps2);
    CHECK(settings.current() == presetValues(DisplayPreset::Ps2));

    // The store writes Display/Preset = 1; the next settle writes Xbox's values, the renderer's too.
    settings.presetSetting() = 1;
    settings.settle();
    CHECK(settings.preset() == DisplayPreset::Xbox);
    CHECK(settings.current() == presetValues(DisplayPreset::Xbox));
    CHECK(renderer == 1);

    // One value changed afterwards: Custom, and the values stay as the player set them.
    settings.values().antiAliasing = 1;
    settings.settle();
    CHECK(settings.preset() == DisplayPreset::Custom);
    CHECK(settings.presetSetting() == 4);
    CHECK(settings.current().antiAliasing == 1);
    CHECK(settings.current().gammaRamp);

    // Set back, the values are Xbox's again, and so is the preset.
    settings.values().antiAliasing = 0;
    settings.settle();
    CHECK(settings.preset() == DisplayPreset::Xbox);

    // The renderer is a value like the others.
    renderer = 0;
    settings.settle();
    CHECK(settings.preset() == DisplayPreset::Custom);

    // Choosing Custom itself changes no value; choosing a preset sets all of its values.
    settings.choose(DisplayPreset::Custom);
    CHECK(settings.current().renderer == 0);
    settings.choose(DisplayPreset::Ps2);
    CHECK(settings.preset() == DisplayPreset::Ps2);
    CHECK(settings.current() == DisplayValues{});
    CHECK(renderer == 0);
}

TEST_CASE("a preset and a value changed in the same batch keep the value", "[display][presets]") {
    // Expected: ../engine/renderer.md#presets, Coney's own design: an overrides file that saves Xbox and one value of
    // its own reads back as that set, Custom.
    int renderer = 0;
    DisplaySettings settings(renderer);
    settings.presetSetting() = 1;
    settings.values().antiAliasing = 1;
    settings.settle();
    CHECK(settings.preset() == DisplayPreset::Custom);
    CHECK(settings.current().antiAliasing == 1);
    CHECK(settings.current().shadows == 1);
    CHECK(settings.current().gammaRamp);

    // A file that saves Enhanced and its values (they differ from the defaults, so they are saved) reads back as
    // Enhanced.
    int otherRenderer = 0;
    DisplaySettings saved(otherRenderer);
    saved.presetSetting() = 2;
    saved.values() = presetValues(DisplayPreset::Enhanced);
    otherRenderer = 1;
    saved.settle();
    CHECK(saved.preset() == DisplayPreset::Enhanced);
    CHECK(saved.current() == presetValues(DisplayPreset::Enhanced));
    CHECK(otherRenderer == 1);
}

TEST_CASE("a preset chosen for the next settle wins over the saved settings", "[display][presets]") {
    // Expected: ../guides/enhancements.md#where-the-settings-live, Coney's own option: --preset sets the preset's
    // values exactly, whatever the overrides file loaded at the start says.
    int renderer = 0;
    DisplaySettings settings(renderer);
    settings.chooseAtNextSettle(DisplayPreset::High);
    // What the overrides file wrote in the same batch: Xbox with one value of its own.
    settings.presetSetting() = 1;
    settings.values().reflections = 1;
    settings.settle();
    CHECK(settings.preset() == DisplayPreset::High);
    CHECK(settings.current() == presetValues(DisplayPreset::High));
    // Only once: a later change is the player's.
    settings.values().antiAliasing = 0;
    settings.settle();
    CHECK(settings.preset() == DisplayPreset::Custom);
}

TEST_CASE("a pinned preset is what drawing reads whatever the settings say", "[display][presets][test-mode]") {
    // Expected: ../engine/renderer.md#ps2-preset, Coney's own design: test mode forces PS2.
    int renderer = 0;
    DisplaySettings settings(renderer);
    settings.choose(DisplayPreset::High);
    CHECK(settings.drawn() == presetValues(DisplayPreset::High));
    settings.pin(DisplayPreset::Ps2);
    CHECK(settings.pinned() == std::optional<DisplayPreset>(DisplayPreset::Ps2));
    CHECK(settings.drawn() == presetValues(DisplayPreset::Ps2));
    // The player's settings are kept as they are, only not drawn.
    CHECK(settings.preset() == DisplayPreset::High);
    settings.values().antiAliasing = 0;
    settings.settle();
    CHECK(settings.drawn() == presetValues(DisplayPreset::Ps2));
    // `--preset xbox` in test mode pins Xbox instead.
    settings.pin(DisplayPreset::Xbox);
    CHECK(settings.drawn() == presetValues(DisplayPreset::Xbox));
    settings.pin(std::nullopt);
    CHECK(settings.drawn() == settings.current());
}
