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

Fighter::Fighter(const combat::AnimRangeList* ranges, std::uint32_t seed)
    : m_ranges(ranges), m_combat(ranges, 0, seed), m_victim(combat::kPlayerPowerClass, seed),
      m_grabbedRandom(seed + 1U) {}

bool Fighter::holdsMovement(const HumanAnimator& animator) const {
    return m_combat.blocking() || m_combat.mode() != combat::CombatMode::Free || grabbed() || helpless(animator);
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

TargetHuman* Fighter::inFront(const FighterInput& input, float range, bool grounded) const {
    TargetHuman* best = nullptr;
    float bestDistance = range;
    const anim::Vec3 ahead = facing(input.heading);
    for (TargetHuman* target : input.targets) {
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

TargetHuman* Fighter::pickTarget(const FighterInput& input, float range) {
    // The heading searched along: the stick's when it is pushed, else the facing. The stick is in the facing frame,
    // its angle positive to the right, and headings grow to the left.
    const float along =
        input.stick.magnitude() > kPickStick ? input.heading - (input.stick.angleDegrees() * kDegrees) : input.heading;
    // The nearest standing target within `reach` (and `cone` degrees of the heading, when given).
    const auto nearest = [&](float reach, float cone) -> TargetHuman* {
        TargetHuman* best = nullptr;
        float bestDistance = reach;
        for (TargetHuman* target : input.targets) {
            if (target->state() != TargetState::Standing || target->health().depleted() ||
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
    if (TargetHuman* found = nearest(range * kPickConeScale, kPickConeDegrees); found != nullptr) {
        return found;
    }
    return nearest(range * kPickAnyScale, 0.0F);
}

const TargetHuman* Fighter::lockTarget() const {
    return combat::lockedOn(combat::combatTuning(), m_target != nullptr, m_l1Held) ? m_target : nullptr;
}

void Fighter::update(const FighterInput& input, HumanAnimator& animator, float& heading) {
    const combat::CombatTuning& tuning = combat::combatTuning();
    const combat::CombatMode before = m_combat.mode();
    m_report = GrabbedReport{};
    m_l1Held = (input.buttons & pad::kL1) != 0;
    // The power meter's maximum follows the hurt state; the throw bonus lasts only while grabbing from the front or
    // throwing.
    m_combat.power().setHurt(hurt(), m_victim.powerClass().hurtPowerFactor);
    const bool frontGrab = m_combat.mode() == combat::CombatMode::Grabbing && !m_rear;
    if (!frontGrab && !clips::isThrow(static_cast<int>(animator.animId()))) {
        m_repeat.resetBonus();
    }
    // The settle after the block counts down while its idle plays (a move started in it ends it); at its end the idle
    // is the controller's again.
    if (m_blockSettle > 0) {
        const bool settled = animator.state() == AnimState::Hold && animator.animId() == clips::kIdle;
        m_blockSettle = settled ? m_blockSettle - 1 : 0;
        if (settled && m_blockSettle == 0) {
            animator.endHold();
        }
    }

    // 1. The victim's side first, as the original applies the pending damage before the actions: a grab caught on
    // the player, a warning, the update's hit, then the reaction's timers.
    if (m_catch.has_value()) {
        startGrabbed(input, animator);
    }
    takeNotice(input, animator);
    takePending(input, animator);
    const bool cannotAct = stepVictim(input, animator) || grabbed();
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
                             .targets = input.targets});
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
    const TargetHuman* front = inFront(input, reachOf(id::kAttackS1), true);
    in.target = front != nullptr && front->state() == TargetState::Grounded ? combat::TargetKind::Grounded
                                                                            : combat::TargetKind::None;
    m_candidate = nullptr;
    if (input.command == combat::command::kCircleTapped || input.command == combat::command::kCircleHeld) {
        const combat::GrabKind kind =
            input.command == combat::command::kCircleHeld ? combat::GrabKind::Tackle : combat::GrabKind::Grab;
        const float range = m_ranges != nullptr ? combat::grabSearchRange(*m_ranges, kind, tuning.grabSearchScale)
                                                : (kind == combat::GrabKind::Tackle ? 3.75F : 3.12F);
        std::vector<combat::TargetCandidate> candidates;
        candidates.reserve(input.targets.size());
        for (const TargetHuman* target : input.targets) {
            candidates.push_back(combat::TargetCandidate{target->position(), target->state() == TargetState::Standing &&
                                                                                 !target->health().depleted()});
        }
        const std::size_t found = combat::nearestTarget(input.position, candidates, range);
        m_candidate = found != combat::kNoTarget ? input.targets[found] : nullptr;
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
    // stands in the idle for kBlockSettleUpdates with the stick held, taking presses
    // (docs/research/combat.md#input-return; what holds the stick is not traced).
    if (out.blocking) {
        m_blockSettle = 0;
        if (!animator.drivingClipPlaying() || animator.settling()) {
            playBlock(input, animator);
        }
    } else if (m_combat.mode() == combat::CombatMode::Free && animator.state() == AnimState::Hold &&
               (animator.animId() == clips::kBlockSustain || animator.animId() == clips::kBlockShuffle)) {
        animator.playCombat(clips::kNoClips, clips::kIdle, AnimState::Hold);
        m_blockSettle = kBlockSettleUpdates;
    }

    // What the dispatcher started.
    bool consumed = false;
    if (out.rageStarted) {
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
    const HeldFlags held = isMovingAttack(animId) ? clips::kMovingAttackHolds : clips::kAttackHolds;
    // Mounted on a tackled victim, the strike returns to the mount.
    if (m_combat.mode() == combat::CombatMode::Tackling) {
        animator.playCombat(clip, clips::kMountingIdle, AnimState::Hold, kCombatFade, held);
        return;
    }
    steer(animId, input, heading);
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

void Fighter::steer(int animId, const FighterInput& input, float& heading) {
    m_slideUpdates = 0;
    const float far = reachOf(animId);
    TargetHuman* target = pickTarget(input, far);
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
    const float wanted = headingOf(to);
    // Beyond the far range it only turns, a little.
    if (distance > far) {
        const float cap = kAttackTurnCapDegrees * kDegrees;
        heading = wrapAngle(heading + std::clamp(wrapAngle(wanted - heading), -cap, cap));
        return;
    }
    // Within it, it faces the target and slides so that it stands at the clip's reach when the hit lands.
    // **Coney choice**: the slide is spread evenly over the updates to the hit (at least one), on top of the clip's
    // own root motion; the target does not move, so its velocity term is 0.
    heading = wanted;
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(clips::clipOf(animId)) : nullptr;
    const float reach = range != nullptr && range->reach > 0.0F ? range->reach : distance;
    const int updates = std::max(1, combat::attackHitUpdate(animId, combat::combatTuning()));
    const float perSecond = (distance - reach) / (static_cast<float>(updates) * kStepSeconds);
    m_slide = anim::scale(anim::Vec3{to.x / distance, to.y / distance, 0.0F}, perSecond);
    m_slideUpdates = updates;
}

void Fighter::landHit(int animId, int damage, const FighterInput& input) {
    // The victim: the thrown one for a throw, the held one for a move in a hold, else whoever stands in reach.
    TargetHuman* victim = nullptr;
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
    // A move in a hold plays its own victim clips; a free hit picks the victim's reaction. The player's hits ignore
    // hit armour (the attacker is a player).
    victim->hit(IncomingHit{.damage = damage,
                            .attackAnim = animId,
                            .code = range != nullptr ? range->kind : 0,
                            .flags = range != nullptr ? range->flags : std::uint16_t{0},
                            .attacker = input.position,
                            .react = !heldMove,
                            .ignoresArmour = true,
                            .attackerFlag200000 = false});
    ++m_hitsLanded;
    m_damageDealt += damage;
    // The hit earns its rage (the target never blocks, so never the blocked award), then goes into the repeat
    // tracker; a throw takes the bonus its grab strikes built.
    combat::awardHitRage(m_combat.rage(), animId, false, combat::combatTuning(), input.nowMs, m_repeat,
                         clips::isThrow(animId));
    m_repeat.note(animId, input.nowMs);
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
