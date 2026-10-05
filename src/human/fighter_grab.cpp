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

void Fighter::startHold(TargetHuman& victim, const FighterInput& input, float& heading, bool tackle,
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
        animator.playCombat(tackleClips, clips::kMountingIdle, AnimState::Hold);
        victim.face(input.position);
        victim.play(clips::kNoClips, clips::kIdle, AnimState::Hold, TargetState::Held);
        m_tacklePending = true;
        return;
    }
    // The grab: from the rear when the player stands on the victim's rear side, else from the front. **Coney's
    // choice**: the side is decided as the intro starts rather than at its end; a passive target does not move between.
    m_rear = combat::victimSide(victim.position(), victim.heading(), input.position) == combat::Side::Rear;
    const std::array<std::uint32_t, 2> grabClips{id::kGrabPlayerIntro,
                                                 m_rear ? clips::kGrabRearEnd : clips::kGrabFrontEnd};
    animator.playCombat(grabClips, m_rear ? clips::kGrabRearHold : clips::kGrabHold, AnimState::Hold);
    // The intro turns the grabber to face the victim over its playing time; the victim's own movement stops.
    const auto intro = static_cast<std::uint32_t>(id::kGrabPlayerIntro);
    const anim::AnimClip* introClip = animator.anims().clip(intro);
    const float introSeconds = introClip != nullptr ? introClip->duration / animator.anims().rate(intro) : 0.0F;
    m_turnUpdates = std::max(1, static_cast<int>(std::lround(introSeconds / kStepSeconds)));
    m_turnStep = wrapAngle(toVictim - heading) / static_cast<float>(m_turnUpdates);
    m_victimTurnStep = 0.0F;
    victim.play(clips::kNoClips, clips::kIdle, AnimState::Hold, TargetState::Held);
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
    // The intro has handed over to the connecting clip.
    if (m_pair == PairStage::Intro && clip == (m_rear ? clips::kGrabRearEnd : clips::kGrabFrontEnd)) {
        connect(input, animator, heading);
        return;
    }
    // A connecting clip or a spin has ended: the victim is snapped to the hold of the side the grab is now on (a
    // spin set the side as it started). At a connecting clip's end the gate may release the grab instead.
    const bool connectEnded = m_lastClip == clips::kGrabFrontEnd || m_lastClip == clips::kGrabRearEnd;
    if (clip != m_lastClip && m_pair == PairStage::Moving && (connectEnded || clips::isSpin(m_lastClip))) {
        const anim::Vec3 offset = pairPoint(m_ranges, m_rear ? clips::kGrabRearHold : clips::kGrabHold,
                                            m_rear ? kRearHoldOffset : kFrontHoldOffset);
        if (connectEnded && !holdGatePasses(input.position, m_held->position(), anim::length(offset))) {
            releaseHold(animator, false);
            return;
        }
        snapAttach(input, heading, offset, m_rear ? 0.0F : kPi);
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
    const std::uint32_t clip = m_rear ? clips::kGrabRearEnd : clips::kGrabFrontEnd;
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(clip) : nullptr;
    const float reach =
        range != nullptr && range->reach > 0.0F ? range->reach : (m_rear ? kConnectReachRear : kConnectReachFront);
    const float listed = m_ranges != nullptr ? m_ranges->farRange(clip) : 0.0F;
    const float far = (listed > 0.0F ? listed : kConnectFarRange) * kPlayerFarScale;
    const PairAlignment align = alignPair(input.position, m_held->position(), reach, far, m_rear);
    if (!align.inRange) {
        // Too far: the grab fails, the miss plays on and the victim is free.
        const std::array<std::uint32_t, 2> miss{clips::kGrabMiss, clips::kNormalFromFight};
        animator.playCombat(miss, clips::kIdle, AnimState::Attack, clips::kPairFade);
        m_combat.release();
        m_held->play(clips::kNoClips, clips::kIdle, AnimState::Attack, TargetState::Standing);
        m_held = nullptr;
        m_pair = PairStage::None;
        return;
    }
    // Over the alignment's time the grabber turns to the victim and slides to the clip's reach, the victim turns.
    const anim::AnimClip* connecting = animator.anims().clip(clip);
    const float seconds = connecting != nullptr ? alignSeconds(*connecting, animator.anims().rate(clip)) : 0.0F;
    m_turnUpdates = std::max(1, static_cast<int>(std::lround(seconds / kStepSeconds)));
    const auto updates = static_cast<float>(m_turnUpdates);
    const float turn = wrapAngle(align.grabberHeading - heading);
    m_turnStep = std::fabs(turn) < kAlignMinTurn ? 0.0F : turn / updates;
    const float victimTurn = wrapAngle(align.victimHeading - m_held->heading());
    m_victimTurnStep = std::fabs(victimTurn) < kAlignMinTurn ? 0.0F : victimTurn / updates;
    const anim::Vec3 slide{align.grabberFeet.x - input.position.x, align.grabberFeet.y - input.position.y, 0.0F};
    const float distance = anim::length(slide);
    const float speed = distance / (updates * kStepSeconds);
    m_slideUpdates = 0;
    if (distance >= kAlignMinSlide && distance < kAlignMaxSlide && speed <= kAlignMaxSpeed) {
        m_slide = anim::scale(slide, 1.0F / (updates * kStepSeconds));
        m_slideUpdates = m_turnUpdates;
    }
    // Both humans switch on the same update: the victim plays the grabber's set's reaction, then its own hold.
    m_held->playPaired(clips::one(m_rear ? clips::kGrabReactFromRear : clips::kGrabReactFromFront), animator.anims(),
                       m_rear ? clips::kGrabRearHeld : clips::kGrabHeld, AnimState::Hold, TargetState::Held);
    m_pair = PairStage::Moving;
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
                          m_pair == PairStage::Attached && !animator.drivingClipPlaying();
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
        animator.playCombat(clips::one(clips::clipOf(out.startAnim)), hold, AnimState::Hold, clips::kPairFade);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clips::clipOf(out.startAnim) + 1), animator.anims(), held, AnimState::Hold,
                               TargetState::Held);
        }
        break;
    case combat::GrabAction::PowerStrike: {
        // From the rear the victim is spun to the front first; both end in the front hold.
        const std::uint32_t strike = clips::clipOf(out.startAnim);
        if (m_rear) {
            const std::array<std::uint32_t, 2> moves{id::kGrabSpinToFront, strike};
            const std::array<std::uint32_t, 2> reacts{id::kGrabSpinToFront + 1, strike + 1};
            animator.playCombat(moves, clips::kGrabHold, AnimState::Hold, clips::kPairFade);
            if (m_held != nullptr) {
                m_held->playPaired(reacts, animator.anims(), clips::kGrabHeld, AnimState::Hold, TargetState::Held);
            }
            m_rear = false;
        } else {
            animator.playCombat(clips::one(strike), clips::kGrabHold, AnimState::Hold, clips::kPairFade);
            if (m_held != nullptr) {
                m_held->playPaired(clips::one(strike + 1), animator.anims(), clips::kGrabHeld, AnimState::Hold,
                                   TargetState::Held);
            }
        }
        break;
    }
    case combat::GrabAction::Throw:
        // The throw lets go: the victim plays its reaction and lands on its back.
        animator.playCombat(clips::one(clips::clipOf(out.startAnim)), kAnimFightIdle, AnimState::Attack,
                            clips::kPairFade);
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
                            AnimState::Hold, clips::kPairFade);
        if (m_held != nullptr) {
            m_held->playPaired(clips::one(clips::clipOf(out.startAnim) + 1), animator.anims(),
                               m_rear ? clips::kGrabRearHeld : clips::kGrabHeld, AnimState::Hold, TargetState::Held);
        }
        break;
    }
    case combat::GrabAction::Mug: {
        // The victim is spun to a rear hold (unless it is there already), then the mugging loop.
        if (m_rear) {
            animator.playCombat(clips::one(clips::kMugIntro), clips::kMugLoop, AnimState::Hold);
            if (m_held != nullptr) {
                m_held->play(clips::one(clips::kMugIntroReact), clips::kMugLoopReact, AnimState::Hold,
                             TargetState::Held);
            }
        } else {
            const std::array<std::uint32_t, 2> mug{id::kGrabSpinToRear, clips::kMugIntro};
            const std::array<std::uint32_t, 2> mugReact{id::kGrabSpinToRear + 1, clips::kMugIntroReact};
            animator.playCombat(mug, clips::kMugLoop, AnimState::Hold);
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
            animator.playCombat(clips::one(id::kGrabLetGo), kAnimFightIdle, AnimState::Attack);
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
    m_held->playPaired(clips::one(clips::kTackleReact), animator.anims(), clips::kMountedIdle, AnimState::Hold,
                       TargetState::Mounted);
    snapAttach(input, heading, pairEventPoint(animator.anims().clip(clips::kMountingIdle), kMountOffset), kPi);
}

void Fighter::victimEscapes(const FighterInput& input, HumanAnimator& animator) {
    // The victim breaks free with its escape and takes the clip's damage; the player plays the grabber's side from
    // the victim's set and goes down stunned.
    const int escape = m_rear ? kEscapeRear : kEscapeFront;
    TargetHuman& victim = *m_held;
    const int damage = m_ranges != nullptr ? m_ranges->damage(static_cast<std::size_t>(escape)) : 0;
    releaseHold(animator, false);
    victim.play(clips::one(clips::clipOf(escape)), clips::kIdle, AnimState::Attack, TargetState::Standing);
    if (damage > 0) {
        victim.hit(IncomingHit{.damage = damage, .attackAnim = escape, .attacker = victim.position(), .react = false});
    }
    animator.playPaired(clips::one(clips::clipOf(escape) + 1), victim.animator().anims(), clips::kGroundedIdle,
                        AnimState::Hold);
    m_victim.knockDown(input.nowMs, true);
    m_reacting = true;
}

void Fighter::releaseHold(HumanAnimator& animator, bool letGo) {
    m_held->setAttached(false);
    m_pair = PairStage::None;
    m_turnUpdates = 0;
    m_grabTurn = 0.0F;
    const bool mounted = m_held->state() == TargetState::Mounted;
    m_combat.release();
    if (letGo) {
        animator.playCombat(clips::one(id::kGrabLetGo), kAnimFightIdle, AnimState::Attack);
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
}

} // namespace coney::human
