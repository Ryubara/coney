// SPDX-License-Identifier: GPL-3.0-or-later
// The gameplay mode's uncuffing (docs/research/crimes.md#arrest, docs/research/crimes.md#uncuffing): a cuffed friendly
// human in player 1's reach is his kind-0 action, triangle starts the mash of L1 and R1, and its outcome frees the
// cuffed human or leaves him cuffed.
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_humans.h"
#include "characters/character_class.h"
#include "combat/combat_tuning.h"
#include "combat/player_combat.h"
#include "combat/stick_games.h"
#include "gamemodes/gameplay_mode.h"
#include "hud/hud.h"
#include "hud/mash_meter.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "scripting/object_bindings.h"
#include "scripting/sound_bindings.h"
#include "warriors/inventory.h"

namespace coney {

namespace {

// The speech commands (docs/references/speech.md): the cuffed human's `arrested`, the freer's `unarrest_reasure`
// and the freed human's `unarrest_thank`.
constexpr std::uint32_t kArrestedCommand = 25;
constexpr std::uint32_t kReassureCommand = 68;
constexpr std::uint32_t kThankCommand = 67;
// A context record's height must be within this of the human's waist, the feet plus 1 m (`0x00417ed0`).
constexpr float kWaistHeight = 1.0F;
constexpr float kHeightReach = 1.5F;
// The kind-0 record's prompt: `GSTRING.HUD` 2 (uncuff).
constexpr std::uint32_t kUncuffPrompt = 2;
// The revive prompt, `GSTRING.HUD` 4, and how near a downed partner must be (3 m, in 3D).
constexpr std::uint32_t kRevivePrompt = 4;
constexpr float kReviveReach = 3.0F;
// The talk prompt's reach (1.5 m, in 3D) and the text of a talkable human with none of its own (`GSTRING.HUD` 9).
constexpr float kTalkReach = 1.5F;
constexpr std::uint32_t kTalkDefaultPrompt = 9;

// Whether `brain` is friendly to `player` for the kind-0 record (`Human_IsFriendly`, `0x00222a90`). **Coney's
// stand-in**: the gangs' friendship (ai::Gangs::friends(): the same gang or kind, or the friend bit).
bool friendlyTo(const ai::Brain& brain, const ai::Brain& player) {
    return ai::Gangs::friends(brain.gang(), player.gang());
}

} // namespace

void GameplayMode::onArrest(ai::Brain& brain, bool arrested) {
    // A release needs nothing here: the kind-0 record goes with the cuffs (cuffedInReach() reads them) and a mash under
    // way on him fails (updateUncuff()).
    const ai::Brain* player = m_scripted ? m_scripted->player() : nullptr;
    // An AI human friendly to player 1 gets the kind-0 record (cuffedInReach() finds it) and says 25 `arrested`.
    if (!arrested || player == nullptr || brain.type() == ai::BrainType::Player || !friendlyTo(brain, *player)) {
        return;
    }
    m_log(std::format("uncuff: human {:.0f} arrested\n", brain.handle()));
    if (m_context.sound != nullptr) {
        static_cast<void>(m_context.sound->sayCommand(
            script::CommandCall{.human = brain.handle(), .command = kArrestedCommand, .interrupt = true}, {}));
    }
}

ai::Brain* GameplayMode::cuffedInReach() const {
    ai::Brain* player = m_scripted ? m_scripted->player() : nullptr;
    if (player == nullptr) {
        return nullptr;
    }
    const anim::Vec3 feet = player->human().position();
    const float reachSquared = m_state.hub.actionDistanceSquared.at(0);
    ai::Brain* best = nullptr;
    float bestSquared = reachSquared;
    ai::Brains& brains = m_scripted->owner();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        ai::Brain& brain = brains.at(i);
        const human::Human& human = brain.human();
        // A friendly AI human who is cuffed and not being freed already.
        if (&brain == player || brain.type() == ai::BrainType::Player || !human.script().arrested ||
            !friendlyTo(brain, *player) || (m_uncuff && m_uncuff->cuffed == brain.handle())) {
            continue;
        }
        // In the kind's reach in the ground plane, and within 1.5 m of the player's waist.
        const anim::Vec3 at = human.position();
        const float squared = ((at.x - feet.x) * (at.x - feet.x)) + ((at.y - feet.y) * (at.y - feet.y));
        if (squared <= bestSquared && std::fabs(at.z - (feet.z + kWaistHeight)) <= kHeightReach) {
            best = &brain;
            bestSquared = squared;
        }
    }
    return best;
}

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
        // Human_IsRevivableBy: friendly, knocked out, not cuffed, flagged revivable (`HuSetRevivable`; every player).
        // **Coney's reading**: the states 0x80000000 and 0x100000000 it also refuses are not modelled.
        if (&brain == player || !human.script().knockedOut || human.script().arrested ||
            !human.hasFlag(human::flag::kRevivable) || !friendlyTo(brain, *player)) {
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

std::string GameplayMode::talkPrompt() const {
    ai::Brain* player = m_scripted ? m_scripted->player() : nullptr;
    if (player == nullptr) {
        return {};
    }
    // The first human within 1.5 m who is talkable and a Warrior (or the co-op partner): his text, else string 9.
    const anim::Vec3 feet = player->human().position();
    const ai::Brains& brains = m_scripted->owner();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const ai::Brain& brain = brains.at(i);
        const human::Human& human = brain.human();
        if (&brain == player || !human.script().talkable ||
            (brain.type() != ai::BrainType::Player && brain.type() != ai::BrainType::Warrior)) {
            continue;
        }
        const anim::Vec3 at = human.position();
        const float squared = ((at.x - feet.x) * (at.x - feet.x)) + ((at.y - feet.y) * (at.y - feet.y)) +
                              ((at.z - feet.z) * (at.z - feet.z));
        if (squared <= kTalkReach * kTalkReach) {
            // The swap prompt's string, else the human's own text, else string 9.
            if (human.script().talkString != 0 && m_context.strings != nullptr) {
                return std::string(m_context.strings->get(human.script().talkString));
            }
            if (!human.script().talkText.empty()) {
                return human.script().talkText;
            }
            return m_context.strings != nullptr ? std::string(m_context.strings->get(kTalkDefaultPrompt))
                                                : std::string{};
        }
    }
    return {};
}

