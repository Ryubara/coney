// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fidget_action.h"

#include <optional>
#include <string_view>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/script_services.h"
#include "human/human.h"

namespace coney::ai {

bool freeToFidget(const Brain& brain) {
    const human::Human& body = brain.human();
    return body.heldFlags() == 0 && body.alive() && body.state() == human::TargetState::Standing && !body.attached() &&
           body.fighter().animSet() == 0;
}

std::optional<int> classRole(const Brain& brain) {
    const Gang* gang = brain.gang();
    return gang != nullptr ? gang->owner().roleOf(brain) : std::nullopt;
}

namespace {

// Whether `brain`'s class may never fidget: a dog, or a class of role 6.
// @orig 0x002f9f78 FidgetAction_Start (unknown)
bool neverFidgets(const Brain& brain) {
    return brain.characterClass() == kDogClass || classRole(brain) == kNoFidgetRole;
}

} // namespace

ActionStatus FidgetAction::start(Brain& brain) {
    ScriptServices* services = brain.services();
    if (services == nullptr || brain.type() == BrainType::Cop || neverFidgets(brain)) {
        return ActionStatus::Done;
    }
    // Free to fidget, unless a knife holder's own fidget is asked for.
    const bool knife = brain.human().fighter().animSet() == kKnifeAnimSet;
    if ((m_idle || !knife) && !freeToFidget(brain)) {
        return ActionStatus::Done;
    }
    if (m_line != 0) {
        services->say(brain, m_line, false, 0.0);
    }
    if (!m_idle && brain.type() == BrainType::Dealer) {
        return ActionStatus::Done;
    }
    // The clip: the idle fidgets, a knife holder's, or the anim; for the fight fidgets a random slot of the gang's
    // group, which gives one of its clips when the slot is filled and else the character's own.
    const int anim = m_idle ? kIdleFidgetAnim : (knife ? kKnifeFidgetAnim : m_animId);
    std::optional<std::uint32_t> held;
    if (Gang* gang = brain.gang(); anim == kFightFidgetAnim && gang != nullptr) {
        if (const std::string_view clip = gang->randomClip(kCombatFidgetClips); !clip.empty()) {
            held = services->playNamedClip(brain, anim, clip, kFidgetFade);
        }
    }
    if (!held) {
        held = services->playClip(brain, anim);
    }
    if (!held) {
        return ActionStatus::Done;
    }
    brain.stopMove();
    m_heldFlags = *held;
    m_playing = anim;
    return ActionStatus::Running;
}

ActionStatus FidgetAction::update(Brain& brain) {
    return (brain.human().animator().flags() & m_heldFlags) == 0 ? ActionStatus::Done : ActionStatus::Running;
}

bool FidgetAction::abort(Brain& brain) {
    if (m_heldFlags != 0) {
        brain.human().stopScripted(static_cast<std::uint32_t>(m_playing), m_heldFlags);
    }
    m_heldFlags = 0;
    return true;
}

} // namespace coney::ai
