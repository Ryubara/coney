// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/render_device.h"
#include "world/world_streams.h"

namespace coney::graphics {

/// One light of a frame, as a renderer needs it: a light record of the manager with this step's colour.
struct FrameLight {
    std::uint16_t record = 0; ///< The record's pool index: the light's id for the level's lifetime.
    LightType type = LightType::Point;
    std::uint16_t lights = 0;  ///< What it lights: kLightsObjects | kLightsWorld.
    LightColour colour;        ///< What it shines with now: brightness, offset and flicker applied.
    LightColour base;          ///< The base colour the script gave it.
    world::Vec3 position;      ///< Point and spot lights, RenderWare's axes.
    world::Vec3 direction;     ///< Spot and directional lights: the way the light travels (unit).
    float radius = 0.0F;       ///< Point and spot lights, metres.
    float coneAngle = 0.0F;    ///< Spot lights.
    std::uint32_t flicker = 0; ///< Its flicker mode (the effects word & kEffectFlickerMask), 0 for none.
    std::int16_t corona = -1;  ///< Its corona's rectangle of the `lighting` sheet, -1 for none.
};

/// The level's shadow light as a frame carries it: in RenderWare's axes, with the direction its light travels.
struct FrameShadowLight {
    LightColour colour;    ///< The shadow colour (ShadowLight::colour).
    world::Vec3 offset;    ///< The light's place from the casters' centre, RenderWare's axes (y up).
    world::Vec3 direction; ///< The way the light travels: the offset reversed, unit.
};

/// Which draw chose an atomic's lights: the callers of the selection the original's trace tells apart by their return
/// address (docs/research/lighting.md#select).
enum class SelectionCaller : std::uint8_t {
    Sector,     ///< A streamed sector (`0x00411990`, returns to `0x00411aa8`).
    Background, ///< The level's background (`0x0040d0a8`, returns to `0x0040d1a8`).
    Human,      ///< A human (`0x00174320`, returns to `0x001746f8`).
    Object,     ///< A world object (`0x0017fd78`, returns to `0x001803a4`); Coney's cars are drawn as objects too.
};

/// One light an atomic chose, as it was when chosen: the values the original's trace reads from each RpLight of the
/// upload list (type, radius, colour, position), so a dump compares with it value for value.
struct ChosenLight {
    std::uint16_t record = 0; ///< The record's pool index.
    LightType type = LightType::Point;
    float radius = 0.0F;
    LightColour colour;   ///< What it shone with when chosen.
    world::Vec3 position; ///< RenderWare's axes; the glow's is the human it was put at.
};

/// One call of the selection in a frame: the caller, the inputs the selection used and the lights it chose in order,
/// the row a trace of the original records per call (docs/research/lighting.md#select).
struct FrameSelection {
    SelectionCaller caller = SelectionCaller::Sector;
    float distSq = 0.0F;      ///< The squared distance to the camera the cap was read from.
    LightSphere sphere;       ///< The atomic's sphere, RenderWare's axes.
    bool objects = false;     ///< The objects' lists (flags bit 0), else the world's.
    bool pointLights = false; ///< Point lights were looked for.
    bool pulse = false;       ///< List C in place of list B.
    bool human = false;       ///< A human's call, which may add the glow.
    std::size_t cap = 0;      ///< The cap the selection read for the point lights.
    std::array<ChosenLight, kMaxSelectedLights> lights{};
    std::size_t count = 0; ///< Lights chosen.

    /// The lights chosen, in order.
    [[nodiscard]] std::span<const ChosenLight> chosen() const { return {lights.data(), count}; }
    /// Whether the list reached its cap.
    [[nodiscard]] bool full() const { return count >= cap; }
};

/// The render-side description of one viewport's lighting: what a renderer and the lighting lab read, built after the
/// light manager's cull from state the faithful path already computes, and never read back by the simulation.
///
/// It holds the lights the viewport's cull kept (LightManager::beginViewport()) with this step's flicker colour, the
/// cull's lists, the world ambient and the fog, the level's shadow light and the objects whose shadow is off, and, as
/// the frame is drawn, each atomic's selection. The project's sun joins it later
/// (docs/guides/realtime-lighting.md#the-lighting-frame).
///
/// Research: docs/engine/renderer.md#lighting, docs/research/xbox-lighting.md#shadow-light
struct LightingFrame {
    LightView view;                         ///< The camera the cull ran for.
    std::vector<FrameLight> lights;         ///< Every light in one of the lists below, once, in record order.
    std::vector<std::uint16_t> worldList;   ///< List A: ambient and directional lights of the world (records).
    std::vector<std::uint16_t> objectList;  ///< List B: of objects.
    std::vector<std::uint16_t> pulseList;   ///< List C: of objects, with the pulse.
    std::vector<std::uint16_t> points;      ///< The visible point and spot lights.
    std::vector<std::uint16_t> worldPoints; ///< Those of them that light the world.
    LightColour worldAmbient;               ///< The world ambient's colour now.
    Rgba fogColour;                         ///< The fog, which is also the background.
    float fogStart = 0.5F;                  ///< Where the fog begins, a fraction of the far clip.
    std::optional<FrameShadowLight> shadow; ///< The level's shadow light; none when its scripts set none.
    std::vector<std::uint32_t> shadowsOff;  ///< The objects (script handles) whose shadow is off, in order.
    std::size_t coronas = 0;                ///< Corona sprites the cull made for the frame.
    std::vector<FrameSelection> selections; ///< Every atomic's selection, in the order the frame drew them.

    /// The light of record `record`, or null when the frame does not hold it.
    [[nodiscard]] const FrameLight* light(std::uint16_t record) const;
};

/// The cap the selection reads for a call with `distSq`, `objects` and `pulse`: 8 for the world, else 6 less one per
/// 1600 m² of squared distance, down to 3 (4 with the pulse). The rule of LightManager::select(), restated for the
/// lab's count of full lists (docs/research/lighting.md#select).
/// @coney-only the lighting lab's counts; the original computes the cap inside the selection
[[nodiscard]] std::size_t selectionCap(float distSq, bool objects, bool pulse);

/// The frame's row for one call of the selection: the caller, its inputs, the cap and the lights of `chosen` as
/// `manager` holds them now (call it right after LightManager::select(), before another call moves the glow).
/// @coney-only the render-side lighting frame
[[nodiscard]] FrameSelection frameSelectionOf(const LightManager& manager, SelectionCaller caller, float distSq,
                                              const LightSphere& sphere, bool objects, bool pointLights, bool pulse,
                                              bool human, const LightSelection& chosen);

/// The frame of `level` for `view`: call it after `level.lights.beginViewport(view, ...)`, whose lists it copies.
/// Reads `level` only.
/// @coney-only the render-side lighting frame; the original hands its lights to RenderWare directly
[[nodiscard]] LightingFrame buildLightingFrame(const LevelLighting& level, const LightView& view);

/// The shadow light of a frame from the level's: none when its scripts set none; else the colour, the offset turned
/// into RenderWare's axes and the direction its light travels.
/// @coney-only the render-side lighting frame; the Xbox reads its shadow object's fields where it draws
[[nodiscard]] std::optional<FrameShadowLight> frameShadowLight(const ShadowLight& shadow);

} // namespace coney::graphics
