// SPDX-License-Identifier: GPL-3.0-or-later
// The World page (docs/guides/debug-menu.md#world): the lighting's layers and the look, the fog, the shadows, the
// humans' gravity and the scene's parts, each with a reset. Coney's own tools (no @orig): the original has no debug
// menu. Each item's help says whether the value it shows or changes is the original's or Coney's.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/display_preset.h"
#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/tunables.h"
#include "graphics/level_lighting.h"
#include "graphics/lighting_frame.h"
#include "graphics/lighting_switches.h"

namespace coney::debug {

namespace {

// What a group that reads the frame shows when no level is drawn.
constexpr std::string_view kNoLevel = "no level is drawn";
// The tunables the page shows: the humans' gravity and the display preset.
constexpr std::string_view kGravityPath = "World/Gravity";
constexpr std::string_view kPresetPath = "Display/Preset";

// The frame the play mode drew last, or null.
const graphics::LightingFrame* frameOf(DebugSession& session) {
    PlayControls* play = session.play();
    return play != nullptr ? play->lightingFrame() : nullptr;
}

// The level's own fog, from the frame the play mode drew last; the device's start (white, from half the far clip)
// when no level is drawn.
graphics::WorldFog levelFog(DebugSession& session) {
    graphics::WorldFog fog;
    if (const graphics::LightingFrame* frame = frameOf(session); frame != nullptr) {
        fog.colour = frame->fogColour;
        fog.start = frame->fogStart;
    }
    return fog;
}

// One channel of `colour`: 0 red, 1 green, 2 blue.
std::uint8_t& channel(graphics::Rgba& colour, std::size_t c) {
    return c == 0 ? colour.r : (c == 1 ? colour.g : colour.b);
}

// The Lighting group: the Lighting page's layers and view, the same switches, and a reset.
std::shared_ptr<MenuPage> lightingPage(DebugSession& session) {
    std::shared_ptr<MenuPage> page = lightingLayersPage(session, "Lighting");
    graphics::LightingSwitches& switches = session.lighting();
    page->add(actionItem("Reset lighting",
                         [&switches] {
                             switches.layers = {};
                             switches.view = graphics::LightingView::Final;
                         }))
        .withHelp("Every layer on and the final view: the light the original gives");
    return page;
}

// The Fog group: the level's fog, and a colour and a start of Coney's own in its place.
std::shared_ptr<MenuPage> fogPage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Fog");
    graphics::FogSwitches& fog = session.lighting().fog;
    page->add(watchItem("Level's fog",
                        [&session] {
                            if (frameOf(session) == nullptr) {
                                return std::string(kNoLevel);
                            }
                            const graphics::WorldFog level = levelFog(session);
                            // Short enough for the pad menu's value column; the help says what the start is.
                            return std::format("({}, {}, {}) from {:.2f}", level.colour.r, level.colour.g,
                                               level.colour.b, level.start);
                        }))
        .withHelp("The original's: the colour the level's scripts set (SetFogColor), also the background, and where "
                  "the fog begins, a fraction of the far clip (SetFogDistance)");
    // The colour's channels, each a number over the colour drawn; a colour equal to the level's is the level's again.
    const graphics::WorldFog atOpen = levelFog(session);
    const std::array<std::string, 3> names{"Red", "Green", "Blue"};
    for (std::size_t c = 0; c < names.size(); ++c) {
        MenuItem item = numberItem(
            names.at(c),
            [&session, &fog, c] {
                graphics::Rgba colour = fog.colour.value_or(levelFog(session).colour);
                return static_cast<double>(channel(colour, c));
            },
            [&session, &fog, c](double value) {
                const graphics::Rgba level = levelFog(session).colour;
                graphics::Rgba colour = fog.colour.value_or(level);
                channel(colour, c) = static_cast<std::uint8_t>(std::lround(value));
                fog.colour = colour == level ? std::nullopt : std::optional<graphics::Rgba>(colour);
            },
            0.0, 255.0, 1.0, true);
        graphics::Rgba level = atOpen.colour;
        item.defaultValue = static_cast<double>(channel(level, c));
        item.help = "Coney's own fog colour in place of the level's; triangle gives the level's channel back";
        page->add(std::move(item));
    }
    MenuItem start = numberItem(
        "Start", [&session, &fog] { return static_cast<double>(fog.start.value_or(levelFog(session).start)); },
        [&session, &fog](double value) {
            const auto wanted = static_cast<float>(value);
            fog.start = wanted == levelFog(session).start ? std::nullopt : std::optional<float>(wanted);
        },
        0.0, static_cast<double>(graphics::kMaxFogStart), 0.01, false);
    start.defaultValue = static_cast<double>(atOpen.start);
    start.help = "Coney's own start in place of the level's, a fraction of the far clip (up to 0.99, where the fog "
                 "would meet the far plane); triangle gives the level's back";
    page->add(std::move(start));
    page->add(actionItem("Reset fog", [&fog] { fog = {}; })).withHelp("The level's fog colour and start");
    return page;
}

// The Shadows group: the blob, the level's shadow light and the shadow maps that wait on the renderer.
std::shared_ptr<MenuPage> shadowsPage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Shadows");
    graphics::LightingLayers& layers = session.lighting().layers;
    page
        ->add(toggleItem(
            "Blob shadows", [&layers] { return layers.blobShadows; }, [&layers](bool on) { layers.blobShadows = on; }))
        .withHelp("The original's PS2 shadow: one faint 1 m blob sprite under each human; off is Coney's switch");
    // The level's shadow light as two rows, colour and offset: one row holding both is wider than the pad menu's
    // value column. `describe` formats the light when the level's scripts set one.
    const auto shadowLight = [&session](auto describe) {
        return [&session, describe] {
            const graphics::LightingFrame* frame = frameOf(session);
            if (frame == nullptr) {
                return std::string(kNoLevel);
            }
            if (!frame->shadow) {
                return std::string("none set");
            }
            return describe(*frame->shadow);
        };
    };
    page->add(watchItem("Shadow colour", shadowLight([](const graphics::FrameShadowLight& light) {
                            return std::format("({:.2f}, {:.2f}, {:.2f})", light.colour.r, light.colour.g,
                                               light.colour.b);
                        })))
        .withHelp("The original's: SetShadowColor from global.lua (none set when the level's scripts set none), "
                  "unused on the PS2 and the Xbox's shadow map source");
    // The frame keeps the offset in RenderWare's axes; shown in the game's (z up).
    page->add(watchItem("Shadow offset", shadowLight([](const graphics::FrameShadowLight& light) {
                            return std::format("({:.0f}, {:.0f}, {:.0f})", light.offset.x, -light.offset.z,
                                               light.offset.y);
                        })))
        .withHelp("The original's: SetShadowLightOffset from global.lua in the game's axes (none set when the level's "
                  "scripts set none), unused on the PS2 and the Xbox's shadow map source");
    page->add(watchItem("Shadows off",
                        [&session] {
                            const graphics::LightingFrame* frame = frameOf(session);
                            return frame != nullptr ? std::to_string(frame->shadowsOff.size()) : std::string(kNoLevel);
                        }))
        .withHelp("The original's: the objects whose shadow the scripts turned off (EnableShadow)");
    page->add(watchItem("Shadow maps",
                        [&session] {
                            const std::optional<double> shadows = session.tunables().value("Display/Shadows");
                            return std::format("not drawn; Display/Shadows {}",
                                               shadows ? std::format("{:.0f}", *shadows) : std::string("unset"));
                        }))
        .withHelp("The Xbox's shadow map and Coney's realtime shadows (docs/guides/realtime-lighting.md): not drawn "
                  "yet (#1259, #1220), both off in test mode");
    page->add(actionItem("Reset shadows", [&layers] { layers.blobShadows = true; }))
        .withHelp("The blob shadows back on");
    return page;
}

