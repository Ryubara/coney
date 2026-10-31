// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
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

/// The render-side description of one viewport's lighting: what a renderer and the lighting lab read, built after the
/// light manager's cull from state the faithful path already computes, and never read back by the simulation.
///
/// It holds the lights the viewport's cull kept (LightManager::beginViewport()) with this step's flicker colour, the
/// cull's lists, the world ambient and the fog, the level's shadow light and the objects whose shadow is off. The
/// lights each atomic chose and the project's sun join it later (docs/guides/realtime-lighting.md#the-lighting-frame).
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

    /// The light of record `record`, or null when the frame does not hold it.
    [[nodiscard]] const FrameLight* light(std::uint16_t record) const;
};

/// The frame of `level` for `view`: call it after `level.lights.beginViewport(view, ...)`, whose lists it copies.
/// Reads `level` only.
/// @coney-only the render-side lighting frame; the original hands its lights to RenderWare directly
[[nodiscard]] LightingFrame buildLightingFrame(const LevelLighting& level, const LightView& view);

/// The shadow light of a frame from the level's: none when its scripts set none; else the colour, the offset turned
/// into RenderWare's axes and the direction its light travels.
/// @coney-only the render-side lighting frame; the Xbox reads its shadow object's fields where it draws
[[nodiscard]] std::optional<FrameShadowLight> frameShadowLight(const ShadowLight& shadow);

} // namespace coney::graphics
