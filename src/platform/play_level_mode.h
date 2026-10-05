// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "core/interpolation.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "human/player.h"
#include "platform/character_lights.h"
#include "platform/character_mesh.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/world_renderer.h"
#include "sandbox/sandbox_world.h"
#include "world/sector_budget.h"

namespace coney::platform {

/// What a play run has done so far: counts and the player's state, for the summary line.
struct PlayStats {
    std::uint64_t frames = 0;
    float travelled = 0.0F; ///< Metres the player's feet moved across the ground.
};

/// Coney's first playable mode, behind `coney --play-level NAME` (docs/guides/building.md#playing-a-level): Rembrandt
/// standing in a scenery (PlayScenery) at its start, driven by pad 1's left stick, and the follow camera behind him
/// turned by the right stick. The scenery is a level's streamed worlds and level file, loaded as the world viewer
/// loads them and streamed around the follow camera, or a sandbox layout (`--play-level sandbox:NAME`,
/// docs/guides/sandbox.md): the same player and camera code on the sandbox's collision mesh.
///
/// Every step (update()): the player's update (human::Player: the human, then the camera), then the scenery's step
/// (for a level: one streaming decision, the draw distance, and the visibility pass the next step's streaming reads).
/// Every real frame (render()): the drawing, from the player's snapshots of the last two steps blended by the frame's
/// alpha (human::interpolate()) and the draw distance blended the same way: the character skinned and drawn with the
/// scenery, where the original draws its objects (docs/guides/conventions.md#update-and-render). Game time only, so
/// with `--frames` and `--input-script` a run is the same every time, and the simulation is the same at any frame rate.
///
/// No level script, objects or other characters yet. The parts follow docs/research/characters.md and
/// docs/research/camera.md; the mode is Coney's own glue.
class PlayLevelMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x106;
    /// **Coney's choice** for the character's stand-in lights (the LightManager is not researched): an ambient light
    /// and one white directional light (the scenery says from where).
    static constexpr float kCharacterAmbient = 0.45F;
    static constexpr float kCharacterDirectional = 0.7F;

    /// Loads level `name` (LevelPlayScenery::load()) and the player's character from `wad`. `print` receives what was
    /// loaded and streamed (counts only). Everything given must outlive the mode. Fails as the loaders do.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
           std::function<void(std::string_view)> print);

    /// The player in the sandbox `world` (SandboxPlayScenery::create()), at spawn point `spawn` (the layout's first
    /// when unset), with the character loaded from `wad`. Fails as the scenery and the character loader do.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    createInSandbox(RenderEngine& engine, const io::Wad& wad, sandbox::SandboxWorld world,
                    const std::optional<std::string>& spawn, std::function<void(std::string_view)> print);

    ~PlayLevelMode() override;
    PlayLevelMode(const PlayLevelMode&) = delete;
    PlayLevelMode& operator=(const PlayLevelMode&) = delete;
    PlayLevelMode(PlayLevelMode&&) = delete;
    PlayLevelMode& operator=(PlayLevelMode&&) = delete;

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// Preloads the scenery around the camera.
    void enter() override;
    /// One step, as the class comment says. Draws nothing.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// Draws a frame between the last two steps and presents it.
    void render(const RenderTime& time) override;
    /// One line: frames, the player's position, heading, speed, gait, clip and state, then the scenery's counts.
    [[nodiscard]] std::string summary() const;

    [[nodiscard]] const human::Player& player() const { return *m_player; }
    [[nodiscard]] const PlayStats& stats() const { return m_stats; }

  private:
    PlayLevelMode(RenderEngine& engine, std::unique_ptr<PlayScenery> scenery,
                  std::unique_ptr<human::PlayerCharacter> character, std::vector<TextureDictionary> dictionaries,
                  std::function<void(std::string_view)> print);

    // The character and its texture from `wad`, then the mode round `scenery`: what both create functions share.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    createWith(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
               std::function<void(std::string_view)> print);

    // The camera of `snapshot` as the scenery draws it, in RenderWare's axes, with `drawDistance` as its far clip.
    [[nodiscard]] WorldView view(const human::PlayerSnapshot& snapshot, float drawDistance) const;
    // Skins the character in `snapshot`'s pose and places it in the world (RenderWare's axes) for drawing.
    void skin(const human::PlayerSnapshot& snapshot);
    // Draws the character: its lights, the render states, the atomic.
    void drawCharacter() const;

    RenderEngine& m_engine;
    std::unique_ptr<PlayScenery> m_scenery;
    std::unique_ptr<human::PlayerCharacter> m_character;
    std::vector<TextureDictionary> m_dictionaries; // before the mesh, which holds a reference to their texture
    std::unique_ptr<human::Player> m_player;
    std::function<void(std::string_view)> m_print;
    std::vector<anim::Vec3> m_positions;
    std::vector<anim::Vec3> m_normals;
    std::unique_ptr<CharacterMesh> m_mesh;
    std::unique_ptr<CharacterLights> m_lights;
    Interpolated<float> m_drawDistance; // at the last two steps
    PlayStats m_stats;
    std::uint32_t m_lastAnimId = 0;
};

} // namespace coney::platform
