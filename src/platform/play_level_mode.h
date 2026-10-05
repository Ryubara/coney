// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "human/player.h"
#include "platform/character_lights.h"
#include "platform/character_mesh.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "platform/world_viewer_mode.h"
#include "world/level_object.h"
#include "world/sector_budget.h"

namespace coney::platform {

/// What a play run has done so far: counts and the player's state, for the summary line.
struct PlayStats {
    std::uint64_t frames = 0;
    std::uint32_t loads = 0;    ///< Parts read while running.
    std::uint32_t unloads = 0;  ///< Parts freed.
    std::uint32_t failures = 0; ///< Parts that could not be read.
    float travelled = 0.0F;     ///< Metres the player's feet moved across the ground.
};

/// Coney's first playable mode, behind `coney --play-level NAME` (docs/guides/building.md#playing-a-level): a level's
/// streamed worlds and level file loaded as the world viewer loads them, Rembrandt standing at the level's player
/// start, driven by pad 1's left stick, and the follow camera behind him turned by the right stick. The scenery streams
/// around the follow camera.
///
/// Every frame, first the simulation: the player's update (human::Player: the human, then the camera), one streaming
/// decision, the draw distance. Then the drawing, from the player's snapshots only: the visibility pass, the character
/// skinned and drawn with the world, between its two worlds where the original draws its objects. Game time only, so
/// with `--frames` and `--input-script` a run is the same every time.
///
/// No level script, objects or other characters yet. The parts follow docs/research/characters.md and
/// docs/research/camera.md; the mode is Coney's own glue.
class PlayLevelMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x106;
    /// The fog and background colour before a level script sets one, as in the world viewer.
    static constexpr graphics::Rgba kFogColour = WorldViewerMode::kFogColour;
    /// **Coney's choice** for the character's stand-in lights (the LightManager is not researched): an ambient light
    /// and one white directional light from above and behind the camera's usual side.
    static constexpr float kCharacterAmbient = 0.45F;
    static constexpr float kCharacterDirectional = 0.7F;

    /// Loads level `name` (its worlds and level file, loadLevelScenery()) and the player's character from `wad`, and
    /// places the player at the level's start: the researched one (human::researchedPlayerStart()), otherwise Coney's
    /// stand-in (playLevelStandInStart()). `print` receives what was loaded and streamed (counts only). Everything
    /// given must outlive the mode. Fails as the loaders do, and with ErrorCode::NotFound for a level without a level
    /// file (no collision to stand on).
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
           std::function<void(std::string_view)> print);

    ~PlayLevelMode() override;
    PlayLevelMode(const PlayLevelMode&) = delete;
    PlayLevelMode& operator=(const PlayLevelMode&) = delete;
    PlayLevelMode(PlayLevelMode&&) = delete;
    PlayLevelMode& operator=(PlayLevelMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// Preloads the scenery around the camera.
    void enter() override;
    /// One frame, as the class comment says.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// One line: frames, the player's position, heading, speed, gait, clip and state, parts streamed.
    [[nodiscard]] std::string summary() const;

    [[nodiscard]] const human::Player& player() const { return *m_player; }
    [[nodiscard]] const PlayStats& stats() const { return m_stats; }

  private:
    PlayLevelMode(RenderEngine& engine, LevelScenery scenery, std::unique_ptr<human::PlayerCharacter> character,
                  std::vector<TextureDictionary> dictionaries, world::SectorBudget& budget,
                  const human::PlayerStart& start, std::function<void(std::string_view)> print);

    // The simulation half of a frame: the player's update, one streaming decision, the draw distance. Reads the pads
    // and game time only; draws nothing.
    void simulate(GameModeStack& stack, const FrameTime& frame);
    // The drawing half: the visibility pass, the character skinned, the frame drawn, all from the player's snapshots
    // `alpha` (0 to 1) of the way from the last step to this one. Changes no simulation state. The split is ready for
    // a frame loop that steps the simulation and draws at its own rate.
    void draw(float alpha);
    // The camera of `snapshot` as the world renderer draws it, in RenderWare's axes.
    [[nodiscard]] WorldView view(const human::PlayerSnapshot& snapshot) const;
    // Skins the character in `snapshot`'s pose and places it in the world (RenderWare's axes) for drawing.
    void skin(const human::PlayerSnapshot& snapshot);
    // Draws the character: its lights, the render states, the atomic.
    void drawCharacter() const;

    RenderEngine& m_engine;
    LevelScenery m_scenery;
    std::unique_ptr<human::PlayerCharacter> m_character;
    std::vector<TextureDictionary> m_dictionaries; // before the mesh, which holds a reference to their texture
    world::SectorBudget& m_budget;
    std::unique_ptr<human::Player> m_player;
    WorldRenderer m_renderer;
    std::function<void(std::string_view)> m_print;
    std::vector<anim::Vec3> m_positions;
    std::vector<anim::Vec3> m_normals;
    std::unique_ptr<CharacterMesh> m_mesh;
    std::unique_ptr<CharacterLights> m_lights;
    float m_drawDistance;
    float m_pending = 0.0F;    // the nearest missing scenery after the last step
    std::uint64_t m_nowMs = 0; // game time after the last step
    PlayStats m_stats;
    std::uint32_t m_lastAnimId = 0;
};

/// **Coney's choice** for a level whose player start is not researched: above the middle of the first world's part 1
/// (the world viewer's start), dropped onto the collision mesh from 200 m above its lowest point. Game axes.
[[nodiscard]] human::PlayerStart playLevelStandInStart(const WorldSet& set, const raycast::CollisionMesh& mesh);

/// A point in the game's axes (z up) in RenderWare's (y up), as the world uses them: (x, z, -y).
[[nodiscard]] world::Vec3 toRenderWare(anim::Vec3 game);

} // namespace coney::platform
