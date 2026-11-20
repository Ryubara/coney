// SPDX-License-Identifier: GPL-3.0-or-later
// The Lighting page, the lighting lab (docs/guides/realtime-lighting.md#lab): a frame's lighting watched, switched
// and dumped. Coney's own tools (no @orig): the original has no debug menu.
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/lighting_lab.h"
#include "graphics/light_manager.h"
#include "graphics/lighting_frame.h"
#include "graphics/lighting_switches.h"

namespace coney::debug {

namespace {

// What a group that reads the frame shows when no level is drawn.
constexpr std::string_view kNoLevel = "no level is drawn";

// The frame the play mode drew last, or null.
const graphics::LightingFrame* frameOf(DebugSession& session) {
    PlayControls* play = session.play();
    return play != nullptr ? play->lightingFrame() : nullptr;
}

// The play mode's light manager, or null.
graphics::LightManager* managerOf(DebugSession& session) {
    PlayControls* play = session.play();
    return play != nullptr ? play->lightManager() : nullptr;
}

// The Selection group's atomic this frame: the Entity inspector's selection, or the player; null for none.
const graphics::FrameSelection* selectedOf(DebugSession& session) {
    PlayControls* play = session.play();
    const graphics::LightingFrame* frame = frameOf(session);
    if (play == nullptr || frame == nullptr) {
        return nullptr;
    }
    const EntityInspect* inspect = play->inspect();
    const std::vector<EntityRef> entities = inspect != nullptr ? inspect->entities() : std::vector<EntityRef>{};
    return labSelection(*frame, entities, session.debugDraw().inspected, play->playerFeet());
}

// A watch whose value is `text` of the counts now, or "-" when no level is drawn.
MenuItem countWatch(DebugSession& session, std::string label, std::string (*text)(const LightingCounts&)) {
    return watchItem(std::move(label), [&session, text] {
        const graphics::LightingFrame* frame = frameOf(session);
        const graphics::LightManager* manager = managerOf(session);
        return frame != nullptr && manager != nullptr ? text(lightingCounts(*frame, *manager)) : std::string("-");
    });
}

// The Frame group: Hold lighting, Light from, Dump this frame.
std::shared_ptr<MenuPage> framePage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Frame");
    graphics::LightingSwitches& switches = session.lighting();
    page->add(toggleItem(
                  "Hold lighting", [&switches] { return switches.hold; }, [&switches](bool on) { switches.hold = on; }))
        .withHelp("Keep the flicker, the pulse and every selection of this step while paused; Step one moves it on");
    page->add(choiceItem(
                  "Light from", {"game camera", "view camera"},
                  [&switches] { return std::size_t{switches.lightFromGameCamera ? 0U : 1U}; },
                  [&switches](std::size_t choice) { switches.lightFromGameCamera = choice == 0; }))
        .withHelp("Cull the lights from the game's camera (as the original) or the camera drawn through; the caps stay "
                  "the drawn camera's");
    page->add(actionItem("Dump this frame",
                         [&session] {
                             const graphics::LightingFrame* frame = frameOf(session);
                             if (frame == nullptr) {
                                 session.print(std::format("lighting: {}", kNoLevel));
                                 return;
                             }
                             const std::filesystem::path& folder = session.services().runLogFolder;
                             if (folder.empty()) {
                                 session.print("lighting: this run has no log folder to dump into (--log-file)");
                                 return;
                             }
                             const auto written = writeLightingDump(*frame, folder);
                             if (!written) {
                                 session.print("lighting: " + written.error().message);
                                 return;
                             }
                             session.notice(std::format("lighting: dumped {} selections and {} lights to {}",
                                                        frame->selections.size(), frame->lights.size(),
                                                        written->string()));
                         }))
        .withHelp("Write the frame's lights and every atomic's selection, in the original trace's columns, to the "
                  "run log's folder");
    return page;
}

// The Counts group.
std::shared_ptr<MenuPage> countsPage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Counts");
    if (frameOf(session) == nullptr || managerOf(session) == nullptr) {
        page->add(watchItem("No level", [] { return std::string(kNoLevel); }));
        return page;
    }
    page->add(countWatch(session, "Light records", [](const LightingCounts& c) {
        return std::format("{} of {}", c.records, graphics::kLightCapacity);
    }));
    page->add(countWatch(session, "Point lights", [](const LightingCounts& c) {
        return std::format("{} (world {})", c.points, c.worldPoints);
    }));
    page->add(countWatch(session, "Coronas", [](const LightingCounts& c) { return std::to_string(c.coronas); }));
    page->add(countWatch(session, "Atomics lit", [](const LightingCounts& c) { return std::to_string(c.atomics); }));
    page->add(countWatch(session, "Humans by lights", [](const LightingCounts& c) {
        std::string text = std::format("3: {}, 4: {}, 5: {}, 6: {}", c.humansByLights[0], c.humansByLights[1],
                                       c.humansByLights[2], c.humansByLights[3]);
        if (c.humansOther != 0) {
            text += std::format(", other: {}", c.humansOther);
        }
        return text;
    }));
    page->add(countWatch(session, "Full lists", [](const LightingCounts& c) { return std::to_string(c.full); }))
        .withHelp("Selections whose list reached its cap");
    return page;
}

