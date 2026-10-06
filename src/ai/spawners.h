// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ai/route_planner.h"
#include "animation/anim_math.h"
#include "scripting/human_bindings.h"

// The gangs' spawners: points that make gang members over time (`GangAddSpawner`, switched by `GangStartSpawner`).
// Each update runs every spawner in use against its state: when the state says it is ready, the delay since its last
// spawn has passed, fewer of its humans are alive than it allows and it has not made its total, it makes one human
// and calls its callback with it. The play mode makes the human as `HuCreate` does.
// Research: docs/research/ai.md#spawners, docs/references/spawner-states.md

namespace coney::ai {

/// A gang has at most this many spawners (gang `+0x640`); a fifth is ignored.
inline constexpr std::size_t kGangSpawners = 4;

/// The spawner states the update knows (spawner `+0x52`, docs/references/spawner-states.md).
enum class SpawnerState : std::uint8_t {
    Stopped = 0,
    On = 1,
    Delayed = 2,
    NearPlayer = 3,
    PoliceDispatch = 4,
    FarFromPlayer = 5,
    DispatchingPolice = 6,
    Reinforcements = 7,
    OutOfSight = 8,
    GangDispatch = 9,
    DispatchingGang = 10,
    KeepUpNumbers = 11,
};

/// A human a spawner makes, for the world to create as `HuCreate(name, type, position, heading, model, 0, gang)`.
struct SpawnRequest {
    int gang = -1;
    std::string name;  ///< `<spawner><count>`.
    int type = 0;      ///< The character type, from the spawner's list.
    std::string model; ///< The spawner's model string (`HuCreate`'s fifth argument).
    std::array<float, 3> position{};
    int heading = 0;
};

/// What the spawners ask of the level.
class SpawnerWorld {
  public:
    SpawnerWorld() = default;
    SpawnerWorld(const SpawnerWorld&) = delete;
    SpawnerWorld& operator=(const SpawnerWorld&) = delete;
    virtual ~SpawnerWorld() = default;

    /// Where player 1 is; nothing when there is none.
    [[nodiscard]] virtual std::optional<anim::Vec3> playerPosition() const = 0;
    /// Where a human of gang `gang` placed out of the camera's sight with the spawner's value `value` (metres)
    /// stands: outOfSightNode() from player 1's position and camera, then the gang's turf; nothing when there is no
    /// such spot this update.
    [[nodiscard]] virtual std::optional<anim::Vec3> outOfSight(float value, int gang) = 0;
    /// Whether the human with `handle` is alive (made, not deleted and not down).
    [[nodiscard]] virtual bool alive(double handle) const = 0;
    /// Whether a camera sees the sphere of `radius` metres about `centre`.
    [[nodiscard]] virtual bool seen(anim::Vec3 centre, float radius) const = 0;
    /// Makes the human and returns its handle; 0 (NilHandle) when none was made.
    virtual double spawn(const SpawnRequest& request) = 0;
    /// Calls the Lua function `callback` (a dotted or `:` name) with the new human's handle, the gang's id and the
    /// spawner's name, when it names a function.
    virtual void spawned(std::string_view callback, double handle, int gang, std::string_view spawner) = 0;

