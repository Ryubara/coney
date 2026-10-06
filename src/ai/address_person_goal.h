// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "ai/goal.h"

// GoalAddressPerson: the human never walks. It turns to a target human and keeps facing it while it is within a range,
// waits for it to come within an approach distance, then plays a scene (its speech) with the script's callback and is
// done; with no scene it stays facing the target until it is removed. The scene is the play-animation goal (type
// `0x21`), pushed above it; the scene system calls the callback.
// Research: docs/research/ai.md#address-person

namespace coney::ai {

class ScriptServices;

/// The address-person goal's head look and the play-animation goal's scene, ms and updates.
inline constexpr std::uint64_t kAddressLookPeriod = 30;
/// Beyond this off the target (radians, 60°) the human turns to it again.
inline constexpr float kAddressTurnAngle = 1.0471976F;

/// The play-animation goal (type `0x21`): plays a scene with the human in it, done when the scene has finished or the
/// human is gone; its End stops the scene. The scene system calls the callback.
/// @orig 0x002e4980 PlayAnimationGoal_Init (unknown)
class PlayAnimationGoal final : public Goal {
  public:
    /// Plays scene `scene` through `services` (which must outlive it), calling `callback` (empty for none) back.
    PlayAnimationGoal(ScriptServices& services, int scene, std::string callback)
        : Goal(GoalType::PlayAnimation), m_services(&services), m_scene(scene), m_callback(std::move(callback)) {}

    /// Starts the scene with the human in it.
    /// @orig 0x002e49f8 PlayAnimationGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Done when the scene has finished; otherwise stops for the update.
    /// @orig 0x002e4a70 PlayAnimationGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;
    /// Stops the scene.
    /// @orig 0x002e4a50 PlayAnimationGoal_End (unknown)
    void end(Brain& brain) override;

    /// The scene.
    [[nodiscard]] int scene() const { return m_scene; }

  private:
    ScriptServices* m_services;
    int m_scene;            // +0x10
    std::string m_callback; // +0x1c
};

/// `GoalAddressPerson`'s arguments.
struct AddressOrder {
    double target = 0;     ///< The human addressed (`+0x10`).
    float approach = 0.0F; ///< The scene starts once the target is this close, m (`+0x14`).
    float range = 0.0F;    ///< Within this the human keeps facing the target, m (`+0x18`).
    int scene = -1;        ///< The speech: a scene id, negative for none (`+0x1c`, `+0x24`).
    std::string callback;  ///< The scene's callback; empty for none (`+0x28`).
};

/// The address-person goal (type `0x57`).
/// @orig 0x002cc408 AddressPersonGoal_Init (unknown)
class AddressPersonGoal final : public Goal {
  public:
    /// A goal as `order` says, finding its target and playing its scene through `services` (which must outlive it).
    AddressPersonGoal(ScriptServices& services, AddressOrder order)
        : Goal(GoalType::AddressPerson), m_services(&services), m_order(std::move(order)) {}

    /// When the target is within twice the approach distance: clears the actions and turns to it.
    /// @orig 0x002cc488 AddressPersonGoal_Start (unknown)
    void start(Brain& brain) override;
    /// Done when the target is gone; waits while actions are queued; within the range, turns to the target (led by
    /// its velocity) when more than 60° off it, and stops; state 0, once the target is within the approach: state 1,
    /// and with a scene pushes the play-animation goal; state 1 with a scene: state 2; state 2: done. **Coney
    /// choice**: the head look every 30 updates (`0x0029a000`) is not built (Coney has no head look).
    /// @orig 0x002cc588 AddressPersonGoal_Process (unknown)
    [[nodiscard]] GoalStatus process(Brain& brain) override;

    /// Its state (`+0x20`): 0 waiting for the target, 1 the scene pushed (or none to play), 2 the scene over.
    [[nodiscard]] int state() const { return m_state; }
    /// The order.
    [[nodiscard]] const AddressOrder& order() const { return m_order; }

  private:
    ScriptServices* m_services;
    AddressOrder m_order;
    int m_state = 0;
};

/// What `GoalAddressPerson(human, target, approach, range, speech, callback)` does for `brain`: pushes the goal.
/// @orig 0x002cc348 Goal_AddressPerson (unknown)
bool goalAddressPerson(Brain& brain, ScriptServices& services, AddressOrder order);

} // namespace coney::ai
