// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "animation/skeleton.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "characters/anim_set.h"
#include "characters/character_assets.h"
#include "characters/character_data.h"
#include "characters/character_types.h"
#include "combat/anim_ranges.h"
#include "combat/commands.h"
#include "combat/power_class.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "core/pad.h"
#include "fileio/wad.h"
#include "human/combatant.h"
#include "human/human.h"
#include "human/humans.h"
#include "raycast/collision_mesh.h"

// Player 1 in a level: the character it plays, the human the pad drives and the follow camera behind it, stepped
// together in the characters' order (the pad into the human's record, the characters' step, then cameras).
// Platform-neutral: the play mode draws it, the disc tests drive it headless.
// Research: docs/research/characters.md#update, docs/research/tasks.md#humans-update, docs/research/camera.md

namespace coney::human {

/// The player's model: Rembrandt, as `level99.lua` creates him (docs/research/characters.md#creation).
inline constexpr std::string_view kPlayerModel = "warr_re_cv";
/// The character type of kPlayerModel: Rembrandt as `level99.lua` creates him (`HuCreate` type 32).
inline constexpr int kPlayerType = 32;

/// Rembrandt's body scale (`+0x65c`) as read at runtime: his walking sphere is 0.35 × 0.97 = 0.34 m
/// (docs/research/characters.md#walls). How the game derives it is open, so Coney uses the value read.
inline constexpr float kPlayerBodyScale = 0.97F;

/// Where a level's script puts player 1: the feet (game axes) and the heading in degrees.
struct PlayerStart {
    anim::Vec3 position;
    float headingDegrees = 0.0F;
};

/// The start the research gives for `level` (`HuCreate` at level99 checkpoint 1: (-284.4, 120.4, 0.3), heading 0),
/// or nothing for a level whose start is not researched yet.
[[nodiscard]] std::optional<PlayerStart> researchedPlayerStart(std::string_view level);

/// A character's resources as the player plays them: its model, its own character data, the generic character data
/// that answers its default slots (characters::kGenericAnimDataHash), the anim set over both and its skeleton. Not
/// copyable or movable: the anim set points into the two character datas.
class PlayerCharacter {
  public:
    /// Loads `name` through the Character List of `wad` with `table` (which needs the character data handlers), and the
    /// generic character data. Fails as characters::loadCharacterAssets() and loadCharacterData() do, and with
    /// ErrorCode::NotFound when a clip the locomotion plays is missing.
    [[nodiscard]] static std::expected<std::unique_ptr<PlayerCharacter>, Error>
    load(const io::Wad& wad, const chunk::ChunkHandlerTable& table, std::string_view name);

    PlayerCharacter(const PlayerCharacter&) = delete;
    PlayerCharacter& operator=(const PlayerCharacter&) = delete;
    PlayerCharacter(PlayerCharacter&&) = delete;
    PlayerCharacter& operator=(PlayerCharacter&&) = delete;
    ~PlayerCharacter() = default;

    [[nodiscard]] const characters::CharacterAssets& assets() const { return m_assets; }
    [[nodiscard]] const characters::AnimSet& anims() const { return m_anims; }
    [[nodiscard]] const anim::Skeleton& skeleton() const { return m_skeleton; }
    /// The character's Anim Range List, decoded: its moves' damage and reach (combat::AnimRangeList).
    [[nodiscard]] const combat::AnimRangeList& ranges() const { return m_ranges; }

  private:
    PlayerCharacter(characters::CharacterAssets assets, characters::CharacterData generic,
                    combat::AnimRangeList ranges);

    characters::CharacterAssets m_assets;
    characters::CharacterData m_generic;
    characters::AnimSet m_anims;
    anim::Skeleton m_skeleton;
    combat::AnimRangeList m_ranges;
};

/// What drawing the player needs from one simulation step, and nothing else: drawing reads only these, so a renderer
/// can draw between two steps by interpolating the previous and the current one.
struct PlayerSnapshot {
    anim::Vec3 feet;           ///< The human's position (game axes).
    float heading = 0.0F;      ///< Radians, 0 facing +y.
    float lean = 0.0F;         ///< The body's lean into a turn, radians, positive to the left (Human::lean()).
    anim::Pose pose;           ///< The blended animation pose.
    anim::Vec3 cameraEye;      ///< The current camera's position.
    anim::Vec3 cameraTarget;   ///< A point ahead on its view direction (the follow camera's look-at point).
    float fieldOfView = 65.0F; ///< The current camera's lens: horizontal degrees, near and far clip.
    float nearClip = 0.1F;
    float farClip = 115.0F;
};

/// The snapshot `alpha` (0 to 1) of the way from `previous` to `current`: positions lerped, the heading along the
/// shorter way round, the pose blended as two animation poses are (each bone's rotation slerped). Returns `current`
/// itself at alpha 1 or more and `previous` at 0 or less, so a render at alpha 1 (test mode, `--fps-cap 30`) draws
/// exactly the newest step (docs/guides/conventions.md#update-and-render).
[[nodiscard]] PlayerSnapshot interpolate(const PlayerSnapshot& previous, const PlayerSnapshot& current, float alpha);

/// What a player takes from his character type's configuration beyond the model and its files
/// (characters::PlayerTraits): the class's damage table (`CfgChar` `+0xb8`), scaled by his Warrior class's percentage
/// and written over his own copy of the Anim Range List (combat::applyClassDamage()), and his power class. Empty, the
/// list's own damage plays unscaled with class 64's runtime values (a level without a configuration).
struct PlayerClass {
    std::vector<std::int16_t> damage;
    int damagePercent = 0;                        ///< The Warrior class byte `+0x06`; 0 leaves the damage unscaled.
    std::optional<combat::PowerClass> powerClass; ///< Nothing keeps combat::kPlayerPowerClass.
};

/// The class a player made as `type` plays with, from `types` (characters::CharacterTypes::playerTraitsOf()); empty
/// when `types` has no such type. The one path for every player, the one a level starts with and a changed one alike,
/// as the original writes the class's damage when any human is made (docs/research/combat.md#damage-table).
[[nodiscard]] PlayerClass playerClassOf(const characters::CharacterTypes& types, int type);

/// Player 1: the human and its follow camera.
class Player {
  public:
    /// A player playing `character` (which must outlive it), spawned at `start` on `mesh` (may be null), the camera
    /// set up behind it, with `playerClass`'s damage and power class (docs/research/combat.md#damage-table).
    Player(const PlayerCharacter& character, const raycast::CollisionMesh* mesh, const PlayerStart& start,
           const PlayerClass& playerClass = {});
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;
    Player(Player&&) = delete;
    Player& operator=(Player&&) = delete;
    ~Player() = default;

