// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/swap_prompt.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ai/brain.h"
#include "combat/reactions.h"
#include "combat/stick.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/script_state.h"
#include "world_objects/object_services.h"

namespace coney::ai {

namespace {

// Whether the human holds an object. **Coney's reading**: the object in its hands (`+0x338`).
bool holds(const human::Human& human) { return human.script().heldObject != world_objects::kNoObject; }

// The nearest player brain within the swap's reach of `brain`, or null.
const Brain* nearestPlayer(const Brain& brain) {
    const Brain* nearest = nullptr;
    float best = kSwapReach;
    for (const std::unique_ptr<Brain>& other : *brain.peers()) {
        if (other.get() == &brain || other->type() != BrainType::Player || other->dead()) {
            continue;
        }
        if (const float distance = brain.distanceTo(*other); distance <= best) {
            best = distance;
            nearest = other.get();
        }
    }
    return nearest;
}

// The prompt's string when the swap applies between `warrior` and `player`; 0 when it does not.
std::uint32_t swapString(const Brain& warrior, const Brain& player) {
    const human::Human& self = warrior.human();
    const human::Human& them = player.human();
    // The player: below a jog.
    if (them.gait() >= human::Gait::Jog) {
        return 0;
    }
    // One of the two holds an object.
    const bool heHolds = holds(self);
    const bool playerHolds = holds(them);
    if (!heHolds && !playerHolds) {
        return 0;
    }
    // The Warrior: standing, in the player's gang, nobody attacking him, in front of the player.
    const bool attacked = std::ranges::any_of(warrior.attackSlots(), [](const Brain* slot) { return slot != nullptr; });
    if (self.gait() != human::Gait::Standing || warrior.gang() == nullptr || warrior.gang() != player.gang() ||
        attacked || combat::victimSide(them.position(), them.heading(), self.position()) != combat::Side::Front) {
        return 0;
    }
    if (heHolds && playerHolds) {
        return kSwapBothHold;
    }
    return heHolds ? kSwapWarriorHolds : kSwapPlayerHolds;
}

} // namespace

void thinkSwapPrompt(Brain& brain) {
    human::ScriptState& script = brain.human().script();
    const Brain* player = brain.peers() != nullptr ? nearestPlayer(brain) : nullptr;
    const std::uint32_t id = player != nullptr ? swapString(brain, *player) : 0;
    // `+0x1b2` follows the test every think; the prompt's text is written only when it holds.
    script.talkable = id != 0;
    if (id != 0) {
        script.talkString = id;
        script.talkText.clear();
    }
}

SwapOutcome swapHeldObjects(human::ScriptState& receiver, human::ScriptState& presser,
                            const std::function<bool(std::string_view typeName)>& placeable) {
    // Both hands are emptied first (each side drops what it holds).
    const double fromPresser = std::exchange(presser.heldObject, world_objects::kNoObject);
    const std::string presserName = std::exchange(presser.heldObjectName, std::string());
    const double fromReceiver = std::exchange(receiver.heldObject, world_objects::kNoObject);
    const std::string receiverName = std::exchange(receiver.heldObjectName, std::string());
    SwapOutcome outcome;
    // Each dropped object goes into the other hand when its type has a placement, else it stays on the ground.
    const auto give = [&](double object, const std::string& name, human::ScriptState& to, double& given) {
        if (object == world_objects::kNoObject) {
            return;
        }
        if (placeable && !placeable(name)) {
            outcome.dropped.push_back(object);
            return;
        }
        to.heldObject = object;
        to.heldObjectName = name;
        given = object;
    };
    give(fromPresser, presserName, receiver, outcome.toReceiver);
    give(fromReceiver, receiverName, presser, outcome.toPresser);
    return outcome;
}

} // namespace coney::ai