// A Parts toggle over the part `field`, with its help.
MenuItem partToggle(graphics::WorldParts& parts, std::string label, bool graphics::WorldParts::* field,
                    std::string help) {
    MenuItem item = toggleItem(
        std::move(label), [&parts, field] { return parts.*field; }, [&parts, field](bool on) { parts.*field = on; });
    item.help = std::move(help);
    return item;
}

// The Parts group: each part of the scene on and off.
std::shared_ptr<MenuPage> partsPage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Parts");
    graphics::LightingSwitches& switches = session.lighting();
    graphics::WorldParts& parts = switches.parts;
    page->add(partToggle(parts, "Streamed world", &graphics::WorldParts::scenery,
                         "Coney's switch: the level's sectors; off hides the background too"));
    page->add(partToggle(parts, "Background", &graphics::WorldParts::background,
                         "Coney's switch: the sky box, the clouds, the skyline and the light glows"));
    page->add(partToggle(parts, "Humans", &graphics::WorldParts::humans,
                         "Coney's switch: every human, with what he wears and holds"));
    page->add(partToggle(parts, "Objects", &graphics::WorldParts::objects,
                         "Coney's switch: the world objects, the doors and the cars"));
    page->add(partToggle(parts, "Glass", &graphics::WorldParts::glass, "Coney's switch: the glass panes"));
    page->add(partToggle(parts, "Water", &graphics::WorldParts::water, "Coney's switch: the water"));
    page->add(partToggle(parts, "Effects", &graphics::WorldParts::effects,
                         "Coney's switch: the particles, the glints and the throw-aim arc"));
    page->add(partToggle(parts, "Health rings", &graphics::WorldParts::rings,
                         "Coney's switch: the health rings and the L1 markers"));
    graphics::LightingLayers& layers = switches.layers;
    page->add(toggleItem(
                  "Coronas", [&layers] { return layers.coronas; }, [&layers](bool on) { layers.coronas = on; }))
        .withHelp("Coney's switch: the lamps' corona sprites (the Lighting page's layer)");
    page
        ->add(toggleItem(
            "Blob shadows", [&layers] { return layers.blobShadows; }, [&layers](bool on) { layers.blobShadows = on; }))
        .withHelp("Coney's switch: the humans' blob shadows (the Lighting page's layer)");
    page->add(actionItem("All on",
                         [&switches] {
                             switches.parts = {};
                             switches.layers.coronas = true;
                             switches.layers.blobShadows = true;
                         }))
        .withHelp("Every part drawn, as the game draws it");
    return page;
}

} // namespace

