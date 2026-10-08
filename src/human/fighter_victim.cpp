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
#include "combat/reactions.h"
#include "human/fighter_clips.h"

// The fighter's victim side: the player hit (a duck, a block, the health floor, the hit armour, the reaction, the stun,
// the ground and the mash), warned by an attacker's clip, and held in another human's grab (the counter at the
// catch, the struggle, the strike back, the escape and the reversal), or held by a grab that drives it (enterHold(), a
// human with a brain the player grabs or tackles). Another human's strikes and warnings reach it
// through Human (a Combatant); the tests use the same entry points (Fighter::takeHit(), warn(), catchInGrab()).
// Research: docs/research/combat.md#damage, docs/research/combat.md#block, docs/research/combat.md#grabbed,
// docs/research/combat.md#being-hit-runtime

namespace coney::human {

namespace {

namespace id = combat::anim_id;

// The share of its power a grabber loses when a third human hits it (`0x00510274`).
constexpr float kThirdHitPower = 0.6F;

} // namespace

VictimFrame Fighter::frame(const FighterInput& input) const {
    // A player has human flag 0x400: combo hits keep their strength on it.
    return VictimFrame{.position = input.position,
                       .heading = input.heading,
                       .hurt = hurt(),
                       .flag400 = hasFlag(flag::kComboStrength),
                       .flag200 = hasFlag(flag::kReducedReact),
                       .flag80 = hasFlag(flag::kUngroundable),
                       .unstunnable = hasFlag(flag::kUnstunnable)};
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
    // The hit sound's struck material, before the hit changes anything (Hit_ResolveBlock): down or dead, then a block
    // or a duck, then a boss-class body, then the shape struck.
    const bool down = m_victim.grounded();
    const std::uint32_t struck = [&] {
        if (down) {
            return material::kDead;
        }
        if (m_combat.blocking()) {
            return material::kBlock;
        }
        // **Coney's stand-in** for the shape struck (Coney's strikes do not test bones): a high hit strikes the head.
        constexpr int kHighHit = 2;
        return m_bossClass || combat::decodeHitCode(hit.code).height == kHighHit ? material::kHead : material::kTorso;
    }();
    // 1. A duck lets the attack pass over the body.
    if (animator.animId() == clips::clipOf(combat::kBlockDodge) && animator.drivingClipPlaying()) {
        reportHitSound(hit, material::kBlock);
        ++m_hitsDucked;
        return;
    }
    reportHitSound(hit, struck);
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
    // 3. The damage, the attacker's as it is: none for a god (flag 0x10); held at a demi-god's health floor (flag
    // 0x20000000000), which, reached, makes it a god (0x00265f70). **Coney's reading**: god mode drops the damage only;
    // the reaction still plays (where the original tests 0x10 is not traced).
    int damage = hasFlag(flag::kGod) ? 0 : hit.damage;
    if (damage > 0 && hasFlag(flag::kDemiGod)) {
        const int floored = combat::flooredDamage(m_health.value(), m_health.maximum(), damage, tuning.healthFloor);
        if (floored < damage) {
            setFlag(flag::kGod, true);
        }
        damage = floored;
    }
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
        m_holdState.reset();
        m_holdAttached = false;
        m_victim.die(hit, here, animator);
        m_reacting = true;
        return;
    }
    // A hit ends a stereo theft (`Human_ApplyPendingDamage` sends a human in a mini-game other than lock picking to
    // `0x00268c50`, which ends it, docs/research/crimes.md#uncuffing); the reaction then plays as in free play.
    // **Coney's reading**: the clip `0x00268c50` plays for the theft is not traced, so the hit's own reaction plays.
    if (hit.react && m_combat.theft().has_value()) {
        m_combat.abortTheft();
    }
    const combat::CombatMode reactMode = m_combat.mode();
    // 5. **Coney's choice**: held or holding someone, the hit only takes health (the reactions by those states,
    // `0x002688d0` and `0x00268ea8`, are not traced).
    if (!hit.react || grabbed() || m_holdState.has_value() || reactMode != combat::CombatMode::Free) {
        return;
    }
    // With reactions off (an AI's block goal, docs/research/ai.md#block) the hit only takes health.
    if (hitReactionsOff()) {
        return;
    }
    // On the ground a strike gets the ground's reaction.
    if (m_victim.grounded()) {
        animator.playCombat(clips::one(clips::kGroundedStrikeReact), clips::kGroundedIdle, AnimState::Hold);
        return;
    }
    // 6. A player's hit armour: winding up or in the chain window, its attack goes on with no reaction.
    if (m_player && combat::hitArmourHolds(animator.flags(), hit.attackAnim, hit.ignoresArmour)) {
        ++m_hitsArmoured;
        return;
    }
    // 7. The reaction; the attack playing is lost.
    m_combat.interrupt();
    m_slideUpdates = 0;
    m_steer.clear();
    m_victim.react(hit, here, animator, kAnimFightIdle, input.nowMs);
    m_reacting = true;
    // The reaction shakes the players' cameras at the hit code's strength (docs/research/camera.md#shake).
    m_reactionShake =
        ReactionShake{.level = combat::decodeHitCode(hit.code).strength, .attackerIsPlayer = hit.attackerIsPlayer};
}

