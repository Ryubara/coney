// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "fileio/wad.h"
#include "graphics/human_lighting.h"
#include "graphics/level_lighting.h"
#include "platform/character_mesh.h"
#include "platform/render_engine.h"
#include "platform/scene_lighting.h"
#include "platform/world_renderer.h"
#include "raycast/collision_mesh.h"

namespace rw {
struct Atomic;
struct RGBA;
struct Texture;
} // namespace rw

namespace coney::platform {

/// The play mode's lighting: the level's lights and fog as its scripts set them (graphics::LevelLighting, kept by
/// gameplay), the rig that hands them to librw (SceneLighting) with the `lighting` and `part_page1` sheets for coronas
/// and blob shadows, the flicker stepped with the simulation and the player's dimming in a shadow.
///
/// A scenery played without a level's scripts (a sandbox, a level run on its own) gets **Coney's stand-in**: the
/// manager as it starts plus one ambient and one directional light lighting objects, from the scenery's direction, so
/// the characters stay visible as before.
///
/// Research: docs/research/lighting.md
class PlayLighting {
  public:
    /// The stand-in lights' colours for a scenery without a level's (grey levels, before the brightness is added).
    static constexpr float kStandInAmbient = 0.3F;
    static constexpr float kStandInDirectional = 0.55F;

    /// Lighting from `level` (gameplay's, which must outlive this), or the stand-in from `standInDirection` (game
    /// axes) when null. Loads the two sheets from `wad` when `engine` draws; a sheet that does not load leaves its
    /// sprites out, with a line to `print`.
    PlayLighting(RenderEngine& engine, const io::Wad& wad, graphics::LevelLighting* level, anim::Vec3 standInDirection,
                 const std::function<void(std::string_view)>& print);
    ~PlayLighting();
    PlayLighting(const PlayLighting&) = delete;
    PlayLighting& operator=(const PlayLighting&) = delete;
    PlayLighting(PlayLighting&&) = delete;
    PlayLighting& operator=(PlayLighting&&) = delete;

    /// The simulation's part of a step of `elapsedMs`: the flicker of the lights `view` sees, and whether the player
    /// standing at `playerFeet` (game axes) is hidden in a shadow of `mesh`.
    void step(const WorldView& view, const raycast::CollisionMesh& mesh, anim::Vec3 playerFeet,
              std::uint32_t elapsedMs);

    /// Draws a human's atomic lit as the original lights humans, faded out from 60 to 70 m from the camera
    /// (graphics::humanDistanceFade()); `player` takes the player's shadow dimming. False when it is too far to draw.
    bool drawHuman(rw::Atomic* atomic, bool player);
    /// Draws a human as the original does, mesh by mesh in two passes: the first with his texture (drawHuman()), then
    /// at once the dual layer with `blood` (null: no second pass), blended by the texture's alpha with no alpha test,
    /// Z tested and written, lit and fogged as the first. `player` as drawHuman()'s.
    /// @orig 0x00174320 HumanRender_Draw (HumanRender.cpp)
    void drawHumanPasses(CharacterMesh& mesh, bool player, rw::Texture* blood);
    /// Keeps a blob shadow for a human standing at `feet` (game axes) on `mesh`, for this frame's drawShadows().
    void addShadow(const raycast::CollisionMesh& mesh, anim::Vec3 feet);
    /// Draws and forgets this frame's blob shadows.
    void drawShadows();
    /// Draws a world object's atomic with the objects' lights (SceneLighting::drawObjectAtomic()).
    void drawObject(rw::Atomic* atomic) { m_scene->drawObjectAtomic(atomic); }
    /// Draws the glass panes (SceneLighting::drawGlass()).
    void drawGlass(std::span<const world_objects::GlassQuad> panes) const { m_scene->drawGlass(panes); }
    /// Draws the health rings and L1 markers, after the shadows (SceneLighting::drawGroundRings()).
    void drawRings(std::span<const hud::GroundRing> rings, std::span<const hud::TargetMarker> markers) const {
        m_scene->drawGroundRings(rings, markers);
    }

    /// The rig, for the scenery's renderer.
    [[nodiscard]] SceneLighting& scene() { return *m_scene; }
    /// One line for the log: the lights, the world ambient, the fog.
    [[nodiscard]] std::string summary() const;

  private:
    std::unique_ptr<graphics::LevelLighting> m_own; // the stand-in's, when there is no level's
    graphics::LevelLighting* m_level = nullptr;     // what is drawn with: the level's or m_own
    std::unique_ptr<SceneLighting> m_scene;
    graphics::ShadowDim m_playerDim;
    std::vector<graphics::BlobShadow> m_shadows;
    rw::Atomic* m_playerAtomic = nullptr;  // the atomic m_playerColours belong to
    std::vector<rw::RGBA> m_playerColours; // its materials' own colours, before the dimming
};

} // namespace coney::platform
