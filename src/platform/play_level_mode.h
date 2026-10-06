// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "animation/anim_math.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/interpolation.h"
#include "core/options.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/gameplay_mode.h"
#include "graphics/render_device.h"
#include "human/player.h"
#include "human/target_human.h"
#include "platform/character_lights.h"
#include "platform/character_mesh.h"
#include "platform/play_scenery.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/world_renderer.h"
#include "sandbox/sandbox_world.h"
#include "world/debug_camera.h"
#include "world/sector_budget.h"

namespace rw {
struct Texture;
}

namespace coney::platform {

/// Who the play mode's player is and how he is first placed, beyond where (the scenery's start).
struct PlayerSetup {
    /// The Character List model the player is drawn and animated as (the level script's type, through `CfgChar`,
    /// characters::modelNameFor()). **Coney's choice:** when it is empty or fails to load, Rembrandt's, with a line in
    /// the log.
    std::string model{human::kPlayerModel};
    /// The character type the player was made as (`HuCreate`'s type): his class (`types`) and the Player page.
    int type = human::kPlayerType;
    /// The character types the scripts configured (`CfgChar`, with the power and Warrior classes): the player takes
    /// his class from them (human::playerClassOf()), and the debug menus can rebuild him as any of them
    /// (debug::PlayControls::changeCharacter()); empty: none, and the player plays his files' own damage.
    characters::CharacterTypes types;
    /// Whether the start is snapped to the ground as `HuCreate` does; false for a start a `TeleportToFlag` gave, which
    /// does not snap (docs/research/flags.md#position).
    bool snapToGround = true;
    /// What the AI fighters are (ai::aiConfigFrom() of the scripts' configuration calls); the research's reference
    /// values by default.
    ai::AiConfig ai;
};

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
/// No level script or objects yet. Other characters are a sandbox layout's `target` lines, Coney's passive targets
/// (human::TargetHuman) stepped after the player, and AI fighters (ai::AiHumans: a layout's `fighter` lines, or spawned
/// from the debug menus), humans with a brain stepped in the player's characters' step; both are drawn with the
/// player's model. The parts follow docs/research/characters.md, docs/research/combat.md, docs/research/ai.md and
/// docs/research/camera.md; the mode is Coney's own glue.
///
/// It is also the debug menus' way into the game (debug::PlayControls, docs/guides/debug-menu.md): the Player, Camera
/// and Spawner pages act on it between steps, and render() draws the Debug draw page's lines into the scene.
class PlayLevelMode final : public GameMode, public debug::PlayControls, public ScriptedPlayer {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x106;
    /// **Coney's choice** for the character's stand-in lights (the LightManager is not researched): an ambient light
    /// and one white directional light (the scenery says from where).
    static constexpr float kCharacterAmbient = 0.45F;
    static constexpr float kCharacterDirectional = 0.7F;

    /// Loads level `name` (LevelPlayScenery::load()) and the player's character from `wad`, the player at `start`
    /// (player 1 as the level script created him) when given, as `setup` says. `print` receives what was loaded and
    /// streamed (counts only). Everything given must outlive the mode. Fails as the loaders do.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
           std::function<void(std::string_view)> print, std::optional<human::PlayerStart> start = std::nullopt,
           const PlayerSetup& setup = {});

