// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/fighter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>

#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "combat/lock_on.h"
#include "combat/reactions.h"
#include "human/fighter_clips.h"
#include "human/pair_placement.h"

// The fighter's grabs the player holds: the intro and the alignment, the connecting clips, the snap to the hold and
// the attachment, the moves inside the hold, the tackle's mount, the stick turning the pair, and letting go.
// Research: docs/research/combat.md#grab-posing, docs/research/combat.md#grab-turn, docs/research/combat.md#grabbing

namespace coney::human {

namespace {

namespace id = combat::anim_id;

constexpr float kPi = std::numbers::pi_v<float>;

// The alignment's slide is dropped from this far, or above this speed (m/s).
constexpr float kAlignMaxSlide = 13.0F;
constexpr float kAlignMaxSpeed = 50.0F;

// The victim's escape clips from the front and the rear (the grabber plays each + 1).
constexpr int kEscapeFront = 100;
constexpr int kEscapeRear = 112;

} // namespace

void Fighter::dropLostHold(const FighterInput& input, HumanAnimator& animator) {
    // Compared as pointers only: a victim gone from the targets may no longer exist.
    const auto listed = [&input](const Holdable* victim) {
        return std::ranges::find(input.targets, static_cast<const Combatant*>(victim)) != input.targets.end();
    };
    if (m_thrown != nullptr && !listed(m_thrown)) {
        m_thrown = nullptr;
    }
    if (m_held == nullptr) {
        return;
    }
    // Still held, not stopped yet (the intro), or out of health (which step 5 of the update lets go of as it does any
    // hold): nothing to drop.
    if (listed(m_held)) {
        const TargetState state = m_held->state();
        if (m_pair == PairStage::Intro || state == TargetState::Held || state == TargetState::Mounted ||
            m_held->health().depleted()) {
            return;
        }
    }
    // The grabber stands in its fight idle; the victim, if it is still there, is left as it is.
    m_held = nullptr;
    m_pair = PairStage::None;
    m_turnUpdates = 0;
    m_grabTurn = 0.0F;
    m_rear = false;
    m_tacklePending = false;
    m_mountPending = false;
    m_combat.release();
    animator.playCombat(clips::kNoClips, kAnimFightIdle, AnimState::Attack);
}

void Fighter::startHold(Holdable& victim, const FighterInput& input, float& heading, bool tackle, int connect,
                        HumanAnimator& animator) {
    const anim::Vec3 to = anim::subtract(victim.position(), input.position);
    const float toVictim = std::hypot(to.x, to.y) > 1e-4F ? headingOf(to) : heading;
    m_held = &victim;
    m_target = &victim;
    m_rear = false;
    m_pair = PairStage::None;
    if (tackle) {
        // The tackle faces the victim at once; the intro covers the distance and the victim waits for the hit clip.
        heading = toVictim;
        const std::array<std::uint32_t, 2> tackleClips{id::kTacklePlayerIntro, clips::kTackleHit};
        animator.playCombat(tackleClips, clips::kMountingIdle, AnimState::Hold, kCombatFade, clips::kGrabHolds);
        victim.face(input.position);
        victim.play(clips::kNoClips, clips::kIdle, AnimState::Hold, TargetState::Held);
        m_tacklePending = true;
        return;
    }
    // The grab: from the rear when the player stands on the victim's rear side, else from the front. **Coney's
    // choice**: the side is decided as the intro starts rather than at its end; a passive target does not move between.
    m_rear = combat::victimSide(victim.position(), victim.heading(), input.position) == combat::Side::Rear;
    m_connect = connect;
    const std::array<std::uint32_t, 2> grabClips{id::kGrabPlayerIntro, connectClip()};
    animator.playCombat(grabClips, m_rear ? clips::kGrabRearHold : clips::kGrabHold, AnimState::Hold, kCombatFade,
                        clips::kGrabHolds);
    // The intro turns the grabber to face the victim over its playing time.
    const auto intro = static_cast<std::uint32_t>(id::kGrabPlayerIntro);
    const anim::AnimClip* introClip = animator.anims().clip(intro);
    const float introSeconds = introClip != nullptr ? introClip->duration / animator.anims().rate(intro) : 0.0F;
    m_turnUpdates = std::max(1, static_cast<int>(std::lround(introSeconds / input.stepSeconds)));
    m_turnStep = wrapAngle(toVictim - heading) / static_cast<float>(m_turnUpdates);
    m_victimTurnStep = 0.0F;
    // The victim is left alone until the connect stops it (`Grab_Connect`): a human may still act (an AI's R1 in
    // these updates is its counter, docs/research/ai.md#block).
    m_pair = PairStage::Intro;
}

void Fighter::stepAlignment(float& heading) {
    if (m_turnUpdates <= 0) {
        return;
    }
    --m_turnUpdates;
    heading = wrapAngle(heading + m_turnStep);
    if (m_held != nullptr && m_pair != PairStage::Attached && m_victimTurnStep != 0.0F) {
        m_held->place(m_held->position(), m_held->heading() + m_victimTurnStep);
    }
}

void Fighter::followPairClips(const FighterInput& input, HumanAnimator& animator, float heading) {
    if (m_held == nullptr) {
        m_pair = PairStage::None;
        return;
    }
    const std::uint32_t clip = animator.animId();
    // A power move has ended with no extension after it: the grab is over.
    if (clips::isPowerMove(m_lastClip) && clip != m_lastClip && !clips::isPowerMove(clip)) {
        endPowerMove();
        return;
    }
    // The intro has handed over to the connecting clip.
    if (m_pair == PairStage::Intro && clip == connectClip()) {
        connect(input, animator, heading);
        return;
    }
    // A connecting clip, a spin or the mount's pick-up has ended: the victim is snapped to the hold of the side the
    // grab is now on (a spin set the side as it started). At a connecting clip's end the gate may release the grab
    // instead.
    const bool connectEnded = m_lastClip == connectClip();
    const bool pickedUp = m_lastClip == clips::clipOf(combat::anim_id::kMountPickup);
    if (clip != m_lastClip && m_pair == PairStage::Moving && (connectEnded || pickedUp || clips::isSpin(m_lastClip))) {
        const anim::Vec3 offset = pairPoint(m_ranges, m_rear ? clips::kGrabRearHold : clips::kGrabHold,
                                            m_rear ? kRearHoldOffset : kFrontHoldOffset);
        if (connectEnded && !holdGatePasses(input.position, m_held->position(), anim::length(offset))) {
            releaseHold(animator, false);
            return;
        }
        snapAttach(input, heading, offset, m_rear ? 0.0F : kPi);
        if (connectEnded && m_connect != static_cast<int>(clips::kGrabFrontEnd)) {
            landGrapple(input);
        }
    }
    detachForSpin(animator);
}

void Fighter::detachForSpin(const HumanAnimator& animator) {
    // A spin carries both bodies by their clips (the victim's turns it half round): the victim leaves its offset
    // until the spin's end snaps it again.
    if (m_held != nullptr && m_pair == PairStage::Attached && clips::isSpin(animator.animId())) {
        m_held->setAttached(false);
        m_pair = PairStage::Moving;
    }
}

void Fighter::connect(const FighterInput& input, HumanAnimator& animator, float heading) {
    const std::uint32_t clip = connectClip();
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(clip) : nullptr;
    const float reach =
        range != nullptr && range->reach > 0.0F ? range->reach : (m_rear ? kConnectReachRear : kConnectReachFront);
    const float listed = m_ranges != nullptr ? m_ranges->farRange(clip) : 0.0F;
    const float far = (listed > 0.0F ? listed : kConnectFarRange) * kPlayerFarScale;
    const PairAlignment align = alignPair(input.position, m_held->position(), reach, far, m_rear);
    if (!align.inRange || m_held->state() != TargetState::Standing) {
        // Too far, or no longer on its feet (a human the intro left free may have gone down): the grab fails, the
        // miss plays on; the victim was never stopped.
        const std::array<std::uint32_t, 2> miss{clips::kGrabMiss, clips::kNormalFromFight};
        animator.playCombat(miss, clips::kIdle, AnimState::Attack, clips::kPairFade, clips::kGrabHolds);
        m_combat.release();
        m_held = nullptr;
        m_pair = PairStage::None;
        return;
    }
    // Over the alignment's time the grabber turns to the victim and slides to the clip's reach, the victim turns.
    const anim::AnimClip* connecting = animator.anims().clip(clip);
    const float seconds = connecting != nullptr ? alignSeconds(*connecting, animator.anims().rate(clip)) : 0.0F;
    m_turnUpdates = std::max(1, static_cast<int>(std::lround(seconds / input.stepSeconds)));
    const auto updates = static_cast<float>(m_turnUpdates);
    const float turn = wrapAngle(align.grabberHeading - heading);
    m_turnStep = std::fabs(turn) < kAlignMinTurn ? 0.0F : turn / updates;
    const float victimTurn = wrapAngle(align.victimHeading - m_held->heading());
    m_victimTurnStep = std::fabs(victimTurn) < kAlignMinTurn ? 0.0F : victimTurn / updates;
    const anim::Vec3 slide{align.grabberFeet.x - input.position.x, align.grabberFeet.y - input.position.y, 0.0F};
    const float distance = anim::length(slide);
    const float speed = distance / (updates * input.stepSeconds);
    m_slideUpdates = 0;
    if (distance >= kAlignMinSlide && distance < kAlignMaxSlide && speed <= kAlignMaxSpeed) {
        m_slide = anim::scale(slide, 1.0F / (updates * input.stepSeconds));
        m_slideUpdates = m_turnUpdates;
    }
    // Both humans switch on the same update: the victim plays the grabber's set's reaction, then its own hold.
    m_held->playPaired(clips::one(clip + 1), animator.anims(), m_rear ? clips::kGrabRearHeld : clips::kGrabHeld,
                       AnimState::Hold, TargetState::Held);
    m_pair = PairStage::Moving;
}

void Fighter::landGrapple(const FighterInput& input) {
    // With power left, the victim takes the damage table's value of the strike (`Grab_ConnectEnd`). The hit is
    // reported as the hold's id, which is playing when the victim applies it: the tutorial waits for 82 or 84. It
    // scores as the strike.
    if (m_held == nullptr || m_combat.power().value() <= 0) {
        return;
    }
    const auto strike = static_cast<int>(connectClip());
    const int damage = m_ranges != nullptr ? combat::strikeDamage(*m_ranges, strike) : 0;
    const auto hold = static_cast<int>(m_rear ? clips::kGrabRearHold : clips::kGrabHold);
    m_held->hit(IncomingHit{.damage = damage,
                            .attackAnim = hold,
                            .attacker = input.position,
                            .react = false,
                            .ignoresArmour = m_player || hasFlag(flag::kIncreasedReact),
                            .attackerFlag200000 = hasFlag(flag::kIncreasedReact),
                            .attackerIsPlayer = m_player});
    ++m_hitsLanded;
    m_damageDealt += damage;
    if (m_player) {
        m_strikes.push_back(hold);
    }
    earnRage(strike, input.nowMs, false);
    m_repeat.note(strike, input.nowMs);
}

void Fighter::snapAttach(const FighterInput& input, float heading, anim::Vec3 offset, float turn) {
    m_holdOffset = offset;
    m_holdTurn = turn;
    m_held->setAttached(true);
    m_pair = PairStage::Attached;
    m_turnUpdates = 0;
    placeAttached(input.position, heading);
}

void Fighter::placeAttached(anim::Vec3 position, float heading) const {
    m_held->place(fromFrame(position, heading, m_holdOffset), heading + m_holdTurn);
}

bool Fighter::victimInPlace(const FighterInput& input) const {
    // A move in the hold finds the victim at the hold's point (the strikes' points are the front hold's); before the
    // snap it is not there yet. **Coney's choice**: the point checked is the current hold's.
    if (m_held == nullptr) {
        return true;
    }
    if (m_pair != PairStage::Attached) {
        return false;
    }
    const anim::Vec3 point = pairPoint(m_ranges, m_rear ? clips::kGrabRearHold : clips::kGrabHold,
                                       m_rear ? kRearHoldOffset : kFrontHoldOffset);
    return pairInPlace(input.position, input.heading, m_held->position(), point);
}

anim::Vec3 Fighter::moveGrab(float stickHeading, float stickMagnitude, const HumanAnimator& animator, float& heading) {
    const combat::CombatTuning& tuning = combat::combatTuning();
    // Only a standing hold turns: no move of the hold playing, the victim attached, the stick nearly full.
    const bool standing = m_combat.mode() == combat::CombatMode::Grabbing && m_held != nullptr &&
                          m_pair == PairStage::Attached && (animator.flags() & combat::kAttackRefusingPhases) == 0;
    if (!standing || stickMagnitude <= tuning.grabTurnStick) {
        m_grabTurn = 0.0F;
        return {};
    }
    // The grabber turns its back to the stick and walks backward along it, pulling the victim.
    const float wanted = wrapAngle(stickHeading + kPi);
    m_grabTurn = combat::grabTurnStep(tuning, wrapAngle(wanted - heading), m_grabTurn);
    heading = wrapAngle(heading + m_grabTurn);
    const float speed = m_rear ? tuning.grabWalkRear : tuning.grabWalkFront;
    return anim::scale(facing(heading), -speed);
}

void Fighter::playGrabAction(const combat::CombatOutput& out, HumanAnimator& animator) {
    const std::uint32_t hold = m_rear ? clips::kGrabRearHold : clips::kGrabHold;
    const std::uint32_t held = m_rear ? clips::kGrabRearHeld : clips::kGrabHeld;
    switch (out.grabAction) {
    case combat::GrabAction::Strike:
        // The strike and the victim's reaction (the next id), then both back to the hold.
        animator.playCombat(clips::one(clips::clipOf(out.startAnim)), hold, AnimState::Hold, clips::kPairFade,
                            clips::kAttackHolds);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clips::clipOf(out.startAnim) + 1), animator.anims(), held, AnimState::Hold,
                               TargetState::Held);
        }
        break;
    case combat::GrabAction::PowerStrike: {
        // From the rear the victim is spun to the front first. The power move ends the grab: the grabber settles
        // through 389, the victim falls (endPowerMove()), unless an extension takes over.
        const std::uint32_t strike = clips::clipOf(out.startAnim);
        if (m_rear) {
            const std::array<std::uint32_t, 3> moves{id::kGrabSpinToFront, strike, clips::kNormalFromFight};
            const std::array<std::uint32_t, 2> reacts{id::kGrabSpinToFront + 1, strike + 1};
            animator.playCombat(moves, clips::kIdle, AnimState::Attack, clips::kPairFade, clips::kAttackHolds);
            if (m_held != nullptr) {
                m_held->playPaired(reacts, animator.anims(), clips::kGroundedIdle, AnimState::Hold, TargetState::Held);
            }
            m_rear = false;
        } else {
            const std::array<std::uint32_t, 2> moves{strike, clips::kNormalFromFight};
            animator.playCombat(moves, clips::kIdle, AnimState::Attack, clips::kPairFade, clips::kAttackHolds);
            if (m_held != nullptr) {
                m_held->playPaired(clips::one(strike + 1), animator.anims(), clips::kGroundedIdle, AnimState::Hold,
                                   TargetState::Held);
            }
        }
        break;
    }
    case combat::GrabAction::Throw:
        // The throw lets go: the victim plays its reaction and lands on its back.
        animator.playCombat(clips::one(clips::clipOf(out.startAnim)), kAnimFightIdle, AnimState::Attack,
                            clips::kPairFade, clips::kAttackHolds);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clips::clipOf(out.startAnim) + 1), animator.anims(), clips::kGroundedIdle,
                               AnimState::Hold, TargetState::Grounded);
        }
        m_thrown = m_held;
        m_pair = PairStage::None;
        m_held = nullptr;
        m_rear = false;
        break;
    case combat::GrabAction::Spin: {
        // The spin to the other side, then that side's hold.
        m_rear = out.startAnim == id::kGrabSpinToRear;
        animator.playCombat(clips::one(clips::clipOf(out.startAnim)), m_rear ? clips::kGrabRearHold : clips::kGrabHold,
                            AnimState::Hold, clips::kPairFade, clips::kGrabHolds);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clips::clipOf(out.startAnim) + 1), animator.anims(),
                               m_rear ? clips::kGrabRearHeld : clips::kGrabHeld, AnimState::Hold, TargetState::Held);
        }
        break;
    }
    case combat::GrabAction::Mount:
        // Both go down together (118 / 119, paired), then sit in the mount: the player 210 on the victim's 207.
        animator.playCombat(clips::one(clips::clipOf(out.startAnim)), clips::kMountingIdle, AnimState::Hold,
                            clips::kPairFade, clips::kGrabHolds);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clips::clipOf(out.startAnim) + 1), animator.anims(), clips::kMountedIdle,
                               AnimState::Hold, TargetState::Mounted);
        }
        m_rear = false;
        m_mountPending = true;
        break;
    case combat::GrabAction::Mug: {
        // The victim is spun to a rear hold (unless it is there already), then the mugging loop.
        if (m_rear) {
            animator.playCombat(clips::one(clips::kMugIntro), clips::kMugLoop, AnimState::Hold, kCombatFade,
                                clips::kGrabHolds);
            if (m_held != nullptr) {
                m_held->play(clips::one(clips::kMugIntroReact), clips::kMugLoopReact, AnimState::Hold,
                             TargetState::Held);
            }
        } else {
            const std::array<std::uint32_t, 2> mug{id::kGrabSpinToRear, clips::kMugIntro};
            const std::array<std::uint32_t, 2> mugReact{id::kGrabSpinToRear + 1, clips::kMugIntroReact};
            animator.playCombat(mug, clips::kMugLoop, AnimState::Hold, kCombatFade, clips::kGrabHolds);
            if (m_held != nullptr) {
                m_held->play(mugReact, clips::kMugLoopReact, AnimState::Hold, TargetState::Held);
            }
        }
        m_rear = true;
        m_mugOnTarget = false;
        break;
    }
    case combat::GrabAction::LetGo:
        if (m_held != nullptr) {
            releaseHold(animator, true);
        } else {
            // Holding a grabber the player reversed (no target to let go of): just the let-go.
            animator.playCombat(clips::one(id::kGrabLetGo), kAnimFightIdle, AnimState::Attack, kCombatFade,
                                clips::kGrabHolds);
            m_rear = false;
        }
        break;
    case combat::GrabAction::Release:
        if (m_held != nullptr) {
            releaseHold(animator, false);
        }
        break;
    case combat::GrabAction::None:
        break;
    }
}