// The menu callbacks below copy strings and call the session; all they can throw is a failed allocation, which ends
// the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

void addWorldPage(DebugSession& session) {
    session.model().addPage(
        "World",
        [&session](MenuPage& page) {
            TunableRegistry& registry = session.tunables();
            page.add(submenuItem("Lighting", [&session] { return lightingPage(session); }))
                .withHelp("The lighting's layers and view (the Lighting page's), with a reset");
            // The look: the display preset, queued like any tunable.
            if (registry.find(kPresetPath) != nullptr) {
                page.add(choiceItem(
                             "Look", {"PS2", "Xbox", "Enhanced", "High", "Custom"},
                             [&registry] {
                                 return static_cast<std::size_t>(std::lround(registry.value(kPresetPath).value_or(0)));
                             },
                             [&registry](std::size_t choice) {
                                 // Custom is shown, never chosen: choosing it leaves the values as they are.
                                 if (choice < static_cast<std::size_t>(kChoosablePresets)) {
                                     registry.set(kPresetPath, static_cast<double>(choice));
                                 }
                             }))
                    .withHelp("The display preset (Tunables, Display/Preset): PS2 is the original's picture and "
                              "the only one drawn yet; choose PS2 to go back");
            } else {
                page.add(watchItem("Look", [] { return std::string("no display preset registered"); }));
            }
            page.add(submenuItem("Fog", [&session] { return fogPage(session); }))
                .withHelp("The level's fog, and a colour and a start of Coney's own");
            page.add(submenuItem("Shadows", [&session] { return shadowsPage(session); }))
                .withHelp("The blob shadows, the level's shadow light and the shadow maps");
            // The humans' gravity, queued like any tunable and applied between two steps.
            if (const Tunable* gravity = registry.find(kGravityPath); gravity != nullptr) {
                MenuItem item = numberItem(
                    "Gravity", [&registry] { return registry.value(kGravityPath).value_or(0.0); },
                    [&registry](double value) { registry.set(kGravityPath, value); }, gravity->min(), gravity->max(),
                    gravity->step(), false);
                item.units = gravity->unitText();
                item.defaultValue = gravity->defaultValue();
                item.help = "What pulls an airborne human down: the original's 15.68 m/s2 (1.6 g) by default; another "
                            "value is Coney's own, made up after each humans' update. Humans only";
                page.add(std::move(item));
            } else {
                page.add(watchItem("Gravity", [] { return std::string("no gravity registered"); }));
            }
            page.add(submenuItem("Parts", [&session] { return partsPage(session); }))
                .withHelp("Each part of the scene on and off");
            page.add(actionItem("Reset world",
                                [&session, &registry] {
                                    graphics::LightingSwitches& switches = session.lighting();
                                    switches.layers = {};
                                    switches.view = graphics::LightingView::Final;
                                    switches.fog = {};
                                    switches.parts = {};
                                    if (const Tunable* gravity = registry.find(kGravityPath); gravity != nullptr) {
                                        registry.set(kGravityPath, gravity->defaultValue());
                                    }
                                }))
                .withHelp("The lighting, the fog, the shadows, the gravity and the parts as the original has them");
            // A short value the pad menu shows whole; the help says why each is missing.
            page.add(watchItem("Not here", [] { return std::string("time of day, HUD, object gravity"); }))
                .withHelp("No time of day: a level's lights are its scripts'. The HUD's parts are on the HUD page. "
                          "The gravity of objects and particles is not a setting yet");
        },
        "The world's lighting, fog, shadows, gravity and parts, live (docs/guides/debug-menu.md#world).");
}

// NOLINTEND(bugprone-exception-escape)

} // namespace coney::debug
