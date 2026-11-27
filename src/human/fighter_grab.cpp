// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/fighter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <span>

#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "combat/lock_on.h"
#include "combat/reactions.h"
#include "combat/tackled.h"
#include "core/log.h"
#include "human/fighter_clips.h"
#include "human/pair_placement.h"
#include "raycast/collision_mesh.h"

// The fighter's grabs the player holds: the intro and the alignment, the connecting clips, the snap to the hold and
// the attachment, the moves inside the hold, the tackle's mount, the stick turning the pair, and letting go.
// Research: docs/research/combat-grabs.md#grab-posing, docs/research/combat-grabs.md#grab-turn,
// docs/research/combat-grabs.md#grabbing

namespace coney::human {

namespace {

namespace id = combat::anim_id;

constexpr float kPi = std::numbers::pi_v<float>;
// The mount's slide and turn of the mounter onto the victim (`Human_AlignToVictimFacing`, 0x00277248).
constexpr float kMountSeatSeconds = 0.1F;
// A front hold's plain throw turns the pair onto the stick over this long (`Human_TurnToOver`, `Human_MoveToOver`
// from `Player_Throw`, docs/research/combat-throws.md#throw-direction): 5 updates.
constexpr float kThrowTurnSeconds = 1.0F / 6.0F;

// The attached pair's move (`Human_MoveAttached`, docs/research/combat-grabs.md#grab-posing): a body is settled when it
// falls short of its goal by a squared length under this (0.001 m², about 3 cm), and the victim and the grabber are
// swept in turn for at most this many rounds.
constexpr float kAttachSettled = 0.001F;
constexpr int kAttachRounds = 3;

// The alignment's slide is dropped from this far, or above this speed (m/s).
constexpr float kAlignMaxSlide = 13.0F;
constexpr float kAlignMaxSpeed = 50.0F;

// The victim's escape clips from the front and the rear (the grabber plays each + 1).
constexpr int kEscapeFront = 100;
constexpr int kEscapeRear = 112;

// The wall throw's ray (docs/research/combat-throws.md#throws): its height above the feet, its length past the far
// range, the steepest face normal · up that counts (cos 80°), and the head-on bound (normal · ray at most cos 135°).
constexpr float kWallThrowRayHeight = 1.4F;
constexpr float kWallThrowRayExtra = 0.2F;
constexpr float kWallThrowSteep = 0.17365F;
constexpr float kWallThrowHeadOn = -0.70711F;

} // namespace

void Fighter::dropLostHold(const FighterInput& input, HumanAnimator& animator) {
    // Compared as pointers only: a victim gone from the targets may no longer exist.
    const auto listed = [&input](const Holdable* victim) {
        return targetListed(input, static_cast<const Combatant*>(victim));
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
    m_spinThrow = id::kNone;
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
        m_spinThrow = id::kNone;
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
    // 4. A tandem it plays a part in ends for it (its clips play on, as the placement decides).
    m_tandemRole = TandemRole::None;
    m_tandemRequest.reset();
    m_rearHeld = false;
}

bool Fighter::inPair() const {
    return m_held != nullptr || m_holdState.has_value() || m_grabbed.has_value() || m_tandemRole != TandemRole::None;
}

void Fighter::startHold(Holdable& victim, const FighterInput& input, float& heading, bool tackle, int connect,
                        HumanAnimator& animator) {
    const anim::Vec3 to = anim::subtract(victim.position(), input.position);
    const float toVictim = std::hypot(to.x, to.y) > 1e-4F ? headingOf(to) : heading;
    m_held = &victim;
    m_target = &victim;
    m_rear = false;
    m_pair = PairStage::None;
    m_spinThrow = id::kNone;
    // The grab's base id (record +0xb4), which the connect's and the pick-up's ends score: `Player_GrabOrTackle`
    // passes `Tackle_Start` 5 (0x00284aac) and `Grab_Start` 0x48 (0x00284c6c), a strong grapple passes its own.
    m_grabBase = tackle ? static_cast<int>(clips::kTackleHit) : connect;
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
    // these updates is its counter, docs/research/ai-fights.md#block).
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
    // A rear hold's throw waits for its spin: nothing else follows the clips until the throw's own begins.
    if (m_spinThrow != id::kNone) {
        if (clip == clips::clipOf(m_spinThrow)) {
            throwAfterSpin(animator);
        }
        return;
    }
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
    // grab is now on (a spin set the side as it started). At a connecting clip's or the pick-up's end the gate may
    // release the grab instead.
    const bool connectEnded = m_lastClip == connectClip();
    const auto pickUp = clips::clipOf(combat::anim_id::kMountPickup);
    const bool pickedUp = m_lastClip == pickUp;
    if (clip != m_lastClip && m_pair == PairStage::Moving && (connectEnded || pickedUp || clips::isSpin(m_lastClip))) {
        const anim::Vec3 offset = pairPoint(m_ranges, m_rear ? clips::kGrabRearHold : clips::kGrabHold,
                                            m_rear ? kRearHoldOffset : kFrontHoldOffset);
        // The connect's end (`Grab_ConnectEnd` `0x0026bad8`), which `Mount_ReturnToFrontGrab` `0x00271808` hangs on
        // the pick-up 248 too: with power left the victim takes the ended clip's damage-table value (a plain grab's 72
        // deals none) before the gate, so a victim out of reach is hurt and let go with nothing scored; otherwise the
        // snap, and only then the move scores (`Grab_ScoreMove` `0x00264fa0`) as the grab's base id, not the clip.
        // **Stand-in** (#684): the gate does not test for a clear line (`Human_IsClearToTarget` `0x0021c240`, not
        // ported). After the snap the original lets a victim in state 0x20000 escape at once, and a victim pressing
        // circle escapes or reverses the grab on a coin flip, scoring nothing; Coney runs neither, as its held human
        // reports its commands only once attached (heldInGrab()) and `Grab_Reverse` is not ported, so the move always
        // scores. With no power the original skips the gate and the snap; Coney gates and snaps, since a victim left
        // unattached would make every move in the hold refuse (victimInPlace()).
        const std::uint32_t ended = pickedUp ? pickUp : connectClip();
        LandedGrapple landed;
        if (pickedUp || (connectEnded && m_connect != static_cast<int>(clips::kGrabFrontEnd))) {
            landed = landGrapple(input, ended);
        }
        if ((connectEnded || pickedUp) && !holdGatePasses(input.position, m_held->position(), anim::length(offset))) {
            releaseHold(animator, false);
            return;
        }
        snapAttach(input, heading, offset, m_rear ? 0.0F : kPi);
        scoreGrapple(input, static_cast<std::uint32_t>(m_grabBase), landed);
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
    // The grab-block flag of the grab's side on either human (`Grab_IntroEnd`): the grab does not go on.
    const std::uint64_t block = m_rear ? flag::kBlockRearGrab : flag::kBlockFrontGrab;
    const bool blocked = hasFlag(block) || (m_held->flags() & block) != 0;
    if (!align.inRange || blocked || m_held->state() != TargetState::Standing) {
        // Too far, blocked, or no longer on its feet (a human the intro left free may have gone down): the grab
        // fails, the miss plays on; the victim was never stopped.
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
    // `Grab_Connect` builds both holds under the connecting clips in this update, and building them fires their ids'
    // animation callbacks (82 / 83, 84 / 85 from the rear): level99's PlayerState runs now, the length of the
    // connecting clip before the strong grapple's hit is scored with 82, and not again when the holds begin
    // (docs/research/characters-animation.md#anim-callbacks). **Coney's reading**: the four-clip mix's second clips (86
    // / 87) are not reported; Coney's hold plays the one clip.
    animator.announceFollowOn();
    m_held->announceFollowOn();
    m_pair = PairStage::Moving;
}

Fighter::LandedGrapple Fighter::landGrapple(const FighterInput& input, std::uint32_t clip) {
    // With power left, the victim takes the damage table's value of the clip that ended (`Grab_ConnectEnd`). The hit
    // is reported as the hold's id, which is playing when the victim applies it: the tutorial waits for 82 or 84.
    // Whether it may earn rage is read before the hit (`Hit_Score` gives none on a man already out of health).
    if (m_held == nullptr || m_combat.power().value() <= 0) {
        return {};
    }
    const auto strike = static_cast<int>(clip);
    const int damage = quarteredFor(*m_held, m_ranges != nullptr ? combat::strikeDamage(*m_ranges, strike) : 0);
    const bool scores = scoresRageOn(*m_held);
    const auto hold = static_cast<int>(m_rear ? clips::kGrabRearHold : clips::kGrabHold);
    m_held->hit(IncomingHit{.damage = damage,
                            .attackAnim = hold,
                            .attacker = input.position,
                            .react = false,
                            .ignoresArmour = m_player || hasFlag(flag::kIncreasedReact),
                            .attackerFlag200000 = hasFlag(flag::kIncreasedReact),
                            .attackerIsPlayer = m_player,
                            .attackerName = logName()});
    ++m_hitsLanded;
    m_damageDealt += damage;
    if (m_player) {
        m_strikes.push_back(hold);
    }
    return {.landed = true, .scoresRage = scores};
}

void Fighter::scoreGrapple(const FighterInput& input, std::uint32_t clip, LandedGrapple landed) {
    // The move scores as the grab's base id (`Grab_ScoreMove` with record +0xb4), once the victim is in the hold.
    if (!landed.landed) {
        return;
    }
    const auto strike = static_cast<int>(clip);
    if (landed.scoresRage) {
        earnRage(strike, input.nowMs, false);
    }
    m_repeat.note(strike, input.nowMs);
}

void Fighter::snapAttach(const FighterInput& input, float heading, anim::Vec3 offset, float turn) {
    // The hold's offset and turn, stored in the grabber's frame (`Pair_SnapAttach` builds the world place and
    // `Pair_Attach` turns it back into the same offset and relative rotation; Coney keeps the frame throughout).
    m_holdOffset = offset;
    m_holdTurn = turn;
    // The victim's own movement is held: the grabber moves it from now on.
    m_held->setAttached(true);
    // **Coney stand-in**: every grabber takes the grabbing movement once attached (moveGrab(), read from its intent);
    // the original switches only a grabber with a player's controls to `Human_MoveGrabbing`.
    m_pair = PairStage::Attached;
    m_turnUpdates = 0;
    // **Coney stand-in**: the original only installs `Human_MoveAttached` as the victim's movement, which runs on the
    // victim's next movement update; Coney's grabber makes that move now and on each of its updates.
    placeAttached(input, heading);
}

void Fighter::placeAttached(const FighterInput& input, float heading) {
    // **Coney stand-in**: with its grabber gone the original's victim gets its own movement back and runs it; Coney's
    // grabber calls this, so it always has one, and a victim whose grabber stops updating frees itself
    // (freeFromLostGrabber()).
    // Where the pair stands now, for a pair the rounds cannot settle. The grabber is read from itself, not from the
    // update's input: a snap earlier in this update may have swept it already.
    const anim::Vec3 grabberStart = input.self != nullptr ? input.self->position() : input.position;
    const anim::Vec3 victimStart = m_held->position();
    const float victimHeading = m_held->heading();
    // 1. The victim's move: from where it stands to the grabber's transform times the stored offset.
    anim::Vec3 victimAt = victimStart;
    anim::Vec3 grabberAt = grabberStart;
    anim::Vec3 move = anim::subtract(fromFrame(grabberStart, heading, m_holdOffset), victimStart);
    int rounds = 0;
    while (rounds < kAttachRounds) {
        // 2. The victim swept by the move; settled when it falls short by less than the threshold.
        const anim::Vec3 victimEnd = m_held->sweepAttached(input.mesh, move);
        const anim::Vec3 victimShort = anim::subtract(victimEnd, anim::add(victimAt, move));
        if (anim::dot(victimShort, victimShort) < kAttachSettled || input.self == nullptr) {
            break;
        }
        // 3. The grabber swept by what the victim fell short by (back from the obstacle); settled likewise.
        victimAt = victimEnd;
        const anim::Vec3 grabberEnd = input.self->sweepAttached(input.mesh, victimShort);
        const anim::Vec3 grabberShort = anim::subtract(grabberEnd, anim::add(grabberAt, victimShort));
        if (anim::dot(grabberShort, grabberShort) < kAttachSettled) {
            break;
        }
        // 4. Another round: the victim takes what the grabber fell short by.
        ++rounds;
        grabberAt = grabberEnd;
        move = grabberShort;
    }
    if (rounds == kAttachRounds) {
        // 5. Unsettled after three rounds: both go back where they stood, the grabber stopped.
        // **Coney stand-in**: the grabber goes back to where this call found it, keeping the heading this update has
        // given him (`heading`), where the original restores the whole transform (position and rotation) its body
        // cached before the update's move; and its stop (Human::place()) also ends its locomotion's turn.
        m_held->place(victimStart, victimHeading);
        input.self->place(grabberStart, heading);
        return;
    }
    // 6. Settled: the victim stands where the sweeps left it, turned to the hold's relative rotation. **Coney
    // stand-in**: it is not handed the grabber's velocity, as Coney's held human has its movement held and no velocity
    // of its own.
    m_held->place(m_held->position(), heading + m_holdTurn);
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
        if (rearThrowPath()) {
            // From the rear hold the spin to the front (80, the victim 81) plays first, then the throw; the victim is
            // let go when the throw's clip begins (throwAfterSpin()). Nothing turns the pair, so the body flies along
            // the side's axis of the facing at the press (`Grab_StartPairedMove`,
            // docs/research/combat-throws.md#throw-direction).
            const std::array<std::uint32_t, 2> moves{id::kGrabSpinToFront, clips::clipOf(out.startAnim)};
            animator.playCombat(moves, kAnimFightIdle, AnimState::Attack, clips::kPairFade, clips::kAttackHolds);
            m_held->playPaired(clips::one(id::kGrabSpinToFront + 1), animator.anims(), clips::kGrabHeld,
                               AnimState::Hold, TargetState::Held);
            m_spinThrow = out.startAnim;
            m_thrown = m_held;
            m_lastThrown = m_held;
            break;
        }
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
        // at 210 the mounter is moved onto the victim (seatMount(), docs/research/combat-grabs.md#mount).
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

bool Fighter::startGroundedMount(Holdable& victim, const FighterInput& input, float& heading, HumanAnimator& animator) {
    // The clip by the side of him the mounter stands on, refused beyond its far range (× 1.25 for a player).
    const combat::Side side = combat::victimSide(victim.position(), victim.heading(), input.position);
    const int clip = combat::kGroundedMountFront + static_cast<int>(side);
    const float listed = m_ranges != nullptr ? m_ranges->farRange(static_cast<std::size_t>(clip)) : 0.0F;
    const float far = m_player ? listed * kPlayerFarScale : listed;
    const anim::Vec3 to = anim::subtract(victim.position(), input.position);
    if (std::hypot(to.x, to.y) > far) {
        return false;
    }
    // The pair is linked at once: the mounter in the mount, the victim mounted (and up from the ground). Coney
    // stand-in: the held-flag tests are the search's (mountableOnGround()), the 0.1 s steer is the seat at 210, and
    // no move style, push sphere or warning is set.
    if (std::hypot(to.x, to.y) > 1e-4F) {
        heading = headingOf(to);
    }
    m_held = &victim;
    m_target = &victim;
    m_rear = false;
    m_spinThrow = id::kNone;
    m_combat.startMounting();
    animator.playCombat(clips::one(static_cast<std::uint32_t>(clip)), clips::kMountingIdle, AnimState::Hold,
                        kCombatFade, clips::kGrabHolds);
    victim.play(clips::one(static_cast<std::uint32_t>(combat::kGroundedMountReact)), clips::kMountedIdle,
                AnimState::Hold, TargetState::Mounted);
    // Nothing holds the pair during the clips: at 210 the mounter is seated onto him (seatMount()).
    victim.setAttached(false);
    m_pair = PairStage::Moving;
    m_mountPending = true;
    m_groundMountLanding = true;
    logging::info(logging::Category::Combat, std::format("grounded mount: {} with {}", logName(), clip));
    return true;
}

void Fighter::endGroundedMountClip(const FighterInput& input, HumanAnimator& animator) {
    m_groundMountLanding = false;
    // Coney stand-in: no reach, height or clear-line get-off (the seat at 210 places the pair); the move is not scored
    // and +0xb8 is not cleared.
    // The victim's chance as the clip lands: its command and stick through its record, and its auto-escape flag.
    const Holdable& victim = *m_held;
    const combat::MountEnd end =
        combat::mountClipEnd(combat::MountEndInput{.command = victim.mountCommand(),
                                                   .stickMagnitude = victim.mountStick(),
                                                   .autoEscape = (victim.flags() & flag::kAutoEscape) != 0},
                             m_grabbedRandom);
    if (end == combat::MountEnd::GetOff) {
        getOffMount(animator);
    } else if (end == combat::MountEnd::Reverse) {
        GrabbedReport reversal;
        reversal.mount = combat::TackledAction::Reversal;
        applyMountReport(reversal, input, animator);
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
        // A pick-up in the mount's 0.1 s seat (an AI's circle comes two updates into 210: 210 at 127, 248 at 129 in
        // docs/research/ai/index.md#ai-grab-runtime) leaves the seat unattached, so the pick-up's end snaps the victim
        // into the hold. **Coney's reading**: the seat's attach is Coney's (attachSeat()); the original's pick-up
        // (`0x00271808`) is not traced for a slide still running.
        m_seatUpdates = 0;
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
    logging::info(logging::Category::Combat, std::format("knock-down: {} by his victim's escape", logName()));
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
    // The heading: the face's inward normal (the hit normal turned round) plus the side's quarter turns, so the
    // throw's side faces the wall square. **Coney stand-in**: a face with no horizontal part leaves the pair as it is.
    const int index = (animId - id::kThrow2Front) / 2;
    const anim::Vec3 inward{-m_wallThrow->normal.x, -m_wallThrow->normal.y, 0.0F};
    if (std::hypot(inward.x, inward.y) < 1e-4F) {
        return;
    }
    // **Coney stand-in**: the original also moves the grabber over the same 0.01 s onto the wall point less the side
    // clip's offset times its reach; Coney's grabber stays where he stands and only turns, at once.
    heading = wrapAngle(headingOf(inward) + (static_cast<float>(index) * kPi / 2.0F));
    // The victim carried round to the stored offset from the grabber's new transform. **Coney stand-in**: the original
    // turns and moves it there over 0.01 s from the grabber's goal; Coney sweeps it now from where the grabber stands.
    placeAttached(input, heading);
}

bool Fighter::rearThrowPath() const { return m_held != nullptr && m_rear && !hasFlag(flag::kBlockFrontGrab); }

int Fighter::spinToFrontUpdates(const HumanAnimator& animator, float stepSeconds) const {
    const auto spin = static_cast<std::uint32_t>(id::kGrabSpinToFront);
    const anim::AnimClip* clip = animator.clip(spin);
    const float rate = animator.anims().rate(spin);
    if (clip == nullptr || rate <= 0.0F || stepSeconds <= 0.0F) {
        return 1;
    }
    // **Coney's reading**: the throw's clip starts on the update after the spin's playing time has run out.
    return std::max(1, static_cast<int>(std::ceil((clip->duration / rate / stepSeconds) - 1e-4F)));
}

void Fighter::turnThrow(Holdable& victim, int animId, const FighterInput& input, float heading) {
    // Only the plain throws (147-153): a wall throw aims at the wall (alignWallThrow()).
    if (animId < id::kThrow1Front || animId > id::kThrow1Left || (animId - id::kThrow1Front) % 2 != 0) {
        return;
    }
    // The side's body axis onto the stick: the thrower's wanted heading is the stick's heading (the press's
    // camera-turned stick, unsnapped) + side x 90 degrees. The stick is in the frame of the facing at the update's
    // start.
    const int side = (animId - id::kThrow1Front) / 2;
    const float stickHeading = wrapAngle(input.heading - (input.stick.angleDegrees() * kPi / 180.0F));
    const float wanted = wrapAngle(stickHeading + (static_cast<float>(side) * kPi / 2.0F));
    m_steer.clear();
    m_steer.turnToOver(heading, wanted, kThrowTurnSeconds);
    // The victim is carried round him on the same turn: its offset in his frame kept, turned to face him.
    const anim::Vec3 offset = toFrame(input.position, heading, victim.position());
    victim.carryOver(fromFrame(input.position, wanted, offset), wrapAngle(wanted + kPi), kThrowTurnSeconds);
}

void Fighter::throwAfterSpin(const HumanAnimator& animator) {
    // The victim plays its side of the throw (the next id) from the grabber's set and is let go, as a front throw's.
    Holdable& victim = *m_held;
    victim.playPaired(clips::one(clips::clipOf(m_spinThrow) + 1), animator.anims(), clips::kGroundedIdle,
                      AnimState::Hold, TargetState::Grounded);
    victim.setAttached(false);
    m_spinThrow = id::kNone;
    m_pair = PairStage::None;
    m_held = nullptr;
    m_rear = false;
}

void Fighter::carryOver(anim::Vec3 position, float heading, anim::Vec3 goal, float targetHeading, float seconds) {
    m_steer.clear();
    m_steer.turnToOver(heading, targetHeading, seconds);
    m_steer.moveToOver(position, goal, seconds);
}

combat::GrabberState Fighter::grabberState() const {
    return combat::GrabberState{.power = m_combat.power().value(),
                                .powerMax = std::max(1, m_combat.power().maximum()),
                                .hurt = hurt(),
                                .raging = m_combat.rage().raging(),
                                .flag40 = hasFlag(flag::kUngrabbable),
                                .struggleDivisor = m_victim.powerClass().struggleDivisor,
                                .scoresRage = !(m_bossClass && hasFlag(flag::kGod))};
}

void Fighter::applyHeldReport(const GrabbedReport& report, const FighterInput& input, HumanAnimator& animator) {
    if (m_held != nullptr && m_combat.mode() == combat::CombatMode::Tackling) {
        applyMountReport(report, input, animator);
        return;
    }
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

void Fighter::applyMountReport(const GrabbedReport& report, const FighterInput& input, HumanAnimator& animator) {
    // The struggle's drain, spent even when no new 250 starts (a tireless mounter spends nothing).
    if (report.grabberPowerCost > 0 && !m_combat.power().unlimited()) {
        m_combat.power().set(std::max(0, m_combat.power().value() - report.grabberPowerCost));
    }
    switch (report.mount) {
    case combat::TackledAction::Struggle: {
        // The victim's damage of 250 to the mounter first; one that empties its health ends the mount. Coney stand-in:
        // as a let-go (the victim rises with 199), not Grab_BreakOnHit and the death reaction; flag 0x4000 not read.
        if (report.grabberDamage > 0 && m_health.apply(report.grabberDamage) > 0 && m_health.depleted()) {
            releaseHold(animator, false);
            return;
        }
        // The victim 250 and the mounter 251 from the victim's set, both back to the mount's idles.
        Holdable& victim = *m_held;
        animator.playPaired(clips::one(clips::clipOf(report.grabberClip)), victim.anims(), clips::kMountingIdle,
                            AnimState::Hold);
        victim.play(clips::one(clips::clipOf(report.victimClip)), clips::kMountedIdle, AnimState::Hold,
                    TargetState::Mounted);
        break;
    }
    case combat::TackledAction::Break:
        getOffMount(animator);
        break;
    case combat::TackledAction::Reversal: {
        // Out of the mount on this side; the victim takes it the other way round and plays both clips.
        if (input.self == nullptr) {
            break;
        }
        Holdable& victim = *m_held;
        victim.setAttached(false);
        m_held = nullptr;
        m_pair = PairStage::None;
        m_tacklePending = false;
        m_mountPending = false;
        m_seatUpdates = 0;
        m_combat.release();
        victim.takeReversedMount(*input.self);
        break;
    }
    case combat::TackledAction::None:
        break;
    }
}

void Fighter::getOffMount(HumanAnimator& animator) {
    // Nothing while the mounter's own grounded mount still plays (its 0x2000).
    const std::uint32_t playing = animator.animId();
    if (playing >= static_cast<std::uint32_t>(combat::kGroundedMountFront) &&
        playing <= static_cast<std::uint32_t>(combat::kGroundedMountLeft)) {
        return;
    }
    // Coney stand-in: no move styles, push spheres or held 0x2000; the end callbacks' Tackle_End is this release.
    Holdable& victim = *m_held;
    victim.setAttached(false);
    m_held = nullptr;
    m_pair = PairStage::None;
    m_tacklePending = false;
    m_mountPending = false;
    m_seatUpdates = 0;
    m_combat.release();
    // The victim breaks out with 246 and stands in its fight idle; the mounter plays 247 from its set, then its own.
    const auto breakOut = static_cast<std::uint32_t>(combat::kMountBreak);
    if (!victim.health().depleted()) {
        victim.play(clips::one(breakOut), kAnimFightIdle, AnimState::Attack, TargetState::Standing);
    }
    animator.playPaired(clips::one(breakOut + 1), victim.anims(), kAnimFightIdle, AnimState::Attack);
    logging::info(logging::Category::Combat, std::format("mount broken: {} is off", logName()));
}

void Fighter::endPowerMove() {
    m_held->setAttached(false);
    m_pair = PairStage::None;
    m_turnUpdates = 0;
    m_grabTurn = 0.0F;
    m_spinThrow = id::kNone;
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
    // A rear hold's throw cut short in its spin (a hit on the grabber) throws no one.
    if (m_spinThrow != id::kNone) {
        m_thrown = nullptr;
        m_spinThrow = id::kNone;
    }
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
