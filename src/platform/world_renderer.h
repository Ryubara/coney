// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "graphics/render_device.h"
#include "platform/render_engine.h"
#include "platform/world_set.h"
#include "world/view_frustum.h"

namespace rw {
struct Atomic;
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
    float drawDistance = 0.0F; ///< Far clip this frame; the fog starts at half of it.
};

/// The device's fog start: the fog begins at this fraction of the far clip (0.5 from start-up,
/// docs/research/graphics.md#device-object).
inline constexpr float kFogStart = 0.5F;

/// Draws the streamed worlds of a WorldSet with librw, in the order of the original's world pass, as far as Coney has
/// its parts: the `s` world's collected sectors, then the `d` world's, each with Z test and write, back-face culling
/// and fog, each atomic faded in over its first second. Not drawn yet: the level world, objects, water and effects.
///
/// The original lights each atomic from its LightManager, which Coney does not have yet. In its place the atomics are
/// lit by one ambient light of the brightness given (a Coney choice; 0 shows the prelighting alone).
///
/// Research: docs/research/world.md#a-frame
class WorldRenderer {
  public:
    /// A renderer whose stand-in ambient light has `ambient` (0 to 1) in each channel. Needs a running RenderEngine;
    /// must be destroyed before it stops.
    explicit WorldRenderer(float ambient);
    ~WorldRenderer();
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;
    WorldRenderer(WorldRenderer&&) = delete;
    WorldRenderer& operator=(WorldRenderer&&) = delete;

    /// One frame of the worlds of `set` through `view`, cleared to `fogColour` (which is also the fog's colour, as the
    /// device's background colour is), drawn into the whole window and presented. Draws the sectors each world
    /// collects (StreamedWorld::collectSectors), so the visibility pass must have run. `nowMs` is game time for the
    /// fade-in. With the NULL backend the frame is begun and presented and nothing is drawn.
    /// @orig 0x0040e8d8 WorldManager_Render (WorldManagerPS2.cpp)
    void render(RenderEngine& engine, const WorldSet& set, const WorldView& view, graphics::Rgba fogColour,
                std::uint64_t nowMs);

    /// Atomics drawn by the last render().
    [[nodiscard]] std::uint32_t drawnAtomics() const { return m_drawn; }

  private:
    /// Fades one sector's atomic in by its material colours' alpha, then draws it.
    /// @orig 0x00411990 World_RenderSectorAtomic (WorldPS2.cpp)
    static void renderSectorAtomic(rw::Atomic* atomic, std::uint64_t fadeEndMs, std::uint64_t nowMs);

    rw::World* m_lights = nullptr;  // owned: holds the light; librw lights atomics from the current world
    rw::Light* m_ambient = nullptr; // owned
    std::uint32_t m_drawn = 0;
};

} // namespace coney::platform