std::string GameplayMode::revivePrompt() const {
    // The player's flash (inventory item 1); a downed AI crew member has no inventory, and a second player's is not
    // kept, so only player 1's counts.
    if (m_context.strings == nullptr || !m_state.player.inventory.has(0, item::kRevive) ||
        revivableInReach() == nullptr) {
        return {};
    }
    return std::string(m_context.strings->get(kRevivePrompt));
}

std::string GameplayMode::uncuffPrompt() const {
    if (m_uncuff || m_context.strings == nullptr || cuffedInReach() == nullptr) {
        return {};
    }
    return std::string(m_context.strings->get(kUncuffPrompt));
}

bool GameplayMode::startUncuff(human::Human& freer) {
    ai::Brain* cuffed = cuffedInReach();
    const HumanCreation* player = m_humans.player(1);
    if (cuffed == nullptr || player == nullptr || m_uncuff) {
        return false;
    }
    // Uncuff_Start without a key: the freer says 68 and turns to the cuffed human over 325; the mash runs with his
    // Warrior class's factor; the cuffed human plays his half. **Coney's readings**: the 0.2 m capsule test between the
    // two and the key path (`Uncuff_WithKey`, `0x00260fd0`) are not built.
    const int mashByte = script::warriorMashByte(&m_recorded, characters::warriorClassOf(player->type));
    const float factor = combat::mashFactor(static_cast<std::uint8_t>(mashByte));
    if (m_context.sound != nullptr) {
        static_cast<void>(m_context.sound->sayCommand(
            script::CommandCall{.human = player->handle, .command = kReassureCommand, .interrupt = true}, {}));
    }
    freer.startUncuff(cuffed->human().position(), factor);
    // The triangle press shows player 1's mash meter: button id 1, the L1-R1 sprite word.
    if (m_context.hud != nullptr) {
        m_context.hud->mashMeter(0).show(1, hud::MashMeter::kUncuffWord);
    }
    cuffed->human().playUncuffReact(freer);
    m_uncuff = Uncuff{.freer = player->handle, .cuffed = cuffed->handle()};
    m_log(std::format("uncuff: freeing human {:.0f}, mash byte {}\n", cuffed->handle(), mashByte));
    return true;
}

