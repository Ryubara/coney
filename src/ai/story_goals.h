// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ai/goal.h"
#include "ai/move_to_flag_goal.h"
#include "animation/anim_math.h"

// The goals the story's second and third missions give their humans: leaving through an exit flag, walking a path of
// flags, looking for an enemy to fight, throwing what they hold, and idling at a flag. Each is driven by its brain as
// the first mission's goals are (ai/goal.h); what they ask of the world beyond the brain (the flags, the exits, the
// player's view, the removal of a human) comes in as functions, which the level's scripted brains give them.
// Research: docs/references/bindings/ai.md, docs/research/ai.md#scripted

namespace coney::ai {

class Brain;
class ScriptServices;

/// The exit goal's checks: every this many ms it may kill a human far out of sight, and that is beyond this many
/// metres of every player; one that arrives within this many metres of a player picks another exit.
inline constexpr std::uint64_t kExitCheckMs = 8000;
inline constexpr float kExitOutOfSight = 60.0F;
inline constexpr float kExitSeenRange = 8.0F;

/// What the exit goal asks of the level.
struct ExitServices {
    /// Where the flag with a handle is; nothing once it is gone.
    std::function<std::optional<world_objects::Placement>(double flag)> flag;
    /// The nearest enabled exit flag (activity 8) to a point other than `exclude`; nothing when there is none.
    std::function<std::optional<double>(anim::Vec3 from, double exclude)> nearestExit;
    /// Where player 1 stands; nothing without one.
    std::function<std::optional<anim::Vec3>()> viewer;
    /// Takes the human out of the level (it reached an exit unseen).
    std::function<void(Brain&)> remove;
    /// Kills the human out of sight (`HuKill`'s body).
    std::function<void(Brain&)> kill;
};

/// `GoalMoveToExitFlag`'s goal (type 2): the human walks to the exit flag (or the offset point) at its gait; one that
/// arrives where player 1 can see it (**Coney stand-in** for the camera tests: within kExitSeenRange of him) picks
/// the nearest other exit and goes on, one that arrives unseen is removed; every kExitCheckMs, one more than
/// kExitOutOfSight from player 1 is killed out of sight instead (**Coney stand-in** for "off screen": the distance
/// alone) and the goal ends. A blocked way also makes it pick another exit.
/// @orig 0x002da8f8 MoveToExitFlagGoal_Init (unknown)
class MoveToExitFlagGoal final : public Goal {
  public:
    /// Leaving through `order`'s flag, with `services`.
    MoveToExitFlagGoal(const MoveToFlagOrder& order, ExitServices services);
    /// The target point and the first check's time.
    /// @orig 0x002da960 MoveToExitFlagGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Clears the actions.
    void resume(Brain& brain) override;
    /// The walk, the arrival and the checks (above).
    /// @orig 0x002dacf8 MoveToExitFlagGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The flag it is heading for now.
    [[nodiscard]] double flag() const { return m_order.flag; }

  private:
    // Aims at the order's flag (and offset); false when the flag is gone.
    bool aim();
    // Takes the nearest other exit; false when there is none.
    bool switchExit(const Brain& brain);

    MoveToFlagOrder m_order;
    ExitServices m_services;
    anim::Vec3 m_target;
    std::uint64_t m_nextCheckMs = 0;
};

/// `GoalTravelPath`'s goal (type 0x38): walks the human along a path's points in order, a MoveToFlagGoal pushed for
/// each; at the end mode 0 ends it, 1 loops to the first point, 2 turns round. A point the services cannot find is
/// skipped (the MoveToFlagGoal ends at once). **Coney choice**: a path with no points ends at once.
/// @orig 0x002e0748 TravelPathGoal_Init (unknown)
class TravelPathGoal final : public Goal {
  public:
    /// Along `points` (flag handles), ending by `mode`, backwards with `reverse`, at `gait` with `radius`, finding the
    /// points through `services` (which must outlive it).
    TravelPathGoal(std::vector<double> points, int mode, bool reverse, int gait, float radius, FlagServices& services);
    /// Each time it is processed with no sub-goal over it: the next point (the first when none yet), pushed as a
    /// MoveToFlagGoal; done past the last point in mode 0.
    /// @orig 0x002e0968 TravelPathGoal_Process (unknown)
    /// @orig 0x002e07d0 TravelPathGoal_NextPoint (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The index of the point it is walking to (-1 before the first).
    [[nodiscard]] int current() const { return m_current; }

