// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "ai/action.h"
#include "animation/anim_math.h"

// The turn actions: turn the human on the spot until it faces a target, a point or a heading within 15°, then end
// (they do not keep facing it), or give up after 3 s. They write the heading to the brain (`+0x110`) at speed 0, and
// the human's state update turns it. `ActLookAt` queues the one that follows a target.
// Research: docs/research/ai.md#look-at

namespace coney::ai {

/// A turn ends once the human faces its heading within this (radians, 15°), or after kTurnLimitMs.
inline constexpr float kTurnDoneAngle = 0.2618F;
inline constexpr std::uint32_t kTurnLimitMs = 3000;
/// Record `+0x08` flags with which a turn has nothing to do at its start, and those that keep it from ending.
inline constexpr std::uint32_t kTurnRefuseFlags = 0x1c16a40;
inline constexpr std::uint32_t kTurnHoldFlags = 0x20080000;

/// Where a look-at's target is now, by its handle: nothing once it is gone.
using TargetLocator = std::function<std::optional<anim::Vec3>(double handle)>;

/// A turn action.
/// @orig 0x002fdc28 TurnAction_Init (unknown)
class TurnAction final : public Action {
  public:
    /// What it turns to: a target found each update, a point, or a heading.
    enum class Aim : std::uint8_t { Target, Point, Heading };
    /// A turn to `aim` that starts after `delayMs`; the factories below fill in what it aims at.
    TurnAction(Aim aim, std::int16_t delayMs) : Action(delayMs), m_aim(aim) {}

    /// Turns to face the target `target` as `locate` finds it each update (the look-at, vtable `0x005430a0`), with
    /// the turn value `turn` (kept, not read: what reads `+0x10` is not traced) after `delayMs`.
    /// @orig 0x002fe160 LookAtAction_Init (unknown)
    [[nodiscard]] static std::unique_ptr<TurnAction> lookAt(TargetLocator locate, double target, float turn,
                                                            std::int16_t delayMs);
    /// Turns to face `point`, the heading taken once at Start (the turn-to-point, vtable `0x005430e0`).
    /// @orig 0x002fe000 TurnToPointAction_Init (unknown)
    [[nodiscard]] static std::unique_ptr<TurnAction> toPoint(anim::Vec3 point, std::int16_t delayMs = 0);
    /// Turns to `heading` (radians, 0 facing +y).
    [[nodiscard]] static std::unique_ptr<TurnAction> toHeading(float heading, std::int16_t delayMs = 0);

    /// Done at once when the record's `+0x08` has any of kTurnRefuseFlags; else the limit is now + kTurnLimitMs.
    /// @orig 0x002fdc68 TurnAction_Start (unknown)
    [[nodiscard]] ActionStatus start(Brain& brain) override;
    /// The heading to the target (taken again each update for a look-at) goes to the brain at speed 0; done when the
    /// target is gone, when aborted, past the limit, or when `+0x08` has none of kTurnHoldFlags and the human faces
    /// the heading within kTurnDoneAngle. The human stands when it ends.
    /// @orig 0x002fe1b0 LookAtAction_Update (unknown)
    /// @orig 0x002fdd08 TurnAction_Update (unknown)
    [[nodiscard]] ActionStatus update(Brain& brain) override;
    /// Marks it aborted and stops the turn. **Coney choice**: never refused (the forced abort that is refused while
    /// `+0x08` has `0x20000000` has no caller in Coney).
    /// @orig 0x002fdcc8 TurnAction_Abort (unknown)
    [[nodiscard]] bool abort(Brain& brain) override;

    /// The turn value (`+0x10`).
    [[nodiscard]] float turn() const { return m_turn; }
    /// The heading it last aimed at (`+0x0c`).
    [[nodiscard]] float heading() const { return m_heading; }

  private:
    Aim m_aim;
    TargetLocator m_locate;
    double m_target = 0; // +0x1c
    anim::Vec3 m_point;
    float m_heading = 0.0F;      // +0x0c
    float m_turn = 0.0F;         // +0x10
    std::uint64_t m_limitMs = 0; // +0x14
    bool m_aborted = false;      // +0x18
};

} // namespace coney::ai
