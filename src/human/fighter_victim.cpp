// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/fighter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "animation/anim_task.h"
#include "combat/anim_ids.h"
#include "combat/being_hit.h"
#include "combat/combat_tuning.h"
#include "combat/rage_awards.h"
#include "human/fighter_clips.h"

// The fighter's victim side: the player hit (a duck, a block, the health floor, the hit armour, the reaction, the stun,
// the ground and the mash), warned by an attacker's clip, and held in another human's grab (the counter at the
// catch, the struggle, the strike back, the escape and the reversal). Nothing in Coney attacks the player yet: these
// are the entry points a future AI attacker and the tests use (Fighter::takeHit(), warn(), catchInGrab()).
// Research: docs/research/combat.md#damage, docs/research/combat.md#block, docs/research/combat.md#grabbed,
// docs/research/combat.md#being-hit-runtime

namespace coney::human {

namespace {

namespace id = combat::anim_id;

// The share of its power a grabber loses when a third human hits it (`0x00510274`).
constexpr float kThirdHitPower = 0.6F;

} // namespace

VictimFrame Fighter::frame(const FighterInput& input) const {
    // The player has human flag 0x400: combo hits keep their strength on it.
    return VictimFrame{.position = input.position, .heading = input.heading, .hurt = hurt(), .flag400 = true};
}

bool Fighter::helpless(const HumanAnimator& animator) const {
    if (m_health.depleted() || m_victim.grounded() || m_victim.stunned() || m_victim.stunExitPending()) {
        return true;
    }
    // A reaction, a rise, the stun's exit or an escape still playing.
    return m_reacting && animator.drivingClipPlaying();
}

void Fighter::takeNotice(const FighterInput& input, HumanAnimator& animator) {
    if (!m_notice.has_value()) {
        return;
    }
    const AttackNotice notice = *m_notice;
    m_notice.reset();
    // Only a blocking player answers a warning.
    if (!m_combat.blocking() || grabbed() || helpless(animator)) {
        return;
    }
    if (notice.warning == combat::AttackWarning::Duck) {
        // The duck (record +0x14 = 0xd): 616, then the block again; the attack passes over it. Its attacker is kept
        // for the duck's counter.
        m_duckAttacker = notice.attacker;
        m_counterAsked = false;
        animator.playCombat(clips::one(clips::clipOf(combat::kBlockDodge)), clips::kBlockSustain, AnimState::Hold,
                            kCombatFade, clips::kDuckHolds);
        return;
    }
    // The early block reaction (+0x14 = 0xc), by the warned attack's code and side.
    const IncomingHit preview{.attackAnim = notice.attackAnim, .code = notice.code, .attacker = notice.attacker};
    const combat::BlockResult block = combat::blockHit(Victim::reactionInput(preview, frame(input)));
    if (block.holds) {
        animator.playCombat(clips::one(clips::clipOf(block.reaction)), clips::kBlockSustain, AnimState::Hold);
    }
}

void Fighter::takePending(const FighterInput& input, HumanAnimator& animator) {
    if (!m_victim.pending()) {
        return;
    }
    const combat::CombatTuning& tuning = combat::combatTuning();
    const IncomingHit hit = m_victim.takePending();
    // 1. A duck lets the attack pass over the body.
    if (animator.animId() == clips::clipOf(combat::kBlockDodge) && animator.drivingClipPlaying()) {
        ++m_hitsDucked;
        return;
    }
    const VictimFrame here = frame(input);
    // 2. A held block cancels the hit with its block reaction, unless the hit breaks it.
    if (m_combat.blocking() && hit.react) {
        const combat::BlockResult block = combat::blockHit(Victim::reactionInput(hit, here));
        if (block.holds) {
            ++m_hitsBlocked;
            animator.playCombat(clips::one(clips::clipOf(block.reaction)), clips::kBlockSustain, AnimState::Hold);
            return;
        }
        m_combat.interrupt();
    }
    // 3. The damage, the attacker's as it is, held at the health floor.
    const int damage = combat::flooredDamage(m_health.value(), m_health.maximum(), hit.damage, tuning.healthFloor);
    m_health.apply(damage);
    ++m_hitsTaken;
    // 4. A grabber hit by a third human loses power.
    const combat::CombatMode mode = m_combat.mode();
    if (mode == combat::CombatMode::Grabbing || mode == combat::CombatMode::Tackling) {
        m_combat.power().spend(kThirdHitPower);
    }
    if (m_health.depleted()) {
        // Out of health: whatever it held goes, and the dying reaction plays.
        if (m_held != nullptr) {
            releaseHold(animator, false);
        }
        m_combat.interrupt();
        m_combat.release();
        m_grabbed.reset();
        m_victim.die(hit, here, animator);
        m_reacting = true;
        return;
    }
    // 5. **Coney's choice**: held or holding someone, the hit only takes health (the reactions by those states,
    // `0x002688d0` and `0x00268ea8`, are not traced).
    if (!hit.react || grabbed() || mode != combat::CombatMode::Free) {
        return;
    }
    // On the ground a strike gets the ground's reaction.
    if (m_victim.grounded()) {
        animator.playCombat(clips::one(clips::kGroundedStrikeReact), clips::kGroundedIdle, AnimState::Hold);
        return;
    }
    // 6. The hit armour: winding up or in the chain window, the player's attack goes on with no reaction.
    if (combat::hitArmourHolds(animator.flags(), hit.attackAnim, hit.ignoresArmour)) {
        ++m_hitsArmoured;
        return;
    }
    // 7. The reaction; the attack playing is lost.
    m_combat.interrupt();
    m_slideUpdates = 0;
    m_victim.react(hit, here, animator, kAnimFightIdle, input.nowMs);
    m_reacting = true;
}

bool Fighter::stepVictim(const FighterInput& input, HumanAnimator& animator) {
    // Every command made while lying down cuts the ground time (a press during the knockdown's reaction does not).
    if (m_victim.grounded() && input.command != combat::command::kNone) {
        m_victim.mash(animator, input.nowMs);
    }
    m_victim.step(animator, kAnimFightIdle, input.nowMs, !m_health.depleted());
    const bool stillDown =
        m_health.depleted() || m_victim.grounded() || m_victim.stunned() || m_victim.stunExitPending();
    if (m_reacting && !stillDown && !animator.drivingClipPlaying()) {
        m_reacting = false;
    }
    return helpless(animator);
}

void Fighter::updateGrabber(const combat::GrabberState& grabber) {
    if (m_grabbed.has_value()) {
        m_grabbed->grabber = grabber;
    }
}

void Fighter::startGrabbed(const FighterInput& input, HumanAnimator& animator) {
    // Called only with a catch waiting.
    if (!m_catch.has_value()) {
        return;
    }
    const combat::CombatTuning& tuning = combat::combatTuning();
    const GrabCatch grab = *m_catch;
    m_catch.reset();
    if (m_health.depleted()) {
        return;
    }
    // R1 on the update the intro ends counters: 76, the grabber 77, as a paired move that needs and spends power.
    const bool raging = m_combat.rage().raging();
    const bool powered = raging || m_combat.power().fraction() > tuning.powerEndurance;
    if (combat::counterAtCatch(input.command, tuning.grabCounters) && powered) {
        if (!raging) {
            m_combat.power().spend(tuning.powerEndurance);
        }
        m_combat.interrupt();
        animator.playCombat(clips::one(clips::clipOf(combat::kGrabFrontCounter)), kAnimFightIdle, AnimState::Attack);
        m_report.countered = true;
        m_report.grabberClip = combat::kGrabFrontCounter + 1;
        // **Coney's choice**: the grabber loses the power endurance's share of its maximum (100 of 400 at runtime).
        m_report.grabberPowerCost =
            static_cast<int>(std::lround(tuning.powerEndurance * static_cast<float>(grab.grabber.powerMax)));
        m_report.grabberDamage =
            m_ranges != nullptr ? m_ranges->damage(static_cast<std::size_t>(combat::kGrabFrontCounter)) : 0;
        m_report.grabberStunned = true;
        combat::awardHitRage(m_combat.rage(), combat::kGrabFrontCounter, false, tuning, input.nowMs, m_repeat);
        m_repeat.note(combat::kGrabFrontCounter, input.nowMs);
        m_reacting = true;
        return;
    }
    // Held: the grabber's set's reaction (73 from the front, 75 from the rear), then the player's held loop.
    m_combat.interrupt();
    m_grabbed = grab;
    const std::uint32_t react = grab.fromRear ? clips::kGrabReactFromRear : clips::kGrabReactFromFront;
    const std::uint32_t held = grab.fromRear ? clips::kGrabRearHeld : clips::kGrabHeld;
    if (grab.grabberAnims != nullptr) {
        animator.playPaired(clips::one(react), *grab.grabberAnims, held, AnimState::Hold);
    } else {
        animator.playCombat(clips::one(react), held, AnimState::Hold, clips::kPairFade);
    }
    m_justCaught = true;
}

void Fighter::updateGrabbed(const FighterInput& input, HumanAnimator& animator) {
    if (m_justCaught) {
        // The catch's own update only starts the hold.
        m_justCaught = false;
        return;
    }
    // Called only while held.
    if (!m_grabbed.has_value()) {
        return;
    }
    GrabCatch& grab = *m_grabbed;
    // The grabber's power ran out: the grab ends, the player escaping when the grabber is hurt.
    if (grab.grabber.power <= 0) {
        if (grab.grabber.hurt) {
            escapeGrab(grab.fromRear ? combat::kGrabEscapeRear : combat::kGrabEscapeFront, animator);
        } else {
            m_report.grabberClip = id::kGrabLetGo;
            releaseFromGrab(animator);
        }
        m_report.ended = true;
        return;
    }
    combat::GrabbedInput in;
    in.command = input.command;
    in.fromRear = grab.fromRear;
    in.hurt = hurt();
    in.ownPowerFraction = m_combat.power().fraction();
    in.ownStruggleDivisor = m_victim.powerClass().struggleDivisor;
    in.movePlaying = animator.drivingClipPlaying();
    in.grabber = grab.grabber;
    const combat::GrabbedOutcome outcome = combat::updateGrabbed(in, m_grabbedRandom);
    m_report.action = outcome.action;
    m_report.grabberPowerCost = outcome.grabberPowerCost;
    // Until the grabber reports again, its meter is taken to have paid.
    grab.grabber.power = std::max(0, grab.grabber.power - outcome.grabberPowerCost);
    if (outcome.action == combat::GrabbedAction::None) {
        return;
    }
    const std::uint32_t clip = clips::clipOf(outcome.animId);
    const int damage = m_ranges != nullptr ? m_ranges->damage(static_cast<std::size_t>(outcome.animId)) : 0;
    m_report.grabberClip = outcome.animId + 1;
    switch (outcome.action) {
    case combat::GrabbedAction::Struggle:
    case combat::GrabbedAction::StrikeBack:
        // The move, then back to the held loop; the grabber takes the move's damage.
        animator.playCombat(clips::one(clip), grab.fromRear ? clips::kGrabRearHeld : clips::kGrabHeld, AnimState::Hold,
                            clips::kPairFade);
        m_report.grabberDamage = damage;
        if (outcome.action == combat::GrabbedAction::StrikeBack) {
            combat::awardHitRage(m_combat.rage(), outcome.animId, false, combat::combatTuning(), input.nowMs, m_repeat);
            m_repeat.note(outcome.animId, input.nowMs);
        }
        break;
    case combat::GrabbedAction::Escape:
        escapeGrab(outcome.animId, animator);
        m_report.ended = true;
        break;
    case combat::GrabbedAction::Reversal:
        // The reversal, then the player holds the grabber from the rear (84; the grabber 85), draining its power.
        animator.playCombat(clips::one(clip), clips::kGrabRearHold, AnimState::Hold, clips::kPairFade);
        m_grabbed.reset();
        m_combat.startHolding();
        m_rear = true;
        m_report.ended = true;
        break;
    case combat::GrabbedAction::None:
        break;
    }
}

void Fighter::escapeGrab(int clip, HumanAnimator& animator) {
    // The escape knocks the grabber down and stuns it; the escapee takes the clip's own damage, as at runtime.
    animator.playCombat(clips::one(clips::clipOf(clip)), kAnimFightIdle, AnimState::Attack, clips::kPairFade);
    if (m_ranges != nullptr) {
        m_health.apply(m_ranges->damage(static_cast<std::size_t>(clip)));
    }
    m_report.grabberClip = clip + 1;
    m_report.grabberKnockedDown = true;
    m_report.grabberStunned = true;
    m_grabbed.reset();
    m_reacting = true;
}

void Fighter::releaseFromGrab(HumanAnimator& animator) {
    if (!m_grabbed.has_value()) {
        return;
    }
    m_grabbed.reset();
    animator.playCombat(clips::one(clips::kGrabLetGoReact), kAnimFightIdle, AnimState::Attack);
    m_reacting = true;
}

bool Fighter::duckCounter(const FighterInput& input, HumanAnimator& animator) {
    const combat::CombatTuning& tuning = combat::combatTuning();
    const std::uint32_t duck = clips::clipOf(combat::kBlockDodge);
    const bool ducking = animator.animId() == duck && animator.drivingClipPlaying();
    // 1. Asked for on an earlier update: the counter by the side its target stands on, once one is in reach (the
    // attacker that made the player duck, else the nearest within 1.25 × the counter's reach).
    if (m_counterAsked) {
        const combat::AnimRange* range =
            m_ranges != nullptr ? m_ranges->find(static_cast<std::size_t>(combat::kDuckCounterFront)) : nullptr;
        const float reach =
            combat::kDuckCounterReachScale * (range != nullptr && range->reach > 0.0F ? range->reach : 1.0F);
        std::optional<anim::Vec3> target;
        if (std::hypot(m_duckAttacker.x - input.position.x, m_duckAttacker.y - input.position.y) <= reach) {
            target = m_duckAttacker;
        } else if (const TargetHuman* found = pickTarget(input, reach); found != nullptr) {
            target = found->position();
        }
        if (target.has_value()) {
            const int counter = combat::duckCounterClip(combat::victimSide(input.position, input.heading, *target));
            m_combat.startCounter(counter, tuning);
            animator.playCombat(clips::one(clips::clipOf(counter)), kAnimFightIdle, AnimState::Attack, kCombatFade,
                                clips::kCounterHolds);
            m_counterAsked = false;
            ++m_duckCounters;
            return true;
        }
        // **Coney's choice**: without a target the request lasts as long as the duck.
        if (!ducking) {
            m_counterAsked = false;
        }
    }
    // 2. The duck's window: its own 0x25 events fire this update while square or cross is pressed or held.
    const anim::AnimTask* top = animator.tasks().top();
    const anim::AnimClip* clip = animator.anims().clip(duck);
    if (!ducking || m_clipSeen != duck || top == nullptr || clip == nullptr) {
        return false;
    }
    if (combat::eventBetween(*clip, combat::kDuckCounterEvent, m_clipTimeSeen, top->time()) &&
        combat::asksDuckCounter(input.command)) {
        m_counterAsked = true;
        return true;
    }
    return false;
}

} // namespace coney::human