  private:
    std::vector<double> m_points;
    int m_mode;
    int m_step; // +1 forwards, -1 backwards
    int m_gait;
    float m_radius;
    FlagServices* m_services;
    int m_current = -1;
};

/// `GoalMelee`'s finding goal (type 0x41): once a second, the nearest standing human hostile to the human (its gang's
/// enemy, or on its enemy list) within its sight range is fought (Brain::fight()), which pushes the fight goal over
/// this one; when that fight ends this one looks again. **Coney choices**: the search values (90, 30, 10) and the
/// melee goal's 4000 ms are not traced, so the search is the brain's sight range; it never ends by itself.
/// @orig 0x002add08 Goal_Melee (unknown)
class FindEnemyGoal final : public Goal {
  public:
    FindEnemyGoal() : Goal(GoalType::FindEnemy) {}
    /// The search, once a second.
    [[nodiscard]] GoalStatus process(Brain& brain) override;

  private:
    std::uint64_t m_nextSearchMs = 0;
};

/// The nearest standing human hostile to `brain` among `candidates` within its sight range; null when none is.
[[nodiscard]] Brain* nearestHostile(Brain& brain, std::span<Brain* const> candidates);
/// The same within `range` metres.
[[nodiscard]] Brain* nearestHostileWithin(Brain& brain, std::span<Brain* const> candidates, float range);
/// Every brain the gangs know (the members of every gang in use, through `brain`'s gang) and `brain`'s enemies.
[[nodiscard]] std::vector<Brain*> knownBrains(Brain& brain);

/// `GoalThrowObject`'s goal (type 0x5d): beyond `range` of the target the human walks toward it; within it the human
/// turns to it, then throws what it holds. **Coney stand-in** for the throw (Coney has no thrown objects yet): what
/// it holds is let go. It ends at once, not completed, when the human holds nothing or the target is gone; its
/// callback is scheduled with (human, completed).
/// @orig 0x002cf690 ThrowObjectGoal_Process (unknown)
class ThrowObjectGoal final : public Goal {
  public:
    /// At `target` (found through `locate`) from within `range`, approaching at `gait`; `callback` may be empty.
    ThrowObjectGoal(std::function<std::optional<anim::Vec3>(double)> locate, double target, float range, int gait,
                    std::string callback, ScriptServices* services);
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Schedules the callback.
    void end(Brain& brain) override;

  private:
    std::function<std::optional<anim::Vec3>(double)> m_locate;
    double m_target;
    float m_range;
    int m_gait;
    std::string m_callback;
    ScriptServices* m_services;
    bool m_completed = false;
};

/// `GoalPlayDynIdle`'s goal (type 0x23): the human walks to the flag (gait 2) until within 1 m, turns to its heading,
/// then idles there for `timeMs` (-1: for ever) and ends. With no flag it idles where it stands. **Coney stand-in**:
/// the three clips are kept but not played (the dynamic slots are not built), so it stands.
/// @orig 0x002d3a18 PlayDynIdleGoal_Init (unknown)
class PlayDynIdleGoal final : public Goal {
  public:
    /// At `flag` (0: where it stands) through `services`, for `timeMs`, the clips' names kept.
    PlayDynIdleGoal(double flag, std::vector<std::string> clips, int timeMs, FlagServices& services);
    /// The walk, the turn, then the idle.
    /// @orig 0x002d3d58 PlayDynIdleGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// The clips: start, loop, end.
    [[nodiscard]] const std::vector<std::string>& clips() const { return m_clips; }
    /// Whether it reached its spot and idles.
    [[nodiscard]] bool idling() const { return m_idleSinceMs.has_value(); }

  private:
    double m_flag;
    std::vector<std::string> m_clips;
    int m_timeMs;
    FlagServices* m_services;
    std::optional<std::uint64_t> m_idleSinceMs;
};

} // namespace coney::ai
