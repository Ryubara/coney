// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/fighter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "animation/anim_task.h"
#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "combat/grab.h"
#include "combat/lock_on.h"
#include "combat/rage_awards.h"
#include "combat/reactions.h"
#include "core/pad.h"
#include "human/fighter_clips.h"

namespace coney::human {

namespace {

namespace id = combat::anim_id;

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;

// Whether `animId` is a moving attack, after which the run may go on.
bool isMovingAttack(int animId) {
    return animId == id::kRunningAttackCharge || animId == id::kRunningAttackDive || animId == id::kAttackFromRun;
}

// The horizontal distance between two points.
float flatDistance(const anim::Vec3& a, const anim::Vec3& b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

Fighter::Fighter(const combat::AnimRangeList* ranges, std::uint32_t seed, const FighterProfile& profile)
    : m_ranges(ranges), m_player(profile.player), m_flags(profile.player ? flag::kPlayerFlags : 0),
      m_combat(ranges, 0, seed), m_health(profile.health > 0 ? profile.health : kPlayerHealth),
      m_victim(profile.powerClass, seed), m_grabbedRandom(seed + 1U) {}

bool Fighter::holdsMovement(const HumanAnimator& animator) const {
    return m_combat.blocking() || m_combat.mode() != combat::CombatMode::Free || grabbed() || m_holdState.has_value() ||
           helpless(animator);
}

anim::Vec3 Fighter::takeSlide() {
    if (m_slideUpdates <= 0) {
        return {};
    }
    --m_slideUpdates;
    return m_slide;
}

float Fighter::reachOf(int animId) const {
    const float far = m_ranges != nullptr && animId >= 0 ? m_ranges->farRange(static_cast<std::size_t>(animId)) : 0.0F;
    return far > 0.0F ? far : kDefaultStrikeReach;
}

Combatant* Fighter::inFront(const FighterInput& input, float range, bool grounded) const {
    Combatant* best = nullptr;
    float bestDistance = range;
    const anim::Vec3 ahead = facing(input.heading);
    for (Combatant* target : input.targets) {
        const bool fightable =
            target->state() == TargetState::Standing || (grounded && target->state() == TargetState::Grounded);
        if (!fightable) {
            continue;
        }
        const anim::Vec3 to = anim::subtract(target->position(), input.position);
        const float distance = std::hypot(to.x, to.y);
        if (distance <= bestDistance && (to.x * ahead.x) + (to.y * ahead.y) >= 0.0F) {
            best = target;
            bestDistance = distance;
        }
    }
    return best;
}

Combatant* Fighter::pickTarget(const FighterInput& input, float range) {
    // The heading searched along: the stick's when it is pushed, else the facing. The stick is in the facing frame,
    // its angle positive to the right, and headings grow to the left.
    const float along =
        input.stick.magnitude() > kPickStick ? input.heading - (input.stick.angleDegrees() * kDegrees) : input.heading;
    // The nearest standing target within `reach` (and `cone` degrees of the heading, when given).
    const auto nearest = [&](float reach, float cone) -> Combatant* {
        Combatant* best = nullptr;
        float bestDistance = reach;
        for (Combatant* target : input.targets) {
            if (target->state() != TargetState::Standing || target->health().depleted() || !target->targetable() ||
                std::fabs(target->position().z - input.position.z) > kPickHeight) {
                continue;
            }
            const anim::Vec3 to = anim::subtract(target->position(), input.position);
            const float distance = std::hypot(to.x, to.y);
            const float off = std::fabs(wrapAngle(headingOf(to) - along));
            if (distance <= bestDistance && (cone <= 0.0F || off <= cone * kDegrees)) {
                best = target;
                bestDistance = distance;
            }
        }
        return best;
    };
    // **Coney choice**: the third pass (humans × 0.7 within 135°) never finds one the second (× 0.9 at any angle)
    // missed, so it is left out.
    if (Combatant* found = nearest(range * kPickConeScale, kPickConeDegrees); found != nullptr) {
        return found;
    }
    return nearest(range * kPickAnyScale, 0.0F);
}

const Combatant* Fighter::lockTarget() const {
    return combat::lockedOn(combat::combatTuning(), m_target != nullptr, m_l1Held) ? m_target : nullptr;
}

void Fighter::update(const FighterInput& input, HumanAnimator& animator, float& heading) {
    m_strikes.clear();
    const combat::CombatTuning& tuning = combat::combatTuning();
    const combat::CombatMode before = m_combat.mode();
    m_report = GrabbedReport{};
    m_reactionShake.reset();
    m_rageStarted = false;
    m_l1Held = (input.buttons & pad::kL1) != 0;
    // The flags the meters follow: a locked rage meter, tireless power. A demi-god at or below its health floor is a
    // god from now on (0x00256f28).
    m_combat.rage().setLocked(hasFlag(flag::kRageLocked), input.nowMs);
    m_combat.power().setUnlimited(hasFlag(flag::kTireless));
    if (hasFlag(flag::kDemiGod) && m_health.fraction() <= tuning.healthFloor) {
        setFlag(flag::kGod, true);
    }
    // The power meter's maximum follows the hurt state; the throw bonus lasts only while grabbing from the front or
    // throwing.
    m_combat.power().setHurt(hurt(), m_victim.powerClass().hurtPowerFactor);
    const bool frontGrab = m_combat.mode() == combat::CombatMode::Grabbing && !m_rear;
    if (!frontGrab && !clips::isThrow(static_cast<int>(animator.animId()))) {
        m_repeat.resetBonus();
    }
    // 1. The victim's side first, as the original applies the pending damage before the actions: a grab caught on
    // the player, a warning, the update's hit, then the reaction's timers.
    dropLostHold(input, animator);
    if (m_catch.has_value()) {
        startGrabbed(input, animator);
    }
    takeNotice(input, animator);
    takePending(input, animator);
    const bool cannotAct = stepVictim(input, animator) || grabbed() || m_holdState.has_value();
    if (cannotAct) {
        // Only the meters run; a held player struggles.
        m_last = m_combat.update(combatInput(input, animator, true), tuning);
        if (grabbed()) {
            updateGrabbed(input, animator);
        }
        noteClip(animator);
        return;
    }

    // 2. A grab's alignment turns, then its pair's moments as the grabber's clips change.
    stepAlignment(heading);
    followPairClips(input, animator, heading);

    // 3. The duck's counter takes a square or cross in its window; then the dispatcher decides, and the fighter plays
    // what it decided.
    combat::CombatInput in = combatInput(input, animator, false);
    if (duckCounter(input, animator)) {
        in.command = combat::command::kNone;
    }
    const combat::CombatOutput out = m_combat.update(in, tuning);
    m_last = out;
    playDecisions(out, before, input, animator, heading);

    // 4. The tackle's hit, then the attack's.
    if (m_tacklePending && m_held != nullptr &&
        (animator.animId() == clips::kTackleHit || animator.animId() == clips::kMountingIdle)) {
        mountVictim(input, animator, heading);
    }
    if (m_mountPending && m_held != nullptr && animator.animId() == clips::kMountingIdle) {
        seatMount(input, animator, heading);
    }
    if (out.hitAnim != id::kNone) {
        landHit(out.hitAnim, out.hitDamage, input);
    }

    // 5. A hold ends when the victim has no health left, and a tackle when the power meter is empty (**Coney's
    // choice**: the research ends a grab at 0 power, which the dispatcher does; the tackle is taken to end alike).
    const combat::CombatMode mode = m_combat.mode();
    const bool holding = mode == combat::CombatMode::Grabbing || mode == combat::CombatMode::Tackling ||
                         mode == combat::CombatMode::Mugging;
    if (holding && m_held != nullptr &&
        (m_held->health().depleted() || (mode == combat::CombatMode::Tackling && m_combat.power().value() == 0))) {
        releaseHold(animator, false);
    }
    // 6. A spin the dispatcher started lets go of the offset; an attached victim follows the grabber: its transform
    // times the stored offset. Then the target is kept or dropped.
    detachForSpin(animator);
    if (m_pair == PairStage::Attached && m_held != nullptr) {
        placeAttached(input.position, heading);
    }
    trackTarget(FighterInput{.command = input.command,
                             .buttons = input.buttons,
                             .stick = input.stick,
                             .padStick = input.padStick,
                             .gait = input.gait,
                             .position = input.position,
                             .heading = heading,
                             .nowMs = input.nowMs,
                             .targets = input.targets,
                             .stepSeconds = input.stepSeconds});
    noteClip(animator);
}

void Fighter::noteClip(const HumanAnimator& animator) {
    m_lastClip = animator.animId();
    m_clipSeen = m_lastClip;
    const anim::AnimTask* top = animator.tasks().top();
    m_clipTimeSeen = top != nullptr ? top->time() : 0.0F;
}

combat::CombatInput Fighter::combatInput(const FighterInput& input, const HumanAnimator& animator, bool helpless) {
    const combat::CombatTuning& tuning = combat::combatTuning();
    combat::CombatInput in;
    in.command = input.command;
    in.buttons = input.buttons;
    in.stick = input.stick;
    in.padStick = input.padStick;
    in.gait = input.gait;
    in.nowMs = input.nowMs;
    in.helpless = helpless;
    if (helpless) {
        return in;
    }
    // Square's target, and for circle the nearest target in its search.
    const Combatant* front = inFront(input, reachOf(id::kAttackS1), true);
    in.target = front != nullptr && front->state() == TargetState::Grounded ? combat::TargetKind::Grounded
                                                                            : combat::TargetKind::None;
    m_candidate = nullptr;
    if (input.command == combat::command::kCircleTapped || input.command == combat::command::kCircleHeld) {
        const combat::GrabKind kind =
            input.command == combat::command::kCircleHeld ? combat::GrabKind::Tackle : combat::GrabKind::Grab;
        const float range = m_ranges != nullptr ? combat::grabSearchRange(*m_ranges, kind, tuning.grabSearchScale)
                                                : (kind == combat::GrabKind::Tackle ? 3.75F : 3.12F);
        // Any human that can be held (Combatant::holdable(): a passive target, or a human without flag 0x40) standing
        // with health left.
        std::vector<combat::TargetCandidate> candidates;
        candidates.reserve(input.targets.size());
        for (Combatant* target : input.targets) {
            candidates.push_back(combat::TargetCandidate{
                target->position(), target->holdable() != nullptr && target->state() == TargetState::Standing &&
                                        !target->health().depleted()});
        }
        const std::size_t found = combat::nearestTarget(input.position, candidates, range);
        m_candidate = found != combat::kNoTarget ? input.targets[found]->holdable() : nullptr;
    }
    in.grabTargetInReach = m_candidate != nullptr;
    // The record's +0x08: the bits the clips playing hold (an attack's phases, the grab bit, the duck's).
    in.phase = animator.flags();
    in.fromRear = m_rear;
    in.victimMuggable = m_held != nullptr && !m_held->health().depleted();
    in.victimInPlace = victimInPlace(input);
    return in;
}

void Fighter::playDecisions(const combat::CombatOutput& out, combat::CombatMode before, const FighterInput& input,
                            HumanAnimator& animator, float& heading) {
    // The block: its clip unless a clip under it plays (an attack, a block reaction, a duck; a move's closing 389 gives
    // way). Let go, once the block's own loop plays again (a duck or a block reaction plays out first), the player
    // goes to the idle through its builder, whose fade holds 0x10000000 for 5 updates: the stick turns him on the spot
    // and he takes presses, then the walk start begins (docs/research/tasks.md#locomotion-gate).
    if (out.blocking) {
        if (!animator.drivingClipPlaying() || animator.settling()) {
            playBlock(input, animator);
        }
    } else if (m_combat.mode() == combat::CombatMode::Free && animator.state() == AnimState::Hold &&
               (animator.animId() == clips::kBlockSustain || animator.animId() == clips::kBlockShuffle)) {
        animator.settleToIdle();
    }

    // What the dispatcher started.
    bool consumed = false;
    if (out.rageStarted) {
        m_rageStarted = true;
        animator.playCombat(clips::one(id::kRageStart), kAnimFightIdle, AnimState::Attack, kCombatFade,
                            clips::kAttackHolds);
        consumed = true;
    }
    if ((out.grabStarted || out.tackleStarted) && m_candidate != nullptr) {
        startHold(*m_candidate, input, heading, out.tackleStarted, animator);
        consumed = true;
    } else if (out.grabMissed) {
        // Each miss ends in 389, then the idle (docs/research/combat.md#input-return).
        if (input.command == combat::command::kCircleHeld) {
            const std::array<std::uint32_t, 3> miss{id::kTacklePlayerIntro, clips::kTackleMiss,
                                                    clips::kNormalFromFight};
            animator.playCombat(miss, clips::kIdle, AnimState::Attack, kCombatFade, clips::kGrabHolds);
        } else {
            const std::array<std::uint32_t, 3> miss{id::kGrabPlayerIntro, clips::kGrabMiss, clips::kNormalFromFight};
            animator.playCombat(miss, clips::kIdle, AnimState::Attack, kCombatFade, clips::kGrabHolds);
        }
        consumed = true;
    }
    if (out.grabPowerOut && hurt() && m_held != nullptr) {
        // Out of power while hurt: the victim escapes rather than being let go.
        victimEscapes(input, animator);
        consumed = true;
    } else if (out.grabAction != combat::GrabAction::None) {
        playGrabAction(out, animator);
        consumed = true;
    } else if (out.mountAction != combat::MountAction::None) {
        playMountAction(out, animator);
        consumed = true;
    }
    if (before == combat::CombatMode::Mugging && out.game != combat::GameResult::Running && m_held != nullptr) {
        // The mugging is over: on success the money is taken and the victim spun back to the front hold.
        if (out.game == combat::GameResult::Succeeded) {
            const std::array<std::uint32_t, 2> end{clips::kMugEnd, id::kGrabSpinToFront};
            const std::array<std::uint32_t, 2> endReact{clips::kMugEndReact, id::kGrabSpinToFront + 1};
            animator.playCombat(end, clips::kGrabHold, AnimState::Hold, kCombatFade, clips::kGrabHolds);
            m_held->play(endReact, clips::kGrabHeld, AnimState::Hold, TargetState::Held);
            m_rear = false;
        } else {
            animator.playCombat(clips::kNoClips, clips::kGrabRearHold, AnimState::Hold);
            m_held->play(clips::kNoClips, clips::kGrabRearHeld, AnimState::Hold, TargetState::Held);
        }
    } else if (const auto& mugging = m_combat.mugging(); m_combat.mode() == combat::CombatMode::Mugging &&
                                                         mugging.has_value() && m_held != nullptr &&
                                                         !animator.drivingClipPlaying()) {
        // While the stick is on target the struggle clips play.
        const bool onTarget = mugging->onTarget();
        if (onTarget != m_mugOnTarget) {
            m_mugOnTarget = onTarget;
            animator.playCombat(clips::kNoClips, onTarget ? clips::kMugStruggle : clips::kMugLoop, AnimState::Hold);
            m_held->play(clips::kNoClips, onTarget ? clips::kMugStruggleReact : clips::kMugLoopReact, AnimState::Hold,
                         TargetState::Held);
        }
    }
    if (before == combat::CombatMode::Theft) {
        if (out.startAnim != id::kNone) {
            animator.playCombat(clips::one(clips::clipOf(out.startAnim)), clips::kIdle, AnimState::Attack, kCombatFade,
                                clips::kAttackHolds);
        }
        consumed = true;
    }
    if (out.startAnim != id::kNone && !consumed) {
        playAttack(out.startAnim, input, animator, heading);
    }
}

void Fighter::playBlock(const FighterInput& input, HumanAnimator& animator) {
    const std::uint32_t wanted =
        input.stick.magnitude() > locomotionTuning().stickDeadZone ? clips::kBlockShuffle : clips::kBlockSustain;
    if (animator.state() == AnimState::Hold && animator.animId() == wanted) {
        return;
    }
    animator.playCombat(clips::kNoClips, wanted, AnimState::Hold);
}

void Fighter::playAttack(int animId, const FighterInput& input, HumanAnimator& animator, float& heading) {
    const auto clip = clips::one(clips::clipOf(animId));
    // The start announces the attack to the humans around (event 0x10, `0x0021d5c0`); each one's brain decides
    // whether its range and field of view take it in. **Coney choice**: it goes to everyone this human can fight.
    for (Combatant* other : input.targets) {
        other->announceAttack(input.position);
    }
    const HeldFlags held = isMovingAttack(animId) ? clips::kMovingAttackHolds : clips::kAttackHolds;
    // Mounted on a tackled victim, the strike returns to the mount.
    if (m_combat.mode() == combat::CombatMode::Tackling) {
        animator.playCombat(clip, clips::kMountingIdle, AnimState::Hold, kCombatFade, held);
        return;
    }
    steer(animId, input, animator, heading);
    // A moving attack with the stick still at a run: the run resumes after it.
    if (isMovingAttack(animId) && input.stick.magnitude() > locomotionTuning().runThreshold) {
        animator.playCombatThenRun(clip, kCombatFade, held);
        return;
    }
    // With a target the fight idle follows; with none the attack ends in 389, then the idle, as at runtime with
    // nobody near (docs/research/combat.md#input-return). **Coney's reading**: the target kept decides between the
    // two (the original's test is not traced).
    if (m_target != nullptr) {
        animator.playCombat(clip, kAnimFightIdle, AnimState::Attack, kCombatFade, held);
        return;
    }
    const std::array<std::uint32_t, 2> settle{clips::clipOf(animId), kAnimNormalFromFight};
    animator.playCombat(settle, clips::kIdle, AnimState::Attack, kCombatFade, held);
}

void Fighter::steer(int animId, const FighterInput& input, const HumanAnimator& animator, float& heading) {
    m_steer.clear();
    const float far = reachOf(animId);
    Combatant* target = pickTarget(input, far);
    if (target == nullptr) {
        return;
    }
    // The attack takes it as the target (human +0xc8).
    m_target = target;
    const anim::Vec3 to = anim::subtract(target->position(), input.position);
    const float distance = std::hypot(to.x, to.y);
    if (distance < 1e-4F) {
        return;
    }
    // Beyond the far range it only turns, a little, at once.
    if (distance > far) {
        const float cap = kAttackTurnCapDegrees * kDegrees;
        heading = wrapAngle(heading + std::clamp(wrapAngle(headingOf(to) - heading), -cap, cap));
        return;
    }
    // Within it, the time to the clip's first event: the target is led by it + 0.1 s, and the steer lasts as long.
    // **Coney's reading** of the clamp: at runtime X1 (first event at frame 5, 0.208 s at rate 0.8) turned for 9.25
    // updates, the time to the event + 0.1 s.
    const auto clipId = static_cast<std::uint32_t>(clips::clipOf(animId));
    const anim::AnimClip* clip = animator.anims().clip(clipId);
    const float toEvent = clip != nullptr ? firstContactTime(*clip, animator.anims().rate(clipId)) : 0.0F;
    const float seconds = toEvent + kSteerLeadExtraSeconds;
    // The reach: the attack's, 0.07 m longer for a big target and 0.1 m shorter from behind it; without one, where the
    // target stands.
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(clipId) : nullptr;
    float reach = range != nullptr && range->reach > 0.0F ? range->reach : distance;
    if (target->bodyScale() > kSteerBigScale) {
        reach += kSteerBigReach;
    }
    if (combat::victimSide(target->position(), target->heading(), input.position) == combat::Side::Rear) {
        reach -= kSteerRearReach;
    }
    // Turn to face the led target and slide to stand at the reach from it, each at a constant rate over the steer's
    // time, from the next state update (the dispatcher runs after it, docs/research/combat.md#targets). **Coney's
    // reading**: the turn faces the led target, not the standing point, which lies behind the attacker when the target
    // is nearer than the reach (the original's XX2 at a target 0.83 m away, inside its 1.12 m reach, turned under 1°).
    const SteerGoal goal = attackSteerGoal(input.position, target->position(), target->velocity(), reach, toEvent);
    const anim::Vec3 toAim = anim::subtract(goal.aim, input.position);
    m_steer.turnToOver(heading, std::hypot(toAim.x, toAim.y) > 1e-4F ? headingOf(toAim) : headingOf(to), seconds);
    m_steer.moveToOver(input.position, goal.stand, seconds);
}

void Fighter::landHit(int animId, int damage, const FighterInput& input) {
    // The victim: the thrown one for a throw, the held one for a move in a hold, else whoever stands in reach.
    Combatant* victim = nullptr;
    bool heldMove = false;
    if (m_thrown != nullptr && clips::isThrow(animId)) {
        victim = m_thrown;
        m_thrown = nullptr;
        heldMove = true;
    } else if (m_held != nullptr && m_combat.mode() != combat::CombatMode::Free) {
        victim = m_held;
        heldMove = true;
    } else {
        victim = inFront(input, reachOf(animId), true);
    }
    if (victim == nullptr || (!heldMove && flatDistance(input.position, victim->position()) > reachOf(animId))) {
        return;
    }
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(clips::clipOf(animId)) : nullptr;
    // A move in a hold plays its own victim clips; a free hit picks the victim's reaction. A player's hits ignore
    // hit armour.
    victim->hit(IncomingHit{.damage = damage,
                            .attackAnim = animId,
                            .code = range != nullptr ? range->kind : 0,
                            .flags = range != nullptr ? range->flags : std::uint16_t{0},
                            .attacker = input.position,
                            .react = !heldMove,
                            .ignoresArmour = m_player || hasFlag(flag::kIncreasedReact),
                            .attackerFlag200000 = hasFlag(flag::kIncreasedReact),
                            .attackerIsPlayer = m_player});
    ++m_hitsLanded;
    m_damageDealt += damage;
    if (m_player) {
        m_strikes.push_back(animId);
    }
    // The hit earns its rage (**Coney choice**: never the blocked award, as a blocked hit is not reported back to the
    // attacker), then goes into the repeat tracker; a throw takes the bonus its grab strikes built.
    earnRage(animId, input.nowMs, clips::isThrow(animId));
    m_repeat.note(animId, input.nowMs);
}

void Fighter::earnRage(int animId, std::uint64_t nowMs, bool isThrow) {
    if (hasFlag(flag::kRageAllowed)) {
        combat::awardHitRage(m_combat.rage(), animId, false, combat::combatTuning(), nowMs, m_repeat, isThrow);
    }
}

void Fighter::trackTarget(const FighterInput& input) {
    const combat::CombatTuning& tuning = combat::combatTuning();
    // A target that left the fight is dropped: gone from the list, out of health, or down.
    if (m_target != nullptr) {
        const bool listed = std::ranges::find(input.targets, m_target) != input.targets.end();
        const bool fightable = listed && !m_target->health().depleted() &&
                               (m_target->state() == TargetState::Standing || m_target == m_held);
        const bool held = m_l1Held || m_target == m_held;
        if (!fightable || !combat::keepsTarget(tuning, flatDistance(input.position, m_target->position()), held)) {
            m_target = nullptr;
        }
    }
    // L1 pressed or held picks one when there is none. **Coney's choice**: it searches as far as a target is kept.
    if (m_target == nullptr &&
        (input.command == combat::command::kL1Pressed || input.command == combat::command::kL1Held)) {
        m_target = pickTarget(input, tuning.targetDropDistance);
    }
}

} // namespace coney::human
