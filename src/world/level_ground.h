// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "animation/anim_math.h"
#include "human/place.h"
#include "human/player.h"
#include "raycast/collision_mesh.h"
#include "world/path_map.h"

namespace coney::world {

/// A passive human to fight placed by the ground (human::TargetHuman, Coney's own test target): its feet, heading and
/// health. Only a sandbox layout places them (docs/guides/sandbox.md).
struct SceneTarget {
    anim::Vec3 feet;
    float headingDegrees = 0.0F; ///< 0 faces +y.
    int health = 0;
};

/// What the play mode's simulation reads of the place it plays in: the collision mesh the humans stand on and the
/// cameras test against, where the player starts, the named places, the sandbox's targets and fighters, and the
/// level's path data. A level's scenery and a sandbox implement it; drawing and streaming are the platform's
/// (platform::PlayScenery adds them), so a build with no renderer can still run the simulation on a ground.
///
/// It is simulation state in the sense of docs/guides/network-architecture.md#checklist: what it answers is the
/// server's, and nothing here reads the camera, the window or the frame rate.
///
/// Research: docs/guides/network-architecture.md#sub-loop (the simulation out of `src/platform/`)
class LevelGround {
  public:
    virtual ~LevelGround() = default;
    LevelGround() = default;
    LevelGround(const LevelGround&) = delete;
    LevelGround& operator=(const LevelGround&) = delete;
    LevelGround(LevelGround&&) = delete;
    LevelGround& operator=(LevelGround&&) = delete;

    /// The collision mesh the player stands on and the camera tests against. Valid as long as the ground.
    [[nodiscard]] virtual const raycast::CollisionMesh& collision() const = 0;
    /// Where the player starts.
    [[nodiscard]] virtual human::PlayerStart start() const = 0;
    /// Where start() comes from, for the player's line ("the level's start", "spawn point lane").
    [[nodiscard]] virtual std::string startSource() const = 0;
    /// What is played, for the debug menus and the summary: the level's name, or "sandbox " and the layout's title.
    [[nodiscard]] virtual std::string name() const = 0;
    /// The named places the debug menus' teleport offers: the start (a level), or the spawn points (a sandbox).
    /// @coney-only the debug menus' teleport list; the original has none
    [[nodiscard]] virtual std::vector<human::Place> places() const {
        const human::PlayerStart s = start();
        return {human::Place{startSource(), s.position, s.headingDegrees}};
    }
    /// The passive humans to fight (Coney's own sandbox targets): a sandbox layout's `target` lines; none in a level.
    [[nodiscard]] virtual std::vector<SceneTarget> targets() const { return {}; }
    /// Where the AI humans that fight the player start: a sandbox layout's `fighter` lines; none in a level.
    [[nodiscard]] virtual std::vector<human::Place> fighters() const { return {}; }
    /// The level's path data, decoded, for the AI's route planner; null where there is none (a sandbox, or a level
    /// whose path data does not decode).
    [[nodiscard]] virtual const PathMap* pathMap() const { return nullptr; }
    /// The collision mesh the level's glass panes and doors switch on and off and retag (world_objects::ObjectWorld);
    /// null where the ground has no such objects (a sandbox).
    [[nodiscard]] virtual raycast::CollisionMesh* objectCollision() { return nullptr; }
    /// The path data the level's doors retag (world_objects::ObjectWorld); null where the ground has none (a sandbox).
    [[nodiscard]] virtual PathMap* objectPaths() { return nullptr; }
};

} // namespace coney::world
