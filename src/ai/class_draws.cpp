// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/class_draws.h"

#include <cstdint>
#include <memory>
#include <string_view>

#include "ai/brain.h"
#include "ai/goal.h"
#include "ai/play_anim_action.h"
#include "ai/script_services.h"
#include "ai/swap_prompt.h"
#include "combat/power_class.h"
#include "human/human.h"

namespace coney::ai {

namespace {

// The held flags under which a human is not free (`Human_IsFree`).
constexpr std::uint32_t kNotFreeHeld = 0x2fefefff;
// No object in the hand (ScriptState::heldObject's nil handle).
constexpr double kEmptyHand = 0.0;

// Whether `brain`'s human is free (`Human_IsFree`): no held flag of 0x2fefefff, and for the state word's 0x7bf9e9f7ff0
// (**Coney stand-in**: Coney keeps no state word) its states: alive, standing, not held in a grab, actions not blocked.
// @orig 0x002282d8 Human_IsFree (unknown)
bool humanFree(const Brain& brain) {
    const human::Human& body = brain.human();
    return (body.heldFlags() & kNotFreeHeld) == 0 && body.alive() && body.state() == human::TargetState::Standing &&
           !body.attached() && !actionsBlocked(body);
}

// The power class the brain's human fights with (`Human_GetClassRecord`).
// @coney-only lookup glue: the original reads the class record in place
const combat::PowerClass& classOf(const Brain& brain) { return brain.human().fighter().victim().powerClass(); }

// Whether a class that flashes is hurt enough to (`Human_ShouldFleeHurt`): its health, as a percentage of its
// maximum, under kFlashHealthPercent.
// @orig 0x00224ec8 Human_ShouldFleeHurt (unknown)
bool shouldFlash(const Brain& brain) {
    if (!classOf(brain).flashesWhenHurt) {
        return false;
    }
    const combat::Health& health = brain.human().fighter().health();
    if (health.maximum() <= 0) {
        return false;
    }
    const float percent = static_cast<float>(health.value()) * 100.0F / static_cast<float>(health.maximum());
    return percent < kFlashHealthPercent;
}

// Clears `brain`'s actions and queues anim `animId`'s clip when his human is free and every action let go.
// @orig 0x0028f6c0 Brain_Think (unknown)
void playWhenFree(Brain& brain, int animId) {
    ScriptServices* services = brain.services();
    if (services == nullptr || !humanFree(brain) || !brain.clearActions()) {
        return;
    }
    // **Coney stand-in**: a play-anim action with the scripted clips' fade and held bit, not PlayAnimIdAction's 0.1 s
    // blends nor GenToIdleAnimAction's held 0x20000 and return to the idle.
    static_cast<void>(brain.queueAction(std::make_unique<PlayAnimAction>(*services, animId, true)));
}

} // namespace

void thinkClassActions(Brain& brain) {
    // Nothing while the human's actions are blocked. **Coney stand-in**: no train is ever near (Coney has none).
    if (actionsBlocked(brain.human())) {
        return;
    }
    const bool fighting = brain.hasFightGoal();
    // 1. In a fight and hurt enough, a flashing class uses a flash, and the think ends there.
    if (fighting && shouldFlash(brain)) {
        playWhenFree(brain, kFlashAnim);
        return;
    }
    // 2. A cop, or a fighter whose class draws (Human_ClassCanFlee: byte +0x41), with nothing in hand draws a weapon;
    // a cop whose class has none does not.
    const bool cop = brain.type() == BrainType::Cop;
    const bool draws = cop || (fighting && classOf(brain).drawsWeapon);
    if (!draws || brain.human().script().heldObject != kEmptyHand) {
        return;
    }
    if (cop && brain.classWeapon() == kNoWeapon) {
        return;
    }
    playWhenFree(brain, kWeaponDrawAnim);
}

void giveHandObject(Brain& brain) {
    // Only with no reaction goal and a goal on the stack.
    const Goal* top = brain.topGoal();
    if (brain.reactionGoal() != nullptr || top == nullptr) {
        return;
    }
    // The four boss goals make their own objects (**Coney stand-in**: not built, so nothing).
    switch (top->type()) {
    case GoalType::BigThrower:
    case GoalType::BigBrawler:
    case GoalType::BigLedgeThrower:
    case GoalType::StationaryThrower:
        return;
    default:
        break;
    }
    ScriptServices* services = brain.services();
    if (services == nullptr) {
        return;
    }
    const std::string_view weapon = brain.classWeapon() == kNoWeapon ? kSwitchblade : brain.classWeapon();
    services->giveHandObject(brain, weapon);
}

void useFlash(Brain& brain) {
    // **Coney stand-in**: an AI has no inventory, HUD panel or screen look, so the flash's spend, the HUD's request and
    // the look are not made. Human_Heal with "full" (no player): a new body, not wounded, at his maximum health.
    human::Human& body = brain.human();
    combat::Health& health = body.fighter().health();
    body.setWounded(false);
    health.set(health.maximum());
    body.heal();
}

} // namespace coney::ai