    /// One update of 1/30 s from `pad` (port 1): the buttons turned into a command (combat::CommandMatcher with the
    /// street's tables) and written with the left stick and the camera's view into the human's per-player record, the
    /// characters' step (humans()) with `targets` to fight, then the camera with the right stick. A human that fell
    /// out of the world is put back at the start (**Coney's choice**: the original fails the mission, which Coney has
    /// no flow for yet).
    void update(const Pad& pad, const raycast::CollisionMesh* mesh, std::span<Combatant* const> targets = {});

    /// Puts the human at `start` on `mesh` (spawned there as at a level start) and the camera behind it, with nothing
    /// to blend from: the debug menus' teleport. Coney's own tool; the original has none.
    void teleport(const raycast::CollisionMesh* mesh, const PlayerStart& start);
    /// Places the camera behind the human again with the current follow settings (camera::followDefaults()), with
    /// nothing to blend from: the debug menus' camera reset.
    void resetCamera();
    /// Places the camera `distance` metres from the human's look-at point with its view facing `viewHeading` (radians),
    /// with nothing to blend from (camera::FollowCamera::place()): a trace's `--start`.
    void placeCamera(float distance, float viewHeading);
    /// The enemy query the camera's sprint zoom asks the player's brain (`0x0021d408`): the distance to his nearest
    /// enemy, or none when he has none. Coney has no brains yet, so whatever owns the enemies sets it; none until then.
    void setNearestEnemy(std::optional<float> distance) { m_nearestEnemy = distance; }
    /// Takes the pad away from the human or gives it back: while it is away update() leaves the stick centred and no
    /// command in the record, so only the brain moves him (a player's brain set dead, docs/research/ai.md#scripted).
    void setPadControlled(bool padControlled);
    /// Whether the pad drives the human (true until setPadControlled(false)).
    [[nodiscard]] bool padControlled() const { return m_padControlled; }
    /// Hands the player's follow camera to `cameras` (which must outlive the player, or be detached with nullptr):
    /// from then on update() steps the cameras rather than the follow camera alone, the stick is turned by the current
    /// camera's view, the snapshots show it, and the player's slow-motion events reach it.
    void setCameras(camera::Cameras* cameras);

    [[nodiscard]] const Human& human() const { return m_human; }
    [[nodiscard]] Human& human() { return m_human; }
    /// The characters' step the player's human is slot 0 of; other humans (and the brains' hook) join it here.
    [[nodiscard]] Humans& humans() { return m_humans; }
    /// The last update's command.
    [[nodiscard]] combat::CommandId command() const { return m_matcher.command(); }
    [[nodiscard]] const camera::FollowCamera& camera() const { return m_camera; }
    [[nodiscard]] camera::FollowCamera& camera() { return m_camera; }
    /// Where a human that fell out of the world is put back: the start it was made at.
    [[nodiscard]] const PlayerStart& start() const { return m_start; }
    /// How often the human has been put back at the start.
    [[nodiscard]] std::uint32_t respawns() const { return m_respawns; }
    /// The state after the last update and the one before it (the same until the first update).
    [[nodiscard]] const PlayerSnapshot& current() const { return m_current; }
    [[nodiscard]] const PlayerSnapshot& previous() const { return m_previous; }

  private:
    // The snapshot of the state now.
    [[nodiscard]] PlayerSnapshot capture() const;

    PlayerStart m_start;
    combat::CommandTables m_tables = combat::CommandTables::street();
    combat::CommandMatcher m_matcher;
    Human m_human;
    Humans m_humans; // holds m_human, which is why the player is neither copied nor moved
    camera::FollowCamera m_camera;
    camera::Cameras* m_cameras = nullptr; // the manager the follow camera belongs to; null steps it alone
    std::uint32_t m_respawns = 0;
    bool m_padControlled = true;
    std::optional<float> m_nearestEnemy; // the distance to the nearest enemy, none with no enemies
    PlayerSnapshot m_previous;
    PlayerSnapshot m_current;
};

} // namespace coney::human