    /// The player in the sandbox `world` (SandboxPlayScenery::create()), at spawn point `spawn` (the layout's first
    /// when unset), with the character loaded from `wad`, as `setup` says (its start is always snapped). Fails as the
    /// scenery and the character loader do.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    createInSandbox(RenderEngine& engine, const io::Wad& wad, sandbox::SandboxWorld world,
                    const std::optional<std::string>& spawn, std::function<void(std::string_view)> print,
                    const PlayerSetup& setup = {});

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
    /// The sandbox's targets (none in a level).
    [[nodiscard]] std::span<human::TargetHuman* const> targets() const { return m_targetPointers; }
    /// The AI fighters.
    [[nodiscard]] const ai::AiHumans& fighters() const { return *m_ai; }
    [[nodiscard]] const PlayStats& stats() const { return m_stats; }

    /// Writes the trace (human::traceHeader(), then human::traceLine() after every step) to the file at `path`,
    /// replacing it: `--trace`. Fails with ErrorCode::Io when the file cannot be opened.
    [[nodiscard]] std::expected<void, Error> traceTo(const std::string& path);

    /// Starts the player at `place` instead (dropped onto the ground below it, as a start is), and the camera where it
    /// says, if it says: `--start`, Coney's own test aid.
    void startAt(const StartPlace& place);

    /// Sets the debug lines render() draws (the debug session's, which must outlive the mode); null draws none.
    void setDebugDraw(const debug::DebugDrawOptions* options) { m_debugDraw = options; }

    // debug::PlayControls, for the debug menus.
    [[nodiscard]] std::string sceneName() const override { return m_scenery->name(); }
    [[nodiscard]] anim::Vec3 playerFeet() const override;
    [[nodiscard]] float playerHeadingDegrees() const override;
    [[nodiscard]] float playerSpeed() const override;
    [[nodiscard]] std::string playerState() const override;
    void teleport(const debug::Place& place) override;
    [[nodiscard]] std::vector<debug::Place> places() const override { return m_scenery->places(); }
    [[nodiscard]] bool playerFrozen() const override { return m_frozen; }
    void setPlayerFrozen(bool frozen) override { m_frozen = frozen; }
    [[nodiscard]] anim::Vec3 cameraEye() const override;
    [[nodiscard]] anim::Vec3 cameraTarget() const override;
    void resetCamera() override { m_player->resetCamera(); }
    [[nodiscard]] bool freeCamera() const override { return m_freeCamera.has_value(); }
    void setFreeCamera(bool on) override;
    [[nodiscard]] bool canSpawn() const override { return m_scenery->canSpawn(); }
    std::expected<void, Error> spawn(const sandbox::Primitive& primitive) override;
    [[nodiscard]] std::size_t spawnedCount() const override { return m_spawned.size(); }
    std::expected<void, Error> clearSpawned() override;
    [[nodiscard]] bool canSpawnFighter() const override { return true; }
    std::expected<void, Error> spawnFighter(anim::Vec3 feet, float headingDegrees) override;
    [[nodiscard]] std::size_t fighterCount() const override { return m_ai->count(); }
    void clearFighters() override;
    [[nodiscard]] bool fightersEngage() const override { return m_ai->engaging(); }
    void setFightersEngage(bool on) override { m_ai->setEngaging(on); }
    [[nodiscard]] std::string fightersState() const override;
    [[nodiscard]] std::vector<debug::CharacterChoice> characterChoices() const override;
    [[nodiscard]] int playerType() const override { return m_type; }
    [[nodiscard]] std::string characterState() const override;
    /// Rebuilds the player as `type` (debug::PlayControls::changeCharacter()): the model a player of the type is drawn
    /// as and its files (the anim set, the moves' Anim Range List, the speeds its clips give), loaded as the mode's
    /// start loads them, and his class as the start's player takes it (human::playerClassOf(): the class's damage
    /// scaled by his Warrior class, his power class), at full health where he stands. The AI
    /// fighters are made again where they stand, at full health, since their brains hold the player they fought.
    std::expected<void, Error> changeCharacter(int type) override;

    /// ScriptedPlayer: a script's `TeleportToFlag` on player 1 during play; no ground snap.
    void teleportPlayer(const world_objects::Placement& placement) override;
    /// The model the player is drawn as.
    [[nodiscard]] const std::string& model() const { return m_model; }

  private:
    // A character's resources and its texture dictionaries, ready to draw.
    struct LoadedCharacter {
        std::unique_ptr<human::PlayerCharacter> character;
        std::vector<TextureDictionary> dictionaries;
    };

    PlayLevelMode(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
                  LoadedCharacter loaded, std::function<void(std::string_view)> print, std::string model,
                  const PlayerSetup& setup);

    // The character and its texture from `wad`, then the mode round `scenery`: what both create functions share.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayLevelMode>, Error>
    createWith(RenderEngine& engine, const io::Wad& wad, std::unique_ptr<PlayScenery> scenery,
               std::function<void(std::string_view)> print, const PlayerSetup& setup = {});
    // Loads the character `model` names from `wad` and its textures, converted for drawing when `engine` draws: the
    // player's creation, at the start and at a change of character (src/platform/play_level_character.cpp).
    [[nodiscard]] static std::expected<LoadedCharacter, Error> loadCharacter(RenderEngine& engine, const io::Wad& wad,
                                                                             std::string_view model);
    // The texture a character's dictionaries hold, which every material uses; null when none.
    [[nodiscard]] static rw::Texture* textureOf(const std::vector<TextureDictionary>& dictionaries);
    // The character the player plays: his own after a change of character, else the scene's.
    [[nodiscard]] const human::PlayerCharacter& playerCharacter() const {
        return m_playerCharacter ? *m_playerCharacter : *m_character;
    }

    // The camera of `snapshot` as the scenery draws it, in RenderWare's axes, with `drawDistance` as its far clip.
    [[nodiscard]] WorldView view(const human::PlayerSnapshot& snapshot, float drawDistance) const;
    // Skins `character` in `pose`, leaned by `lean` and turned to `heading` at `feet`, into `positions` and `normals`
    // in the world (RenderWare's axes) for drawing.
    static void skin(const human::PlayerCharacter& character, const anim::Pose& pose, anim::Vec3 feet, float heading,
                     float lean, std::vector<anim::Vec3>& positions, std::vector<anim::Vec3>& normals);
    // Makes the layout's targets, dropped onto the ground, with a mesh each.
    void makeTargets(rw::Texture* texture);
    // Spawns a fighter dropped onto the ground below `spot`, with its mesh.
    void addFighter(anim::Vec3 spot, float headingDegrees);
    // Draws the character: its lights, the render states, the atomic.
    void drawCharacter() const;
    // The view from a camera pose (RenderWare's axes) through the player camera's lens, with `drawDistance`.
    [[nodiscard]] WorldView viewFrom(const world::CameraPose& pose, float drawDistance) const;
    // The free camera `camera` between its last two steps, `alpha` of the way.
    [[nodiscard]] static world::DebugCamera blendedFreeCamera(const Interpolated<world::DebugCamera>& camera,
                                                              float alpha);
    // Draws the Debug draw page's lines through the current camera, from `snapshot`.
    void drawDebugLines(const human::PlayerSnapshot& snapshot) const;

    RenderEngine& m_engine;
    const io::Wad& m_wad;
    std::unique_ptr<PlayScenery> m_scenery;
    std::unique_ptr<human::PlayerCharacter> m_character; // the scene's: the targets and fighters play it
    std::vector<TextureDictionary> m_dictionaries;       // before the mesh, which holds a reference to their texture
    // After a change of character, the player's own character and textures; and the characters he played before,
    // kept while the mode lasts because a target may still be playing a paired clip from one of their anim sets.
    std::unique_ptr<human::PlayerCharacter> m_playerCharacter;
    std::vector<TextureDictionary> m_playerDictionaries;
    std::vector<std::unique_ptr<human::PlayerCharacter>> m_retired;
    characters::CharacterTypes m_types; // the configuration's types
    int m_type = 0;                     // the type the player is
    int m_levelNumber = 0;              // the scene's level number (its models), 0 for a sandbox
    std::unique_ptr<human::Player> m_player;
    std::function<void(std::string_view)> m_print;
    std::vector<anim::Vec3> m_positions;
    std::vector<anim::Vec3> m_normals;
    std::unique_ptr<CharacterMesh> m_mesh;
    std::unique_ptr<CharacterLights> m_lights;
    Interpolated<float> m_drawDistance; // at the last two steps
    PlayStats m_stats;
    std::uint32_t m_lastAnimId = 0;
    const debug::DebugDrawOptions* m_debugDraw = nullptr;
    bool m_frozen = false;
    std::optional<Interpolated<world::DebugCamera>> m_freeCamera; // at the last two steps, while it is on
    std::vector<sandbox::Primitive> m_spawned;
    // The sandbox's targets, each drawn with its own copy of the character's mesh.
    struct Target {
        std::unique_ptr<human::TargetHuman> human;
        std::unique_ptr<CharacterMesh> mesh;
        std::vector<anim::Vec3> positions;
        std::vector<anim::Vec3> normals;
    };
    std::vector<Target> m_targets;
    std::vector<human::TargetHuman*> m_targetPointers;
    std::vector<human::Combatant*> m_combatants; // the targets, as the player's step takes them
    // The AI fighters, and a mesh each (in the fighters' order).
    std::unique_ptr<ai::AiHumans> m_ai;
    struct FighterMesh {
        std::unique_ptr<CharacterMesh> mesh;
        std::vector<anim::Vec3> positions;
        std::vector<anim::Vec3> normals;
    };
    std::vector<FighterMesh> m_fighterMeshes;
    rw::Texture* m_texture = nullptr; // the character's texture, for new meshes
    std::string m_model;              // the Character List model the player is
    // The --trace file (closed when unset) and the steps traced.
    std::optional<std::ofstream> m_trace;
    std::uint64_t m_traceSteps = 0;
};

} // namespace coney::platform
