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
#include "raycast/collision_mesh.h"

// The fighter's grabs the player holds: the intro and the alignment, the connecting clips, the snap to the hold and
// the attachment, the moves inside the hold, the tackle's mount, the stick turning the pair, and letting go.
// Research: docs/research/combat.md#grab-posing, docs/research/combat.md#grab-turn, docs/research/combat.md#grabbing

namespace coney::human {

namespace {

namespace id = combat::anim_id;

constexpr float kPi = std::numbers::pi_v<float>;
// The mount's slide and turn of the mounter onto the victim (`Human_AlignToVictimFacing`, 0x00277248).
constexpr float kMountSeatSeconds = 0.1F;

// The alignment's slide is dropped from this far, or above this speed (m/s).
constexpr float kAlignMaxSlide = 13.0F;
constexpr float kAlignMaxSpeed = 50.0F;

// The victim's escape clips from the front and the rear (the grabber plays each + 1).
constexpr int kEscapeFront = 100;
constexpr int kEscapeRear = 112;

// The wall throw's ray (docs/research/combat.md#throws): its height above the feet, its length past the far range,
// the steepest face normal · up that counts (cos 80°), and the head-on bound (normal · ray at most cos 135°).
constexpr float kWallThrowRayHeight = 1.4F;
constexpr float kWallThrowRayExtra = 0.2F;
constexpr float kWallThrowSteep = 0.17365F;
constexpr float kWallThrowHeadOn = -0.70711F;

} // namespace

void Fighter::dropLostHold(const FighterInput& input, HumanAnimator& animator) {
    // Compared as pointers only: a victim gone from the targets may no longer exist.
    const auto listed = [&input](const Holdable* victim) {
        return std::ranges::find(input.targets, static_cast<const Combatant*>(victim)) != input.targets.end();
    };
    if (m_lastThrown != nullptr && !listed(m_lastThrown)) {
        m_lastThrown = nullptr;
    }
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
    // A placement broke the pair from the victim's side: this side plays its reaction (244 from the mount, 138 or 106
    // from a grab or a mugging) and stands free. Otherwise the grabber stands in its fight idle; the victim, if it is
    // still there, is left as it is.
    std::optional<TargetState> broken;
    if (listed(m_held)) {
        broken = m_held->takeBrokenHold();
    }
    std::uint32_t reaction = 0;
    if (broken == TargetState::Mounted) {
        reaction = clips::kBreakMounter;
    } else if (broken.has_value()) {
        reaction = m_rear ? clips::kBreakRearGrabber : clips::kBreakFrontGrabber;
    }
    m_held = nullptr;
    m_pair = PairStage::None;
    m_turnUpdates = 0;
    m_grabTurn = 0.0F;
    m_rear = false;
    m_tacklePending = false;
    m_mountPending = false;
    m_seatUpdates = 0;
    m_combat.release();
    if (reaction != 0) {
        animator.playCombat(clips::one(reaction), kAnimFightIdle, AnimState::Attack);
        m_reacting = true;
    } else {
        animator.playCombat(clips::kNoClips, kAnimFightIdle, AnimState::Attack);
    }
}

void Fighter::breakPair() {
    // This human plays nothing: its link ends and whatever it plays goes on (the placement that called this decides).
    // 1. Holding someone (a grab, a mount, a mugging): the victim is unlinked and plays its side's reaction.
    if (m_held != nullptr) {
        Holdable& victim = *m_held;
        const bool mounted = victim.state() == TargetState::Mounted;
        victim.setAttached(false);
        if (!victim.health().depleted()) {
            if (mounted) {
                static constexpr std::array<std::uint32_t, 2> kUp{clips::kBreakMountedVictim, clips::kGroundedRise};
                victim.play(kUp, clips::kIdle, AnimState::Attack, TargetState::Standing);
            } else {
                victim.play(clips::one(m_rear ? clips::kBreakRearVictim : clips::kBreakFrontVictim), clips::kIdle,
                            AnimState::Attack, TargetState::Standing);
            }
        }
        m_held = nullptr;
        m_pair = PairStage::None;
        m_turnUpdates = 0;
        m_grabTurn = 0.0F;
        m_rear = false;
        m_combat.release();
    }
    // 2. A throw's link is cleared, with no clip.
    m_thrown = nullptr;
    // 3. Held or mounted: free at once; the grabber reads the broken hold on its next update and plays its reaction.
    if (m_holdState.has_value()) {
        m_brokenFrom = m_holdState;
        m_holdState.reset();
        m_holdAttached = false;
        m_holdUnkept = 0;
    }
    m_catch.reset();
    m_grabbed.reset();
    m_tacklePending = false;
    m_mountPending = false;
    m_seatUpdates = 0;
}

bool Fighter::inPair() const { return m_held != nullptr || m_holdState.has_value() || m_grabbed.has_value(); }

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
    const anim::AnimClip* introClip = animator.clip(intro);
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
    const anim::AnimClip* connecting = animator.clip(clip);
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
        m_lastThrown = m_held;
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
        // Nothing holds the pair during 118 / 119: each body moves by its own clip from where the hold left it, and
        // at 210 the mounter is moved onto the victim (seatMount(), docs/research/combat.md#mount).
        if (m_held != nullptr) {
            m_held->setAttached(false);
            m_pair = PairStage::Moving;
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

void Fighter::mountVictim(const HumanAnimator& animator) {
    // The victim goes down under the player (the attacker's clip, a paired task), mounted, by its own clip; at 210
    // the mounter is moved onto it, as after the grab's mount (seatMount()).
    m_tacklePending = false;
    m_held->playPaired(clips::one(clips::kTackleReact), animator.anims(), clips::kMountedIdle, AnimState::Hold,
                       TargetState::Mounted);
    m_held->setAttached(false);
    m_pair = PairStage::Moving;
    m_mountPending = true;
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
            m_seatUpdates = 0;
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
    // The mounter, never the victim, is moved: to the victim's position minus clip 210's offset turned by the
    // mounter's rotation, and turned to the victim's heading + 180 degrees, both over 0.1 s; the victim then lies
    // 0.120 m to his left and 0.032 m ahead. Attached once the slide is done (attachSeat()).
    m_mountPending = false;
    const anim::Vec3 offset = pairEventPoint(animator.clip(clips::kMountingIdle), kMountOffset);
    const anim::Vec3 along = anim::subtract(fromFrame(input.position, heading, offset), input.position);
    const anim::Vec3 stand = anim::subtract(m_held->position(), along);
    m_steer.clear();
    m_steer.turnToOver(heading, wrapAngle(m_held->heading() + kPi), kMountSeatSeconds);
    m_steer.moveToOver(input.position, stand, kMountSeatSeconds);
    m_seatUpdates = static_cast<int>(std::ceil((kMountSeatSeconds / input.stepSeconds) - 1e-4F));
}

void Fighter::attachSeat(const FighterInput& input, float heading) {
    // Where the slide left the pair: the victim is attached as it lies, so nothing jumps.
    const anim::Vec3 offset = toFrame(input.position, heading, m_held->position());
    snapAttach(input, heading, offset, wrapAngle(m_held->heading() - heading));
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

std::optional<Fighter::WallThrow> Fighter::wallThrow(const FighterInput& input) const {
    if (input.mesh == nullptr || m_ranges == nullptr) {
        return std::nullopt;
    }
    // The side the stick names, its body axis in the world (front +y, right +x, rear -y, left -x of the facing), and
    // the far range of its wall throw (155, 157, 159, 161).
    const combat::Side side = combat::sideOf(input.stick.angleDegrees());
    const auto index = static_cast<int>(side);
    const float axisHeading = wrapAngle(input.heading - (static_cast<float>(index) * kPi / 2.0F));
    const int wallClip = id::kThrow2Front + (2 * index);
    const float far = m_ranges->farRange(static_cast<std::size_t>(clips::clipOf(wallClip)));
    if (far <= 0.0F) {
        return std::nullopt;
    }
    const anim::Vec3 origin = anim::add(input.position, anim::Vec3{0.0F, 0.0F, kWallThrowRayHeight});
    // The stick's own direction in the world: its x to the right of the facing, its y ahead.
    const anim::Vec3 ahead = facing(input.heading);
    const anim::Vec3 right{std::cos(input.heading), std::sin(input.heading), 0.0F};
    const anim::Vec3 stick = anim::add(anim::scale(right, input.stick.x), anim::scale(ahead, input.stick.y));
    const float stickLength = std::hypot(stick.x, stick.y);
    const std::array<anim::Vec3, 2> directions{
        facing(axisHeading), stickLength > 1e-4F ? anim::scale(stick, 1.0F / stickLength) : facing(axisHeading)};
    for (const anim::Vec3& direction : directions) {
        const auto hit =
            input.mesh->rayCast(raycast::Ray{.origin = raycast::Vec3{origin.x, origin.y, origin.z},
                                             .direction = raycast::Vec3{direction.x, direction.y, direction.z},
                                             .length = far + kWallThrowRayExtra},
                                {}, 0);
        if (!hit.has_value()) {
            continue;
        }
        // Only a steep face counts; within the far range it is the wall throw (the first ray that hits decides).
        if (hit->normal.z >= kWallThrowSteep || hit->t > far) {
            return std::nullopt;
        }
        const anim::Vec3 normal{hit->normal.x, hit->normal.y, hit->normal.z};
        return WallThrow{.vector = anim::scale(direction, hit->t),
                         .normal = normal,
                         .headOn = anim::dot(normal, direction) <= kWallThrowHeadOn};
    }
    return std::nullopt;
}

void Fighter::alignWallThrow(const combat::CombatOutput& out, const FighterInput& input, float& heading) {
    const int animId = out.startAnim;
    const bool wall = animId >= id::kThrow2Front && animId <= id::kThrow2Left && (animId - id::kThrow2Front) % 2 == 0;
    if (!wall || !m_wallThrow.has_value() || !m_wallThrow->headOn || m_held == nullptr ||
        m_pair != PairStage::Attached) {
        return;
    }
    // The side's axis along the face's inward normal, the victim carried round at the hold's offset.
    const int index = (animId - id::kThrow2Front) / 2;
    const anim::Vec3 inward{-m_wallThrow->normal.x, -m_wallThrow->normal.y, 0.0F};
    if (std::hypot(inward.x, inward.y) < 1e-4F) {
        return;
    }
    heading = wrapAngle(headingOf(inward) + (static_cast<float>(index) * kPi / 2.0F));
    placeAttached(input.position, heading);
}

combat::GrabberState Fighter::grabberState() const {
    return combat::GrabberState{.power = m_combat.power().value(),
                                .powerMax = std::max(1, m_combat.power().maximum()),
                                .hurt = hurt(),
                                .raging = m_combat.rage().raging(),
                                .flag40 = hasFlag(flag::kUngrabbable),
                                .struggleDivisor = m_victim.powerClass().struggleDivisor};
}

void Fighter::applyHeldReport(const GrabbedReport& report, const FighterInput& input, HumanAnimator& animator) {
    if (m_held == nullptr || m_combat.mode() != combat::CombatMode::Grabbing) {
        return;
    }
    // The struggle's cost, spent even when no move could start; at 0 the grab's own power-out ends it.
    if (report.grabberPowerCost > 0 && !m_combat.power().unlimited()) {
        m_combat.power().set(m_combat.power().value() - report.grabberPowerCost);
    }
    switch (report.action) {
    case combat::GrabbedAction::Struggle:
    case combat::GrabbedAction::StrikeBack: {
        // The move's damage first: one that empties the grabber's health breaks the grab.
        if (report.grabberDamage > 0 && m_health.apply(report.grabberDamage) > 0 && m_health.depleted()) {
            releaseHold(animator, false);
            return;
        }
        // Both play the pair from the victim's set (the grabber its id + 1), then their holds again.
        Holdable& victim = *m_held;
        animator.playPaired(clips::one(clips::clipOf(report.grabberClip)), victim.anims(),
                            m_rear ? clips::kGrabRearHold : clips::kGrabHold, AnimState::Hold);
        victim.play(clips::one(clips::clipOf(report.victimClip)), m_rear ? clips::kGrabRearHeld : clips::kGrabHeld,
                    AnimState::Hold, TargetState::Held);
        break;
    }
    case combat::GrabbedAction::Escape:
        victimEscapes(input, animator);
        break;
    case combat::GrabbedAction::None:
    case combat::GrabbedAction::Reversal:
        break;
    }
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
    m_seatUpdates = 0;
}

} // namespace coney::human
