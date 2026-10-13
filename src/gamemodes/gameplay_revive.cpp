// SPDX-License-Identifier: GPL-3.0-or-later
// The gameplay mode's revive (docs/research/combat-rage.md#revive, docs/research/hud-messages.md#action-prompts): the
// prompt beside a knocked-out partner while player 1 holds a flash, d-pad left (command 0x27) to revive him, the
// reviver's 351 and 353, and at 351's end the flash spent, the heal and the revived human's 354.
#include <cstddef>
#include <format>
#include <string>
#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_humans.h"
#include "ai/swap_prompt.h"
#include "combat/commands.h"
#include "combat/revive.h"
#include "gamemodes/gameplay_mode.h"
#include "hud/hud.h"
#include "human/human.h"
#include "human/human_contacts.h"
#include "human/human_flags.h"
#include "human/way_clear.h"
#include "scripting/sound_bindings.h"
#include "warriors/inventory.h"

namespace coney {

namespace {

// The revive prompt, `GSTRING.HUD` 4, and how near a downed partner must be (3 m, in 3D).
constexpr std::uint32_t kRevivePrompt = 4;
constexpr float kReviveReach = 3.0F;
// Player 1's panel inventory: his flashes pay for a revive.
constexpr int kPlayerOnePanel = 0;

// Knocked out (state `0x40000`, `Human_IsKnockedOut`): his health gone in a fight, or a script's `HuSetConscious`.
bool knockedOut(const human::Human& human) { return human.fighter().health().depleted() || human.script().knockedOut; }

} // namespace

ai::Brain* GameplayMode::revivableInReach() const {
    ai::Brain* player = m_scripted ? m_scripted->player() : nullptr;
    if (player == nullptr) {
        return nullptr;
    }
    const anim::Vec3 feet = player->human().position();
    ai::Brain* best = nullptr;
    float bestSquared = kReviveReach * kReviveReach;
    ai::Brains& brains = m_scripted->owner();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        ai::Brain& brain = brains.at(i);
        const human::Human& human = brain.human();
        // Human_IsRevivableBy: friendly (**Coney's stand-in**: the gangs' friendship), knocked out, not cuffed, flagged
        // revivable (`HuSetRevivable`; every player). **Coney's reading**: the states 0x80000000 and 0x100000000 it
        // also refuses are not modelled.
        if (&brain == player || !knockedOut(human) || human.script().arrested ||
            !human.hasFlag(human::flag::kRevivable) || !ai::Gangs::friends(brain.gang(), player->gang())) {
            continue;
        }
        const anim::Vec3 at = human.position();
        const float squared = ((at.x - feet.x) * (at.x - feet.x)) + ((at.y - feet.y) * (at.y - feet.y)) +
                              ((at.z - feet.z) * (at.z - feet.z));
        if (squared < bestSquared && player->hasLineOfSight(brain)) {
            best = &brain;
            bestSquared = squared;
        }
    }
    return best;
}

std::string GameplayMode::revivePrompt() const {
    // The player's flash (inventory item 1); a downed AI crew member has no inventory, and a second player's is not
    // kept, so only player 1's counts.
    if (m_context.strings == nullptr || !m_state.player.inventory.has(kPlayerOnePanel, item::kRevive) ||
        revivableInReach() == nullptr) {
        return {};
    }
    return std::string(m_context.strings->get(kRevivePrompt));
}

void GameplayMode::updateRevive() {
    ai::Brain* player = m_scripted ? m_scripted->player() : nullptr;
    if (player == nullptr) {
        m_revive.reset();
        return;
    }
    // A revive under way waits for the end of the reviver's 351, however it ends: 353 next, or anything else that cut
    // it short (a hit, a scene). Revive_Finish is the clip task's callback, which its clean-up runs on the clip's end,
    // a cut-off and the task's release alike, so the flash is spent and the downed human healed either way, once.
    if (m_revive) {
        ai::Brain* reviver = m_scripted->brain(m_revive->reviver);
        if (reviver != nullptr && reviver->human().reviveBeginPlaying()) {
            return;
        }
        ai::Brain* revived = m_scripted->brain(m_revive->downed);
        const double downed = m_revive->downed;
        m_revive.reset();
        if (reviver == nullptr || revived == nullptr) {
            m_log(std::format("revive: human {:.0f} or his reviver gone\n", downed));
            return;
        }
        if (!reviver->human().reviveEndPlaying()) {
            m_log(std::format("revive: cut short, human {:.0f} revived all the same\n", downed));
        }
        finishRevive(*reviver, *revived);
        return;
    }
    if (player->human().record().command == combat::command::kDpadLeft) {
        static_cast<void>(tryRevive(*player));
    }
}