void GameplayMode::updateUncuff() {
    ai::Brain* player = m_scripted ? m_scripted->player() : nullptr;
    if (player == nullptr) {
        return;
    }
    // Triangle by a cuffed human: the kind-0 record, tried before the level's own.
    human::Human& freer = player->human();
    if (m_uncuffHooked != &freer) {
        m_uncuffHooked = &freer;
        freer.setFirstContextAction([this](human::Human& human) { return startUncuff(human); });
    }
    if (!m_uncuff) {
        return;
    }
    ai::Brain* cuffed = m_scripted->brain(m_uncuff->cuffed);
    const combat::PlayerCombat& combat = freer.fighter().combat();
    const combat::GameResult result = combat.theftResult();
    const bool running = combat.mode() == combat::CombatMode::Theft;
    // The cuffed human gone, or freed some other way: the mash fails.
    const bool cuffedGone = cuffed == nullptr || !cuffed->human().script().arrested;
    // The meter's fill each update: the meter over its target (HUD_MashMeterUpdate).
    const int target = combat::combatTuning().mashTarget;
    if (m_context.hud != nullptr && combat.mash() && target > 0) {
        m_context.hud->mashMeter(0).setFill(static_cast<float>(combat.mash()->meter()) / static_cast<float>(target));
    }
    if (running && !cuffedGone && freer.uncuffPlaying()) {
        return;
    }
    // Whatever the outcome, the mini-game ends and its meter goes (MiniGame_Abort).
    const Uncuff uncuff = *m_uncuff;
    m_uncuff.reset();
    if (m_context.hud != nullptr) {
        m_context.hud->mashMeter(0).hide();
    }
    if (result == combat::GameResult::Succeeded && !cuffedGone) {
        uncuffSucceeded(freer, *cuffed);
        return;
    }
    // A hit (anything that took the freer's body before an outcome) plays 331; a failure 332. **Coney's reading**:
    // the cuffed human goes back to 320 after a hit too (the original's clips there are not traced).
    const bool hit = result == combat::GameResult::Running && !cuffedGone;
    m_log(std::format("uncuff: freeing human {:.0f} {}\n", uncuff.cuffed, hit ? "cut short" : "failed"));
    freer.endUncuff(hit);
    if (cuffed != nullptr && cuffed->human().script().arrested) {
        cuffed->human().endUncuffReact(freer, false);
    }
}

// Uncuff_MashSuccess: the cuffed human thanks the freer and is released (and brought round when knocked out and
// revivable), each playing his end.
// @orig 0x002606e8 Uncuff_MashSuccess (unknown)
void GameplayMode::uncuffSucceeded(human::Human& freer, ai::Brain& cuffed) {
    m_log(std::format("uncuff: freed human {:.0f}\n", cuffed.handle()));
    if (m_context.sound != nullptr) {
        static_cast<void>(m_context.sound->sayCommand(
            script::CommandCall{.human = cuffed.handle(), .command = kThankCommand, .interrupt = true}, {}));
    }
    freer.endUncuff(false);
    human::Human& freed = cuffed.human();
    m_scripted->humanHost().setArrested(cuffed.handle(), false);
    if (freed.script().knockedOut && freed.hasFlag(human::flag::kRevivable)) {
        freed.revive();
    }
    freed.endUncuffReact(freer, true);
}

} // namespace coney