void Fighter::reportHitSound(const IncomingHit& hit, std::uint32_t struck) {
    // A charging body's strike (Strike_Contact, before the hit's own sound): `HUMAN` against `HUMAN` at the attacker.
    if (hit.charge) {
        m_sounds.push_back(HumanSound{.kind = HumanSound::Kind::Impact,
                                      .material1 = material::kHuman,
                                      .material2 = material::kHuman,
                                      .volume = 1.0F,
                                      .victimDown = false,
                                      .ownerIsPlayer = hit.attackerIsPlayer,
                                      .at = hit.attacker});
    }
    // Only a strike that names its material, on a human with health left, sounds; it sounds at the attacker, his.
    if (hit.strikeMaterial == 0 || m_health.depleted()) {
        return;
    }
    m_sounds.push_back(HumanSound{.kind = HumanSound::Kind::Impact,
                                  .material1 = hit.strikeMaterial,
                                  .material2 = struck,
                                  .volume = 1.0F,
                                  .victimDown = m_victim.grounded(),
                                  .ownerIsPlayer = hit.attackerIsPlayer,
                                  .at = hit.attacker});
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
        earnRage(combat::kGrabFrontCounter, input.nowMs);
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
            earnRage(outcome.animId, input.nowMs);
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

void Fighter::struggleInHold(const FighterInput& input, const HumanAnimator& animator) {
    const std::optional<HeldGrabber> held = std::exchange(m_heldGrabber, std::nullopt);
    if (!held.has_value()) {
        return;
    }
    combat::GrabbedInput in;
    in.command = input.command;
    in.fromRear = held->fromRear;
    in.hurt = hurt();
    in.ownPowerFraction = m_combat.power().fraction();
    in.ownStruggleDivisor = m_victim.powerClass().struggleDivisor;
    in.movePlaying = animator.drivingClipPlaying();
    in.grabber = held->grabber;
    combat::GrabbedOutcome outcome = combat::updateGrabbed(in, m_grabbedRandom);
    if (outcome.action == combat::GrabbedAction::Reversal) {
        outcome = combat::GrabbedOutcome{};
    }
    // The cost adds up until the grabber takes the report; a move is kept until then (one at a time plays).
    m_heldReport.grabberPowerCost += outcome.grabberPowerCost;
    if (outcome.action == combat::GrabbedAction::None || m_heldReport.action != combat::GrabbedAction::None) {
        return;
    }
    m_heldReport.action = outcome.action;
    m_heldReport.victimClip = outcome.animId;
    m_heldReport.grabberClip = outcome.animId + 1;
    m_heldReport.ended = outcome.action == combat::GrabbedAction::Escape;
    if (outcome.action != combat::GrabbedAction::Escape) {
        m_heldReport.grabberDamage =
            m_ranges != nullptr ? m_ranges->damage(static_cast<std::size_t>(outcome.animId)) : 0;
    }
    if (outcome.action == combat::GrabbedAction::StrikeBack) {
        earnRage(outcome.animId, input.nowMs);
        m_repeat.note(outcome.animId, input.nowMs);
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

void Fighter::stunFor(HumanAnimator& animator, std::uint64_t nowMs, std::uint64_t durationMs) {
    m_victim.stunUntil(nowMs + durationMs);
    animator.playCombat(clips::one(static_cast<std::uint32_t>(combat::kStunLoop)),
                        static_cast<std::uint32_t>(combat::kStunLoop), AnimState::Hold);
}

void Fighter::endStun(std::uint64_t nowMs) {
    if (m_victim.stunned()) {
        m_victim.stunUntil(nowMs);
    }
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
        } else if (const Combatant* found = pickTarget(input, reach); found != nullptr) {
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
    const anim::AnimClip* clip = animator.clip(duck);
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

void Fighter::enterHold(TargetState targetState, HumanAnimator& animator, std::uint64_t nowMs) {
    // Going down starts the ground time; anything else ends a stun and the ground (as a passive target's does).
    if (targetState != TargetState::Grounded) {
        m_victim.clear();
    } else if (!m_victim.grounded()) {
        m_victim.knockDown(nowMs, false);
    }
    if (targetState == TargetState::Held || targetState == TargetState::Mounted) {
        // Taken into the hold: a grab it held, a catch on it, the attack and the steer it was making all end.
        if (!m_holdState.has_value()) {
            if (m_held != nullptr) {
                releaseHold(animator, false);
            }
            m_catch.reset();
            m_grabbed.reset();
            m_tacklePending = false;
            m_mountPending = false;
            m_combat.interrupt();
            m_combat.release();
            m_notice.reset();
            m_slideUpdates = 0;
            m_steer.clear();
        }
        m_holdState = targetState;
        m_holdUnkept = 0;
        m_brokenFrom.reset();
        return;
    }
    // Let go: the clip the grabber gave plays out before it acts again.
    m_holdState.reset();
    m_holdAttached = false;
    m_reacting = true;
}

void Fighter::freeFromLostGrabber(HumanAnimator& animator) {
    // As if the grabber's placement had broken the pair: the victim's side of the break (245 then the rise from the
    // mount, 107 from a rear hold, else 145), then the idle.
    const bool mounted = m_holdState == TargetState::Mounted;
    const std::uint32_t clip = animator.animId();
    const bool rear = clip == clips::kGrabRearHeld || clip == clips::kGrabReactFromRear;
    enterHold(TargetState::Standing, animator, 0);
    if (mounted) {
        static constexpr std::array<std::uint32_t, 2> kUp{clips::kBreakMountedVictim, clips::kGroundedRise};
        animator.playCombat(kUp, clips::kIdle, AnimState::Attack);
    } else {
        animator.playCombat(clips::one(rear ? clips::kBreakRearVictim : clips::kBreakFrontVictim), clips::kIdle,
                            AnimState::Attack);
    }
}

} // namespace coney::human
