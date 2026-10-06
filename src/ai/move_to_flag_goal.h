// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "ai/goal.h"
#include "ai/move_action.h"
#include "animation/anim_math.h"
#include "world_objects/flags.h"

// GoalMoveToFlag: walk or run to a world flag (or a point offset from it) and stop within an arrival radius,
// optionally turning to the flag's heading. It only queues move and turn actions; each time a move ends short of the
// radius the next update plans again. Arriving tells the flag (message 8) and the human's gang.
// Research: docs/research/ai.md#move-to-flag

namespace coney::ai {

class Brain;

/// What the goal asks of the world about its flag. The level's flags and gangs implement it; it must outlive the
/// goals given it.
class FlagServices {
  public:
    FlagServices() = default;
    FlagServices(const FlagServices&) = delete;
    FlagServices& operator=(const FlagServices&) = delete;
    FlagServices(FlagServices&&) = delete;
    FlagServices& operator=(FlagServices&&) = delete;
    virtual ~FlagServices() = default;

    /// Where the flag with `handle` is and which way it faces (degrees, 0 facing +y); nothing once it is gone.
    [[nodiscard]] virtual std::optional<world_objects::Placement> flag(double handle) const = 0;
    /// The flag with `handle` gets message 8 with `user`, who arrived at it (flag vtable `+0x44`). Nothing by
    /// default: Coney's flags take no messages yet.
    virtual void arrived(double /*handle*/, Brain& /*user*/) {}
    /// `member`'s gang hears that it arrived at the flag with `handle` (`Gang_OnEvent`). Nothing by default: Coney
    /// has no gangs yet; this is where they will hook in.
    virtual void tellGang(Brain& /*member*/, double /*handle*/) {}
};

/// The goal's arguments (`GoalMoveToFlag(human, flag, gait, angle, distance, radius, intervalMs, faceFlag, option)`).
struct MoveToFlagOrder {
    double flag = 0;              ///< `+0x20`.
    int gait = 2;                 ///< `+0x24`.
    float angleDegrees = 0.0F;    ///< `+0x28`: the offset's direction.
    float distance = 0.0F;        ///< `+0x30`: the offset's length.
    float radius = 0.0F;          ///< `+0x2c`: the arrival radius.
    std::uint32_t intervalMs = 0; ///< `+0x3c`: the gesture's interval, 0 for none.
    bool faceFlag = false;        ///< `+0x40`: turn to the flag's heading on arrival.
    bool option = false;          ///< `+0x3f`: handed to the move action.
};

/// Turning to the flag's heading is asked for only when the human is more than this off it (radians, 15°).
inline constexpr float kFaceFlagAngle = 0.2618F;
/// Each move it queues starts after a random 0 to this many ms.
inline constexpr int kMoveToFlagDelayMaxMs = 250;

/// The move-to-flag goal (type 1).
/// @orig 0x002da3b0 MoveToFlagGoal_Init (unknown)
class MoveToFlagGoal final : public Goal {
  public:
    /// A goal to move as `order` says, finding its flag through `services` (which must outlive it).
    MoveToFlagGoal(const MoveToFlagOrder& order, FlagServices& services)
        : Goal(GoalType::MoveToFlag), m_order(order), m_services(&services) {}

    /// The target: the flag's position moved `distance` towards `angle` (degrees, the headings' convention: 0 faces
    /// +y); the first gesture tick when there is an interval; then resume(). **Coney choice**: the angle is a world
    /// direction, not turned by the flag's heading (`0x003376c0` is not traced).
    /// @orig 0x002da408 MoveToFlagGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Clears the actions.
    /// @orig 0x002da550 MoveToFlagGoal_Resume (unknown)
    void resume(Brain& brain) override;
    /// Done when the flag is gone; waits while actions are queued; inside the radius, queues a turn to the flag's
    /// heading when asked and more than 15° off it, else arrives (done); outside, queues a move to the target (a start
    /// delay of 0-250 ms) and waits. **Coney choice**: the gesture on each interval tick is not built (the tick is
    /// kept), nor the fight stance's switch-off, and the move's flag-kind bit is left clear (the move does not read
    /// it).
    /// @orig 0x002da588 MoveToFlagGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// When it arrived: message 8 to the flag and the gang's notice.
    /// @orig 0x002da4a0 MoveToFlagGoal_End (unknown)
    void end(Brain& brain) override;

    /// The point it goes to (`+0x10`), and whether it arrived (`+0x3e`).
    [[nodiscard]] anim::Vec3 target() const { return m_target; }
    [[nodiscard]] bool arrived() const { return m_arrived; }
    /// The order.
    [[nodiscard]] const MoveToFlagOrder& order() const { return m_order; }

  private:
    MoveToFlagOrder m_order;
    FlagServices* m_services;
    anim::Vec3 m_target;
    std::uint64_t m_nextTickMs = 0; // +0x38
    bool m_arrived = false;
};

/// What `GoalMoveToFlag` does for `brain`: pushes the goal (refused, false, when the stack is full).
/// @orig 0x002da2c0 Goal_MoveToFlag (unknown)
bool goalMoveToFlag(Brain& brain, const MoveToFlagOrder& order, FlagServices& services);

} // namespace coney::ai
