// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string_view>

#include "animation/anim_math.h"
#include "animation/skeleton.h"
#include "camera/follow_camera.h"
#include "characters/anim_set.h"
#include "characters/character_assets.h"
#include "characters/character_data.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "core/pad.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "raycast/collision_mesh.h"

// Player 1 in a level: the character it plays, the human the pad drives and the follow camera behind it, stepped
// together in the characters' order (pad, human, then cameras). Platform-neutral: the play mode draws it, the disc
// tests drive it headless. Research: docs/research/characters.md#update, docs/research/camera.md

namespace coney::human {

/// The player's model: Rembrandt, as `level99.lua` creates him (docs/research/characters.md#creation).
inline constexpr std::string_view kPlayerModel = "warr_re_cv";

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

  private:
    PlayerCharacter(characters::CharacterAssets assets, characters::CharacterData generic);

    characters::CharacterAssets m_assets;
    characters::CharacterData m_generic;
    characters::AnimSet m_anims;
    anim::Skeleton m_skeleton;
};

/// What drawing the player needs from one simulation step, and nothing else: drawing reads only these, so a renderer
/// can draw between two steps by interpolating the previous and the current one.
struct PlayerSnapshot {
    anim::Vec3 feet;         ///< The human's position (game axes).
    float heading = 0.0F;    ///< Radians, 0 facing +y.
    anim::Pose pose;         ///< The blended animation pose.
    anim::Vec3 cameraEye;    ///< The follow camera's position.
    anim::Vec3 cameraTarget; ///< Its look-at point.
};

/// The snapshot `alpha` (0 to 1) of the way from `previous` to `current`: positions lerped, the heading along the
/// shorter way round, the pose blended as two animation poses are (each bone's rotation slerped). Returns `current`
/// itself at alpha 1 or more and `previous` at 0 or less, so a render at alpha 1 (test mode, `--fps-cap 30`) draws
/// exactly the newest step (docs/guides/conventions.md#update-and-render).
[[nodiscard]] PlayerSnapshot interpolate(const PlayerSnapshot& previous, const PlayerSnapshot& current, float alpha);

/// Player 1: the human and its follow camera.
class Player {
  public:
    /// A player playing `character` (which must outlive it), spawned at `start` on `mesh` (may be null), the camera
    /// set up behind it.
    Player(const PlayerCharacter& character, const raycast::CollisionMesh* mesh, const PlayerStart& start);

    /// One update of 1/30 s from `pad` (port 1): the human with the left stick turned by the camera, then the camera
    /// with the right stick. A human that fell out of the world is put back at the start (**Coney's choice**: the
    /// original fails the mission, which Coney has no flow for yet).
    void update(const Pad& pad, const raycast::CollisionMesh* mesh);

    /// Puts the human at `start` on `mesh` (spawned there as at a level start) and the camera behind it, with nothing
    /// to blend from: the debug menus' teleport. Coney's own tool; the original has none.
    void teleport(const raycast::CollisionMesh* mesh, const PlayerStart& start);
    /// Places the camera behind the human again with the current follow settings (camera::followDefaults()), with
    /// nothing to blend from: the debug menus' camera reset.
    void resetCamera();

    [[nodiscard]] const Human& human() const { return m_human; }
    [[nodiscard]] const camera::FollowCamera& camera() const { return m_camera; }
    /// How often the human has been put back at the start.
    [[nodiscard]] std::uint32_t respawns() const { return m_respawns; }
    /// The state after the last update and the one before it (the same until the first update).
    [[nodiscard]] const PlayerSnapshot& current() const { return m_current; }
    [[nodiscard]] const PlayerSnapshot& previous() const { return m_previous; }

  private:
    // The snapshot of the state now.
    [[nodiscard]] PlayerSnapshot capture() const;

    PlayerStart m_start;
    Human m_human;
    camera::FollowCamera m_camera;
    std::uint32_t m_respawns = 0;
    PlayerSnapshot m_previous;
    PlayerSnapshot m_current;
};

} // namespace coney::human