  protected:
    SpawnerWorld(SpawnerWorld&&) = default;
    SpawnerWorld& operator=(SpawnerWorld&&) = default;
};

/// The camera a placement hides from: where it is, its forward (length 1; its orientation's y axis) and half its field
/// of view.
struct PlacementCamera {
    anim::Vec3 eye;
    anim::Vec3 forward{0.0F, 1.0F, 0.0F};
    float halfFovRadians = 0.0F;
};

/// A node farther than this from the camera is out of sight whatever its direction (`0x0050cc60`, metres).
inline constexpr float kOutOfSightFar = 70.0F;
/// The first try's goal is this far straight ahead of the camera (metres).
inline constexpr float kFirstGoalAhead = 100.0F;
/// The cone a node must lie outside is half the field of view plus this (`+0x2ac`, 10 degrees).
inline constexpr float kConeMarginRadians = 10.0F * std::numbers::pi_v<float> / 180.0F;
/// The searches a placement makes before it gives up for this update: the first ahead and 16 turned.
inline constexpr int kPlacementTries = 17;
/// The outward search fails once its open list would hold more than this (`0x002531c0`)...
inline constexpr std::size_t kSearchOpenMax = 1000;
/// ...or it has closed this many nodes (`0x002534d0`).
inline constexpr std::size_t kSearchClosedMax = 3000;

/// A uniform number in [0, 1) for the turned tries' angles (the original's `Random_Float`, `0x00335420`).
using PlacementRandom = std::function<float()>;

/// The route node a human placed out of `camera`'s sight stands on (docs/research/ai.md#spawner-placement). Distances
/// are from the origin, the camera's eye at `player`'s height, in 3D. The search starts at the node `player` would
/// leave a route from (RoutePlanner::startNode(); none: nothing). The cone's axis is the camera's forward with its z
/// cleared, not normalised again, and its half-angle half the field of view + kConeMarginRadians. Each of
/// kPlacementTries draws an angle in [h, 2 pi - h] from `random` and searches (searchOutward()): the first toward the
/// origin + kFirstGoalAhead x the forward (its draw unused), each later toward the origin + 2 x `value` x the
/// flattened forward turned by the angle about the vertical. Nothing when no try finds a node.
/// @orig 0x001673b8 Gang_PlaceOutOfSight (unknown)
[[nodiscard]] std::optional<anim::Vec3> outOfSightNode(const RoutePlanner& planner, anim::Vec3 player,
                                                       const PlacementCamera& camera, float value,
                                                       const PlacementRandom& random);

/// One outward search over `planner`'s graph from node `start` toward `goal`: A* (g the route edge costs with mask
/// `0xff`, h 16 x the 3D distance to the goal, a node already open taken again only for a lower g), skipping the
/// links with the avoid bit; each node popped (the start too) is found when its distance from `origin` exceeds
/// kOutOfSightFar, or exceeds `value` with the unit direction from `origin` having a dot product below cos
/// `halfAngle` with `coneAxis`. Nothing when the open list empties or outgrows kSearchOpenMax, or kSearchClosedMax
/// nodes close.
/// @orig 0x00251d28 Route_SearchOutward (unknown)
[[nodiscard]] std::optional<std::uint32_t> searchOutward(const RoutePlanner& planner, std::uint32_t start,
                                                         anim::Vec3 origin, anim::Vec3 goal, anim::Vec3 coneAxis,
                                                         float halfAngle, float value);

/// One spawner: what `GangAddSpawner` gave it and what it has done since.
struct Spawner {
    script::SpawnerCall call;      ///< The fields `GangAddSpawner` set (the position, the types, the limits).
    int state = 0;                 ///< `+0x52`.
    int value = 0;                 ///< `+0x68`: seconds for state 2, metres for 3, 5, 6 and 8.
    bool inUse = true;             ///< `+0x50`: cleared once it has made its total.
    int made = 0;                  ///< `+0x56`: humans made so far.
    std::uint64_t nextSpawnMs = 0; ///< `+0x64`: the earliest game time of the next spawn.
    std::uint64_t deadlineMs = 0;  ///< `+0x7c`: state 2's deadline.
    std::vector<double> humans;    ///< The handles of the humans it made that may still be alive.
    std::size_t typeIndex = 0;     ///< `+0x4c`: the type list's entry made last (0 for a new spawner).
    int maxConcurrent = 0;         ///< `+0x5a`: its humans alive at once; negative -n makes waves of n.
    bool waveRunning = false;      ///< `+0x51`: a wave is running (ready in any state until it fills).
    bool offScreen = false;        ///< `+0x8c`: it spawns only while no camera sees its spot (state 11 sets it).
};

/// The level's spawners, by gang.
class Spawners {
  public:
    /// `GangAddSpawner`: a fifth spawner on a gang is ignored. It starts in the call's kind as its state, with the
    /// call's value, ready to spawn at once.
    /// @orig 0x00166ff8 Gang_AddSpawner (unknown)
    void add(const script::SpawnerCall& call);
    /// `GangStartSpawner`: the gang's spawner named `name` takes `mode` when it is one the original accepts (0-5, 7, 8,
    /// 9, 11), and `value` unless it is -1 (written even when the mode is not accepted); its next spawn is now.
    /// @orig 0x0016afc8 Gang_StartSpawner (unknown)
    /// @orig 0x00168cd0 Gang_SetSpawnerState (unknown)
    void start(int gang, std::string_view name, int mode, int value);
    /// One update at game time `nowMs`: each spawner in use that is ready and may spawn makes one human.
    /// @orig 0x001681a0 Gang_UpdateSpawners (unknown)
    void update(std::uint64_t nowMs, SpawnerWorld& world);

    /// The spawners of gang `gang`, in the order they were added.
    [[nodiscard]] const std::vector<Spawner>& of(int gang) const;
    /// The gang's spawner named `name`; null when none is.
    [[nodiscard]] const Spawner* find(int gang, std::string_view name) const;

    /// `GangSetMaxConcurrent(gang, name, count)`: the spawner's limit of humans alive at once, as 16 bits; a negative
    /// -n spawns waves of n, each only once the last has all died. An unknown gang or name does nothing.
    /// @orig 0x0016b0b0 Gang_SetSpawnerMaxConcurrent (unknown)
    /// @orig 0x00168eb8 GangSpawner_SetMaxConcurrent (unknown)
    void setMaxConcurrent(int gang, std::string_view name, int count);
    /// `GangSetSpawnerMustBeOffScreen(gang, name, on)`: while on, the spawner does nothing in an update in which a
    /// camera sees the 0.3 m sphere 1.6 m above its position. An unknown gang or name does nothing.
    /// @orig 0x0016b070 Gang_SetSpawnerMustBeOffScreen (unknown)
    /// @orig 0x00168e68 GangSpawner_SetMustBeOffScreen (unknown)
    void setMustBeOffScreen(int gang, std::string_view name, bool on);

  private:
    // Whether `spawner`'s state lets it spawn now (the update's first step).
    [[nodiscard]] static bool ready(const Spawner& spawner, std::uint64_t nowMs, const SpawnerWorld& world);
    // The next character type of the spawner's list: the index moves on first, back to 0 at the list's end or at an
    // entry of 0.
    // @orig 0x0016d810 Gang_SpawnerNextType (unknown)
    [[nodiscard]] static int pickType(Spawner& spawner);
    // The gang's spawner named `name`; null when none is.
    [[nodiscard]] Spawner* named(int gang, std::string_view name);
    // Whether `spawner` has room for another human: fewer alive than its limit (a wave's n), or state 11.
    [[nodiscard]] static bool roomFor(const Spawner& spawner);
    // Makes one human from `spawner`, counts it and calls its callback.
    void spawnOne(Spawner& spawner, std::uint64_t nowMs, SpawnerWorld& world);

    std::map<int, std::vector<Spawner>> m_spawners;
    std::uint64_t m_nowMs = 0; // the last update's time, which a state set between updates counts from
};

} // namespace coney::ai
