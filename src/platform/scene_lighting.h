// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "graphics/human_lighting.h"
#include "graphics/level_lighting.h"
#include "graphics/light_manager.h"
#include "graphics/particle_page.h"
#include "hud/health_rings.h"
#include "world/view_frustum.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Light;
struct World;
} // namespace rw

namespace coney::platform {

/// Draws atomics lit the way the original's LightManager lights them: per atomic, the lights
/// graphics::LightManager::select() chooses, handed to librw (whose GL3 lighting adds ambient, directional and point
/// lights to the prelighting and clamps, as the PS2 pipeline does); then the frame's coronas from the `lighting`
/// sheet and the humans' blob shadows from `part_page1`, as camera-facing and ground-lying quads.
///
/// librw lights an atomic from the current world's lights, and its point lights only when the atomic is in that
/// world. So the rig owns a librw world holding just the selected lights of the atomic being drawn, and puts the
/// atomic in it for the draw. One librw light mirrors each manager record that is drawn.
///
/// Needs a running RenderEngine and must be destroyed before it stops. Not copyable or movable: librw keeps pointers.
///
/// Research: docs/research/lighting.md
class SceneLighting {
  public:
    /// Lights from `lighting`, which must outlive the rig. `coronas` and `shadows` are the `lighting` and `part_page1`
    /// sheets (converted for drawing); without them the coronas or shadows are not drawn.
    SceneLighting(graphics::LevelLighting& lighting, std::optional<graphics::SpriteSheet> coronas,
                  std::optional<graphics::SpriteSheet> shadows);
    ~SceneLighting();
    SceneLighting(const SceneLighting&) = delete;
    SceneLighting& operator=(const SceneLighting&) = delete;
    SceneLighting(SceneLighting&&) = delete;
    SceneLighting& operator=(SceneLighting&&) = delete;

    /// The start of a frame through a camera at `pose` (RenderWare's axes) with clip distances `nearClip` and
    /// `farClip`, at game time `nowMs`: the manager's viewport pass (the ambients, the cull, the coronas).
    void beginFrame(const world::CameraPose& pose, float nearClip, float farClip, std::uint64_t nowMs);

    /// Draws a streamed sector's atomic with the world's lights (list A and the world's point lights it overlaps).
    /// @orig 0x00411990 World_RenderSectorAtomic (WorldPS2.cpp)
    void drawWorldAtomic(rw::Atomic* atomic);
    /// Draws the background's atomics with the world's ambient and directional lights only.
    void drawBackgroundAtomic(rw::Atomic* atomic);
    /// Draws a human's atomic: the objects' lights for his squared distance to the camera, and the point lights his
    /// sphere overlaps unless `hidden` (in a shadow). Colour dimming is the caller's (the mesh's colour).
    /// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
    void drawHumanAtomic(rw::Atomic* atomic, bool hidden);

    /// Draws this frame's coronas: Z test on, no Z write, no fog, blended by alpha.
    /// @orig 0x0017caa8 Light_UpdateFlickerAndCorona (LightManager.cpp)
    void drawCoronas() const;
    /// Draws one blob shadow (game axes) per entry of `shadows`.
    void drawBlobShadows(std::span<const graphics::BlobShadow> shadows) const;
    /// Draws a world object's atomic with the objects' lights for its squared distance to the camera and the point
    /// lights its sphere overlaps, as a human's (docs/research/objects.md#models: the lights are chosen per object).
    /// @orig 0x0017fd78 ObjectRender_Draw (unknown)
    void drawObjectAtomic(rw::Atomic* atomic);
    /// Draws the health rings and the L1 markers (game axes) from `part_page1`: triangle fans in their per-vertex
    /// colours, Z tested, fog off. **Coney's stand-in** for the state the world pass leaves (not traced): alpha
    /// blended, no Z write.
    /// @orig 0x0017b2e0 GroundRing_DrawQueued (unknown)
    void drawGroundRings(std::span<const hud::GroundRing> rings, std::span<const hud::TargetMarker> markers) const;

    /// This frame's camera (RenderWare's axes), as beginFrame() was given it.
    [[nodiscard]] const world::CameraPose& pose() const { return m_pose; }
    /// The light manager, for the summary.
    [[nodiscard]] const graphics::LightManager& lights() const { return m_lighting.lights; }
    /// The level's fog, which the world pass clears to and fogs with.
    [[nodiscard]] const graphics::WorldFog& fog() const { return m_lighting.fog; }

  private:
    // Puts the lights of `selection` into the rig's world, mirrored from the manager's records.
    void useLights(const graphics::LightSelection& selection);
    // Draws `atomic` inside the rig's world, so librw lights it with the rig's lights.
    void renderInRig(rw::Atomic* atomic);
    // The librw light that mirrors record `index`, made on first use with the record's type.
    rw::Light* mirror(std::uint16_t index);

    graphics::LevelLighting& m_lighting;
    std::optional<graphics::SpriteSheet> m_coronaSheet;
    std::optional<graphics::SpriteSheet> m_shadowSheet;
    rw::World* m_world = nullptr;      // owned: holds the current atomic's lights
    std::vector<rw::Light*> m_mirrors; // owned, with their frames; null where none is made yet
    std::vector<rw::Light*> m_inWorld; // the mirrors now in m_world
    world::CameraPose m_pose;          // this frame's camera
};

} // namespace coney::platform
