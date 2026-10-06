// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include "graphics/level_lighting.h"
#include "graphics/render_device.h"
#include "platform/render_engine.h"
#include "platform/scene_lighting.h"
#include "platform/world_set.h"
#include "world/level_object.h"
#include "world/view_frustum.h"

namespace rw {
struct Atomic;
struct Camera;
struct Light;
struct World;
} // namespace rw

namespace coney::platform {

/// The camera a world frame is drawn through: where it is, its view window and clip distances.
struct WorldView {
    world::CameraPose pose;
    float halfWidth = 0.5F;  ///< Half the view window's width at distance 1.
    float halfHeight = 0.5F; ///< Half its height.
    float nearClip = 0.5F;
    float drawDistance = 0.0F; ///< Far clip this frame.
    float fogStart = 0.5F;     ///< The fraction of the far clip where the fog begins (`SetFogDistance`).
};

/// Places librw's camera at `view` (RenderWare's axes): its frame, view window, the near clip and the draw distance as
/// the far clip, and the fog plane at the view's fog start of it. Shared by the world renderer and the sandbox's.
void placeWorldCamera(rw::Camera* camera, const WorldView& view);

/// Draws a level with librw in the order of the original's viewport pass, as far as Coney has its parts: the level's
/// background (LevelObject_RenderBackground: sky box, turning cloud box, skyline, then a Z-only clear), then the world
/// pass (WorldManager_Render): the level world's light glows, the `s` world's collected sectors and the `d` world's,
/// each with Z test and write, back-face culling and fog, each sector's atomic faded in over its first second. Not
/// drawn yet: objects, water and effects.

/// The sky's clip planes, round a camera whose translation is zeroed (docs/research/level-loading.md#render-order).
inline constexpr float kSkyNearClip = 0.05F;
inline constexpr float kSkyFarClip = 5.0F;
/// The skyline's far clip, and the most its near clip can be: it comes closer when scenery nearer is still missing.
inline constexpr float kSkylineFarClip = 560.0F;
inline constexpr float kSkylineNearClip = 39.0F;
/// How fast the cloud box turns about the up axis: one radian a minute of game time.
inline constexpr float kCloudRadiansPerMs = 1.0F / 60000.0F;

/// The cloud box's frame after `nowMs` of game time: `base` turned about RenderWare's up axis (y) through the
/// origin by nowMs × kCloudRadiansPerMs.
[[nodiscard]] world::FrameMatrix cloudFrame(const world::FrameMatrix& base, std::uint64_t nowMs);
///
/// Each atomic is lit as the original's LightManager lights it (SceneLighting): the sectors by the world's lights and
/// the world point lights they overlap, the background by the world's ambient and directional lights; the coronas
/// follow the world. The lighting and the fog are a level's (setLighting()), or without one the manager as it starts:
/// the world ambient at the brightness alone (0.157) and white fog, what a level looks like before its scripts run.
///
/// Research: docs/research/world.md#a-frame
class WorldRenderer {
  public:
    /// A renderer with the light manager as it starts. Needs a running RenderEngine; must be destroyed before it
    /// stops.
    WorldRenderer();
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    WorldRenderer(WorldRenderer&&) = delete;
    WorldRenderer& operator=(WorldRenderer&&) = delete;

    /// One frame of the worlds of `set` through `view`, cleared to the fog's colour (the device's background colour),
    /// drawn into the whole window and presented. With `level`, its background comes first and its light glows open the
    /// world pass; `pendingDistance` (the nearest missing scenery, world::nearestPendingDistance()) brings the
    /// skyline's near clip closer. Draws the loaded sectors that `view`'s own frustum may contain
    /// (StreamedWorld::collectSectorsIn), so a view blended between two steps draws what it sees without redoing the
    /// simulation's visibility pass. `nowMs` is game time for the fade-in and the clouds. `drawObjects`, when given,
    /// draws the objects between the `s` and the `d` world (step 7, where the original draws the resource manager's
    /// queued objects); it may change the current lights and render states, which are put back after it. `overlay`,
    /// when given, draws after the world with no depth test, before the frame is presented (the menus' 2D pass). With
    /// the NULL backend the frame is begun and presented and nothing is drawn.
    /// @orig 0x0040e8d8 WorldManager_Render (WorldManagerPS2.cpp)
    void render(RenderEngine& engine, const WorldSet& set, const world::LevelObject* level, const WorldView& view,
                float pendingDistance, std::uint64_t nowMs, const std::function<void()>& drawObjects = {},
                const std::function<void()>& overlay = {});

    /// Lights the frames with `lighting` (a level's, which must outlive its use) from now on; null goes back to the
    /// renderer's own.
    void setLighting(SceneLighting* lighting) { m_lighting = lighting; }
    /// The lighting the frames are drawn with.
    [[nodiscard]] SceneLighting& lighting() const { return m_lighting != nullptr ? *m_lighting : *m_ownLighting; }

    /// Atomics drawn by the last render().
    [[nodiscard]] std::uint32_t drawnAtomics() const { return m_drawn; }

  private:
    /// The level's background, before the world: the sky box and the turning cloud box round the camera, then the
    /// skyline in place from min(39, pendingDistance) to 560, then a Z-only clear so the world covers it. Leaves the
    /// camera as `view` places it.
    /// @orig 0x0040d0a8 LevelObject_RenderBackground (unknown)
    void renderBackground(rw::Camera* camera, const world::LevelObject& level, const WorldView& view,
                          graphics::Rgba fogColour, float pendingDistance, std::uint64_t nowMs) const;

    /// Fades one sector's atomic in by its material colours' alpha, then draws it with the world's lights.
    /// @orig 0x00411990 World_RenderSectorAtomic (WorldPS2.cpp)
    void renderSectorAtomic(rw::Atomic* atomic, std::uint64_t fadeEndMs, std::uint64_t nowMs) const;

    std::unique_ptr<graphics::LevelLighting> m_ownLevel; // the manager as it starts, for frames without a level's
    std::unique_ptr<SceneLighting> m_ownLighting;
    SceneLighting* m_lighting = nullptr; // a level's, when set
    std::uint32_t m_drawn = 0;
};

} // namespace coney::platform