// One light's page: its line and its edits, queued and applied between two steps.
std::shared_ptr<MenuPage> lightPage(DebugSession& session, std::uint16_t index, std::string title) {
    auto page = std::make_shared<MenuPage>(std::move(title));
    // The record now, or null when the light is gone.
    const auto record = [&session, index]() -> const graphics::LightRecord* {
        const graphics::LightManager* manager = managerOf(session);
        return manager != nullptr ? manager->light(static_cast<graphics::LightHandle>(index) + 1) : nullptr;
    };
    page->add(watchItem("Light", [&session, index, record] {
        const graphics::LightManager* manager = managerOf(session);
        return manager != nullptr && record() != nullptr ? lightLine(*manager, index) : std::string("gone");
    }));
    page->add(toggleItem(
                  "On", [record] { return record() != nullptr && record()->desc.on; },
                  [&session, index](bool on) { session.lightEdits().push(LightEdit{.record = index, .on = on}); }))
        .withHelp("A debug edit, applied between two steps");
    page->add(numberItem(
                  "Radius", [record] { return record() != nullptr ? static_cast<double>(record()->desc.radius) : 0.0; },
                  [&session, index](double radius) {
                      session.lightEdits().push(LightEdit{.record = index, .radius = static_cast<float>(radius)});
                  },
                  0.0, 100.0, 0.1, false))
        .units = "m";
    // The base colour's components, each its own edit.
    const std::array<std::string, 3> names{"Red", "Green", "Blue"};
    for (std::size_t c = 0; c < names.size(); ++c) {
        const auto component = [c](const graphics::LightColour& colour) {
            return c == 0 ? colour.r : (c == 1 ? colour.g : colour.b);
        };
        page->add(numberItem(
            names.at(c),
            [record, component] {
                return record() != nullptr ? static_cast<double>(component(record()->desc.colour)) : 0.0;
            },
            [&session, index, c](double value) {
                LightEdit edit{.record = index};
                edit.colour.at(c) = static_cast<float>(value);
                session.lightEdits().push(edit);
            },
            0.0, 2.0, 0.01, false));
    }
    LightingDrawOptions& draw = session.debugDraw().lighting;
    page->add(toggleItem(
                  "Marked in the scene", [&draw, index] { return draw.marked == static_cast<std::int32_t>(index); },
                  [&draw, index](bool on) { draw.marked = on ? static_cast<std::int32_t>(index) : -1; }))
        .withHelp("A yellow cross at the light");
    page->add(watchItem("View camera to it",
                        [] { return std::string("not built yet: the free camera has no place to set"); }));
    return page;
}

// The Lights group: every record in use, each with its source.
std::shared_ptr<MenuPage> lightsPage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Lights");
    const graphics::LightManager* manager = managerOf(session);
    if (manager == nullptr) {
        page->add(watchItem("No level", [] { return std::string(kNoLevel); }));
        return page;
    }
    for (std::size_t i = 0; i < graphics::kLightCapacity; ++i) {
        const auto index = static_cast<std::uint16_t>(i);
        const graphics::LightRecord& record = manager->record(index);
        if (!record.inUse) {
            continue;
        }
        std::string label = std::format("#{} {}", index, lightKindName(record.desc.type, record.desc.radius));
        MenuItem item = submenuItem(label, [&session, index, label] { return lightPage(session, index, label); });
        item.detail = lightOriginName(manager->origin(index), index);
        item.help = "Its line, on and off, radius and colour as debug edits, and a mark in the scene";
        page->add(std::move(item));
    }
    return page;
}

// The Selection group: the inspected entity's (or the player's) lights this frame, in order.
std::shared_ptr<MenuPage> selectionPage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Selection");
    page->add(watchItem("Atomic",
                        [&session] {
                            if (frameOf(session) == nullptr) {
                                return std::string(kNoLevel);
                            }
                            const graphics::FrameSelection* selection = selectedOf(session);
                            return selection != nullptr ? selectionSummary(*selection)
                                                        : std::string("none drawn this frame");
                        }))
        .withHelp("The Entity inspector's selection, or the player when nothing is inspected");
    page->add(logItem("Lights", [&session] {
        const graphics::LightingFrame* frame = frameOf(session);
        const graphics::FrameSelection* selection = selectedOf(session);
        return frame != nullptr && selection != nullptr ? selectionLines(*selection, *frame)
                                                        : std::vector<std::string>{};
    }));
    // With --light-trace, the original's lights for the same atomic, matched as trace diff matches them.
    if (!session.services().lightTrace.empty()) {
        page->add(logItem("Original's lights", [&session] {
            const graphics::FrameSelection* selection = selectedOf(session);
            return selection != nullptr ? originalSelectionLines(session.services().lightTrace, *selection)
                                        : std::vector<std::string>{};
        }));
    }
    page->add(watchItem("Sector under the view", [] { return std::string("not built yet"); }));
    return page;
}