void Fighter::mountVictim(const FighterInput& input, const HumanAnimator& animator, float heading) {
    // The victim goes down under the player (the attacker's clip, a paired task), mounted, and stays at clip 210's
    // pair event: 0.120 m to the mounter's left and 0.032 m ahead, facing the other way.
    m_tacklePending = false;
    m_mountPending = false;
    m_held->playPaired(clips::one(clips::kTackleReact), animator.anims(), clips::kMountedIdle, AnimState::Hold,
                       TargetState::Mounted);
    snapAttach(input, heading, pairEventPoint(animator.anims().clip(clips::kMountingIdle), kMountOffset), kPi);
}

void Fighter::playMountAction(const combat::CombatOutput& out, HumanAnimator& animator) {
    const std::uint32_t clip = clips::clipOf(out.startAnim);
    switch (out.mountAction) {
    case combat::MountAction::Strike:
    case combat::MountAction::PowerStrike:
        // The strike and its reaction, both back to the mount's idles.
        animator.playCombat(clips::one(clip), clips::kMountingIdle, AnimState::Hold, clips::kPairFade,
                            clips::kAttackHolds);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clip + 1), animator.anims(), clips::kMountedIdle, AnimState::Hold,
                               TargetState::Mounted);
        }
        break;
    case combat::MountAction::ToHold:
        // Both rise to the front hold (248 / 249, then 82 / 83); the victim leaves the mount's offset until the
        // pick-up's end snaps it to the hold, as after a spin.
        animator.playCombat(clips::one(clip), clips::kGrabHold, AnimState::Hold, clips::kPairFade, clips::kGrabHolds);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clip + 1), animator.anims(), clips::kGrabHeld, AnimState::Hold,
                               TargetState::Held);
            m_held->setAttached(false);
            m_pair = PairStage::Moving;
        }
        m_rear = false;
        break;
    case combat::MountAction::GetOff:
        // The player gets off (244); the victim plays 245, then rises with 199.
        if (m_held != nullptr) {
            Holdable& victim = *m_held;
            m_held->setAttached(false);
            m_held = nullptr;
            m_pair = PairStage::None;
            m_tacklePending = false;
            m_mountPending = false;
            const std::array<std::uint32_t, 2> rise{clip + 1, clips::kGroundedRise};
            victim.play(rise, clips::kIdle, AnimState::Attack, TargetState::Standing);
        }
        animator.playCombat(clips::one(clip), kAnimFightIdle, AnimState::Attack, kCombatFade, clips::kGrabHolds);
        m_rear = false;
        break;
    case combat::MountAction::None:
        break;
    }
}