bool GameplayMode::wayClearTo(const human::Human& from, const human::Human& to) const {
    // The 0.2 m capsule as tall as `from`'s, swept to `to` past every other human in the world.
    std::vector<human::UprightCapsule> others;
    const ai::Brains& brains = m_scripted->owner();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const human::Human& human = brains.at(i).human();
        if (&human != &from && &human != &to && !human.outOfWorld()) {
            others.push_back(human.uprightCapsule());
        }
    }
    return human::wayClear(brains.collision(), from.position(), from.uprightCapsule().top, to.uprightCapsule(), others);
}

bool GameplayMode::tryRevive(ai::Brain& reviver) {
    human::Human& body = reviver.human();
    ai::Brain* downed = revivableInReach();
    // Player_TryRevive's tests. **Coney's readings**: "blocked" is the swap prompt's reading of the state mask plus
    // anything that keeps him off his feet (down, held, in the air), which covers the way test's "grabbed"; a downed
    // AI crew member has no inventory and Coney keeps no second player, so only player 1's flash counts.
    const combat::ReviveFacts facts{.reviverHeld = body.animator().flags() != 0,
                                    .reviverBlocked = ai::actionsBlocked(body) ||
                                                      body.state() != human::TargetState::Standing || body.airborne(),
                                    .found = downed != nullptr,
                                    .downedHeld = downed != nullptr && downed->human().animator().flags() != 0,
                                    .wayClear = downed != nullptr && wayClearTo(body, downed->human()),
                                    .downedIsPlayer = downed != nullptr && downed->type() == ai::BrainType::Player,
                                    .reviverIsWarrior = reviver.type() == ai::BrainType::Warrior,
                                    .reviverHasFlash = m_state.player.inventory.has(kPlayerOnePanel, item::kRevive),
                                    .downedHasFlash = false};
    if (downed == nullptr || !combat::mayRevive(facts)) {
        return false;
    }
    // Revive_Start: the reviver says 76 `revive_reasure` (interrupting) and plays 351, 353 and back to his idle,
    // turned to the downed human's head (the led steer's point). **Coney's readings**: the pair ids, the push spheres
    // turned off and Human_SetPartner have no counterpart; the chain's own clips keep them apart.
    if (m_context.sound != nullptr) {
        static_cast<void>(m_context.sound->sayCommand(script::CommandCall{.human = reviver.handle(),
                                                                          .command = combat::kReviveReassureCommand,
                                                                          .interrupt = true},
                                                      {}));
    }
    body.startRevive(downed->human());
    m_revive = Revive{.reviver = reviver.handle(), .downed = downed->handle()};
    m_log(std::format("revive: reviving human {:.0f}\n", downed->handle()));
    return true;
}

void GameplayMode::finishRevive(ai::Brain& reviver, ai::Brain& revived) {
    // One flash spent: the downed human's when he has one (only a co-op partner can), else the reviver's.
    if (combat::reviveFlashFrom(false) == combat::ReviveFlashFrom::Reviver) {
        m_state.player.inventory.give(kPlayerOnePanel, item::kRevive, -1);
    }
    // Interface cue 23, the flash's sound; the revived says 75 `revive_thank`.
    if (m_context.hud != nullptr) {
        m_context.hud->services().sound.playCue(combat::kReviveSoundCue);
    }
    if (m_context.sound != nullptr) {
        static_cast<void>(m_context.sound->sayCommand(
            script::CommandCall{.human = revived.handle(), .command = combat::kReviveThankCommand, .interrupt = true},
            {}));
    }
    // Human_Heal in full wakes him: a script's knock-out ends with his brain back on (HuSetConscious); a fight's ends
    // as his health returns (Brains wakes the brain when it sees it).
    if (revived.human().script().knockedOut) {
        m_scripted->humanHost().setConscious(revived.handle(), true);
    }
    revived.human().playRevived(reviver.human());
    m_log(std::format("revive: revived human {:.0f}, health {}\n", revived.handle(),
                      revived.human().fighter().health().value()));
}

} // namespace coney