// A Layers toggle over the switch `field`.
MenuItem layerToggle(DebugSession& session, std::string label, bool graphics::LightingLayers::* field) {
    graphics::LightingLayers& layers = session.lighting().layers;
    return toggleItem(
        std::move(label), [&layers, field] { return layers.*field; },
        [&layers, field](bool on) { layers.*field = on; });
}

} // namespace

std::shared_ptr<MenuPage> lightingLayersPage(DebugSession& session, std::string title) {
    auto page = std::make_shared<MenuPage>(std::move(title));
    page->add(layerToggle(session, "World ambient (A)", &graphics::LightingLayers::worldAmbient));
    page->add(layerToggle(session, "Ambient and directional", &graphics::LightingLayers::objectLights))
        .withHelp("The ambient and directional lights other than light A");
    page->add(layerToggle(session, "Point lights", &graphics::LightingLayers::pointLights));
    page->add(layerToggle(session, "Flicker", &graphics::LightingLayers::flicker))
        .withHelp("Off: flickering lights shine with their base colour");
    page->add(layerToggle(session, "Brightness and offset", &graphics::LightingLayers::brightness));
    page->add(layerToggle(session, "Coronas", &graphics::LightingLayers::coronas));
    page->add(layerToggle(session, "Blob shadows", &graphics::LightingLayers::blobShadows));
    graphics::LightingSwitches& switches = session.lighting();
    page->add(choiceItem(
        "View", {"final", "prelight only"}, [&switches] { return static_cast<std::size_t>(switches.view); },
        [&switches](std::size_t choice) { switches.view = static_cast<graphics::LightingView>(choice); }));
    page->add(watchItem("Not built yet", [] {
        return std::string("prelighting off, runtime light only, runtime share, saturation and lights per atomic "
                           "(Coney's shaders, renderer item 22); the sun (#1257)");
    }));
    return page;
}

namespace {

// The Debug draw group: the lab's lines in the scene.
std::shared_ptr<MenuPage> drawPage(DebugSession& session) {
    auto page = std::make_shared<MenuPage>("Debug draw");
    LightingDrawOptions& draw = session.debugDraw().lighting;
    // A toggle over the switch `field`.
    const auto toggle = [&draw](std::string label, bool LightingDrawOptions::* field) {
        return toggleItem(
            std::move(label), [&draw, field] { return draw.*field; }, [&draw, field](bool on) { draw.*field = on; });
    };
    page->add(toggle("Point lights", &LightingDrawOptions::pointLights)).withHelp("Spheres of their radius");
    page->add(toggle("Corona-only lights", &LightingDrawOptions::coronaOnly)).withHelp("Crosses");
    page->add(toggle("Directional lights", &LightingDrawOptions::directional)).withHelp("Arrows from the light view");
    page->add(toggle("Cull reach", &LightingDrawOptions::cullReach))
        .withHelp("Three quarters of the draw distance round the light view");
    page->add(toggle("Selection", &LightingDrawOptions::selection))
        .withHelp("The Selection group's sphere and a line to each light it chose");
    return page;
}

} // namespace

// The menu callbacks below copy strings and call the session; all they can throw is a failed allocation, which ends
// the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

void addLightingPage(DebugSession& session) {
    session.model().addPage(
        "Lighting",
        [&session](MenuPage& page) {
            page.add(submenuItem("Frame", [&session] { return framePage(session); }))
                .withHelp("Hold lighting, Light from, Dump this frame");
            page.add(submenuItem("Counts", [&session] { return countsPage(session); }));
            page.add(submenuItem("Lights", [&session] { return lightsPage(session); }))
                .withHelp("Every light record with its source");
            page.add(submenuItem("Selection", [&session] { return selectionPage(session); }))
                .withHelp("An atomic's lights this frame, in order");
            page.add(submenuItem("Layers", [&session] { return lightingLayersPage(session, "Layers"); }))
                .withHelp("Switches on the drawing side, and the view");
            page.add(submenuItem("Debug draw", [&session] { return drawPage(session); }));
            page.add(watchItem("Shadows", [] {
                return std::string("not built yet: the shadow maps (renderer items 28 and 30, #1259)");
            }));
            page.add(watchItem("Original's trace",
                               [&session] {
                                   const DebugServices& services = session.services();
                                   if (services.lightTrace.empty()) {
                                       return std::string("none loaded (--light-trace FILE)");
                                   }
                                   return std::format("{} calls from {}", services.lightTrace.size(),
                                                      services.lightTraceName);
                               }))
                .withHelp("The original's light selections from coney-tools trace lights; the Selection group shows "
                          "the original's lights for its atomic");
        },
        "The lighting lab: how a frame is lit (docs/guides/realtime-lighting.md#lab).");
}

// NOLINTEND(bugprone-exception-escape)

} // namespace coney::debug