void Fighter::seatMount(const FighterInput& input, const HumanAnimator& animator, float heading) {
    // The victim already plays 119 into 207; only its place changes, to clip 210's pair event.
    m_mountPending = false;
    snapAttach(input, heading, pairEventPoint(animator.anims().clip(clips::kMountingIdle), kMountOffset), kPi);
}

void Fighter::victimEscapes(const FighterInput& input, HumanAnimator& animator) {
    // The victim breaks free with its escape and takes the clip's damage; the player plays the grabber's side from
    // the victim's set and goes down stunned.
    const int escape = m_rear ? kEscapeRear : kEscapeFront;
    Holdable& victim = *m_held;
    const int damage = m_ranges != nullptr ? m_ranges->damage(static_cast<std::size_t>(escape)) : 0;
    releaseHold(animator, false);
    victim.play(clips::one(clips::clipOf(escape)), clips::kIdle, AnimState::Attack, TargetState::Standing);
    if (damage > 0) {
        victim.hit(IncomingHit{.damage = damage, .attackAnim = escape, .attacker = victim.position(), .react = false});
    }
    animator.playPaired(clips::one(clips::clipOf(escape) + 1), victim.anims(), clips::kGroundedIdle, AnimState::Hold);
    m_victim.knockDown(input.nowMs, true);
    m_reacting = true;
}

