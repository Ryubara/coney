// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/taunt_action.h"

#include <optional>
#include <string_view>

#include "ai/brain.h"
#include "ai/fidget_action.h"
#include "ai/gangs.h"
#include "ai/script_services.h"
#include "human/human.h"

namespace coney::ai {

ActionStatus TauntAction::start(Brain& brain) {
    ScriptServices* services = brain.services();
    const std::optional<int> role = classRole(brain);
    if (services == nullptr || brain.type() == BrainType::Cop || role == kNoFidgetRole) {
        return ActionStatus::Done;
    }
    // The clip: the gang's for the kind's anim at a random variant, else the character's own.
    if (m_kind >= 0 && m_kind < kNoClipTaunt) {
        if (!freeToFidget(brain) && role != kAlwaysTauntRole) {
            return ActionStatus::Done;
        }
        const int anim = tauntAnim(m_kind);
        std::optional<std::uint32_t> held;
        if (Gang* gang = brain.gang(); gang != nullptr) {
            if (const std::string_view clip = gang->randomClip(anim); !clip.empty()) {
                held = services->playNamedClip(brain, anim, clip, kTauntFade);
            }
        }
        if (!held) {
            held = services->playClip(brain, anim);
        }
        if (held) {
            brain.stopMove();
            m_heldFlags = *held;
            m_playing = anim;
        }
    }
    if (m_line != 0) {
        services->say(brain, m_line, false, 0.0);
    }
    // Kinds 0-2 and the clipless kind do not wait for their clip.
    m_wait = m_kind > 2 && m_kind != kNoClipTaunt;
    return ActionStatus::Running;
}

ActionStatus TauntAction::update(Brain& brain) {
    if (m_wait && (brain.human().animator().flags() & m_heldFlags) != 0) {
        return ActionStatus::Running;
    }
    m_ended = true;
    return ActionStatus::Done;
}

bool TauntAction::abort(Brain& brain) {
    // A normal end (the original's mode 0) accepts and leaves the clip to play out.
    if (m_ended) {
        return true;
    }
    // Else the taunt stops (the idle again while its clip still plays), and a taunt that waits for its clip refuses
    // while the clip still holds its flags.
    if (m_heldFlags != 0) {
        brain.human().stopScripted(static_cast<std::uint32_t>(m_playing), m_heldFlags);
    }
    return !(m_wait && m_heldFlags != 0 && (brain.human().animator().flags() & m_heldFlags) != 0);
}

} // namespace coney::ai
