// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    /// A spot about `metres` from player 1 that the camera does not see, for the `turn`-th human placed there;
    /// nothing when there is none.
    [[nodiscard]] virtual std::optional<anim::Vec3> outOfSight(float metres, std::size_t turn) const = 0;
    /// Whether the human with `handle` is alive (made, not deleted and not down).
    [[nodiscard]] virtual bool alive(double handle) const = 0;
    /// Makes the human and returns its handle; 0 (NilHandle) when none was made.
    virtual double spawn(const SpawnRequest& request) = 0;
    /// Calls the Lua function `callback` with the new human's handle.
    virtual void spawned(std::string_view callback, double handle) = 0;

  protected:
    SpawnerWorld(SpawnerWorld&&) = default;
    SpawnerWorld& operator=(SpawnerWorld&&) = default;
};

/// What the camera sees, for outOfSightSpot(): a cone from `eye` along `forward` (length 1).
struct SightCone {
    anim::Vec3 eye;
    anim::Vec3 forward{0.0F, 1.0F, 0.0F};
    float halfAngleRadians = 0.0F;
    float range = 0.0F; ///< The far clip, metres: farther is not seen.
};

/// How many of the best spots outOfSightSpot() hands out in turn.
inline constexpr std::size_t kOutOfSightSpots = 4;

/// **Coney stand-in** for the original's out-of-sight placement (`0x001673b8`, not on the page): of `spots` (the
/// level's flags) those outside `view`, the kOutOfSightSpots whose distance from `player` is nearest `metres`, taken
/// in turn by `turn` so that humans placed one after another do not stand on one spot. Nothing when every spot is seen.
[[nodiscard]] std::optional<anim::Vec3> outOfSightSpot(std::span<const anim::Vec3> spots, anim::Vec3 player,
                                                       const SightCone& view, float metres, std::size_t turn);

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
    std::size_t nextType = 0;      ///< The next entry of the type list to make (Coney stand-in, see pickType()).
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

  private:
    // Whether `spawner`'s state lets it spawn now (the update's first step).
    [[nodiscard]] static bool ready(const Spawner& spawner, std::uint64_t nowMs, const SpawnerWorld& world);
    // The next character type of the spawner's list (0 when the list is empty).
    [[nodiscard]] static int pickType(Spawner& spawner);
    // Makes one human from `spawner`, counts it and calls its callback.
    void spawnOne(Spawner& spawner, std::uint64_t nowMs, SpawnerWorld& world);

    std::map<int, std::vector<Spawner>> m_spawners;
    std::uint64_t m_nowMs = 0; // the last update's time, which a state set between updates counts from
    std::size_t m_placed = 0;  // humans placed out of sight so far, by every spawner: the turn of the next
};

} // namespace coney::ai