void Fighter::endPowerMove() {
    m_held->setAttached(false);
    m_pair = PairStage::None;
    m_turnUpdates = 0;
    m_grabTurn = 0.0F;
    m_combat.release();
    if (!m_held->health().depleted()) {
        m_held->play(clips::kNoClips, clips::kGroundedIdle, AnimState::Hold, TargetState::Grounded);
    }
    m_held = nullptr;
    m_rear = false;
}

void Fighter::releaseHold(HumanAnimator& animator, bool letGo) {
    m_held->setAttached(false);
    m_pair = PairStage::None;
    m_turnUpdates = 0;
    m_grabTurn = 0.0F;
    const bool mounted = m_held->state() == TargetState::Mounted;
    m_combat.release();
    if (letGo) {
        animator.playCombat(clips::one(id::kGrabLetGo), kAnimFightIdle, AnimState::Attack, kCombatFade,
                            clips::kGrabHolds);
    } else {
        animator.playCombat(clips::kNoClips, kAnimFightIdle, AnimState::Attack);
    }
    if (!m_held->health().depleted()) {
        // A mounted victim gets up; a let-go one plays its clip; a held one stands where it is.
        const auto rise = clips::one(clips::kGroundedRise);
        const auto freed = clips::one(clips::kGrabLetGoReact);
        std::span<const std::uint32_t> after = clips::kNoClips;
        if (mounted) {
            after = rise;
        } else if (letGo) {
            after = freed;
        }
        m_held->play(after, clips::kIdle, AnimState::Attack, TargetState::Standing);
    }
    m_held = nullptr;
    m_rear = false;
    m_tacklePending = false;
    m_mountPending = false;
}

} // namespace coney::human
