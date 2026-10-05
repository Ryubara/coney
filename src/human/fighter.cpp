// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/fighter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

#include "combat/anim_ids.h"
#include "combat/combat_tuning.h"
#include "combat/grab.h"

namespace coney::human {

namespace {

namespace id = combat::anim_id;

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDegrees = kPi / 180.0F;

// The clips the moves play, by anim id (docs/research/combat.md, docs/references/anim-ids.md).
constexpr std::uint32_t kIdle = 388;
constexpr std::uint32_t kGrabFrontEnd = 72;
constexpr std::uint32_t kGrabReactFromFront = 73;
constexpr std::uint32_t kGrabMiss = 69;
constexpr std::uint32_t kGrabHold = 82;
constexpr std::uint32_t kGrabHeld = 83;
constexpr std::uint32_t kGrabRearHold = 84;
constexpr std::uint32_t kGrabRearHeld = 85;
constexpr std::uint32_t kGrabLetGoReact = 94;
constexpr std::uint32_t kNormalFromFight = 389;
constexpr std::uint32_t kTackleMiss = 2;
constexpr std::uint32_t kTackleHit = 5;
constexpr std::uint32_t kTackleReact = 6;
constexpr std::uint32_t kMountedIdle = 207;
constexpr std::uint32_t kMountingIdle = 210;
constexpr std::uint32_t kGroundedIdle = 196;
constexpr std::uint32_t kGroundedRise = 199;
constexpr std::uint32_t kMugIntro = 338;
constexpr std::uint32_t kMugIntroReact = 339;
constexpr std::uint32_t kMugLoop = 340;
constexpr std::uint32_t kMugLoopReact = 341;
constexpr std::uint32_t kMugStruggle = 342;
constexpr std::uint32_t kMugStruggleReact = 343;
constexpr std::uint32_t kMugEnd = 344;
constexpr std::uint32_t kMugEndReact = 345;
constexpr std::uint32_t kBlockSustain = 606;
constexpr std::uint32_t kBlockShuffle = 607;

// Whether `animId` is one of the throws.
bool isThrow(int animId) { return animId >= id::kThrow1Front && animId <= id::kThrow2Left; }

// Whether `animId` is a moving attack, after which the run may go on.
bool isMovingAttack(int animId) {
    return animId == id::kRunningAttackCharge || animId == id::kRunningAttackDive || animId == id::kAttackFromRun;
}

// The one clip `clip` as an array.
std::array<std::uint32_t, 1> one(std::uint32_t clip) { return {clip}; }
// Anim id `animId` as a clip id.
std::uint32_t clipOf(int animId) { return static_cast<std::uint32_t>(animId); }

// No clips: just a loop.
constexpr std::array<std::uint32_t, 0> kNone{};

// The horizontal distance between two points.
float flatDistance(const anim::Vec3& a, const anim::Vec3& b) { return std::hypot(b.x - a.x, b.y - a.y); }

} // namespace

Fighter::Fighter(const combat::AnimRangeList* ranges, std::uint32_t seed)
    : m_ranges(ranges), m_combat(ranges, 0, seed) {}

bool Fighter::holdsMovement(const HumanAnimator& animator) const {
    return m_combat.blocking() || m_combat.mode() != combat::CombatMode::Free ||
           (animator.state() == AnimState::Attack && animator.drivingClipPlaying());
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

void Fighter::update(const FighterInput& input, HumanAnimator& animator, float& heading) {
    const combat::CombatTuning& tuning = combat::combatTuning();
    const combat::CombatMode before = m_combat.mode();

    // What the dispatcher needs from the world: square's target, and for circle the nearest target in its search.
    combat::CombatInput in;
    in.command = input.command;
    in.buttons = input.buttons;
    in.stick = input.stick;
    in.padStick = input.padStick;
    in.gait = input.gait;
    in.nowMs = input.nowMs;
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
    in.fromRear = m_rear;
    in.victimMuggable = m_held != nullptr && !m_held->health().depleted();
    const combat::CombatOutput out = m_combat.update(in, tuning);
    m_last = out;

    // The block: its clip unless an attack under it plays; letting go returns to the fight idle.
    const bool attackPlaying = animator.state() == AnimState::Attack && animator.drivingClipPlaying();
    if (out.blocking) {
        if (!attackPlaying) {
            playBlock(input, animator);
        }
        m_wasBlocking = true;
    } else if (m_wasBlocking) {
        m_wasBlocking = false;
        if (animator.state() == AnimState::Hold) {
            animator.playCombat(kNone, kAnimFightIdle, AnimState::Attack);
        }
    }

    // What the dispatcher started.
    bool consumed = false;
    if (out.rageStarted) {
        animator.playCombat(one(id::kRageStart), kAnimFightIdle, AnimState::Attack);
        consumed = true;
    }
    if ((out.grabStarted || out.tackleStarted) && m_candidate != nullptr) {
        startHold(*m_candidate, input, heading, out.tackleStarted, animator);
        consumed = true;
    } else if (out.grabMissed) {
        if (input.command == combat::command::kCircleHeld) {
            const std::array<std::uint32_t, 2> miss{id::kTacklePlayerIntro, kTackleMiss};
            animator.playCombat(miss, kAnimFightIdle, AnimState::Attack);
        } else {
            const std::array<std::uint32_t, 3> miss{id::kGrabPlayerIntro, kGrabMiss, kNormalFromFight};
            animator.playCombat(miss, kIdle, AnimState::Attack);
        }
        consumed = true;
    }
    if (out.grabAction != combat::GrabAction::None) {
        playGrabAction(out, animator);
        consumed = true;
    }
    if (before == combat::CombatMode::Mugging && out.game != combat::GameResult::Running && m_held != nullptr) {
        // The mugging is over: on success the money is taken and the victim spun back to the front hold.
        if (out.game == combat::GameResult::Succeeded) {
            const std::array<std::uint32_t, 2> end{kMugEnd, id::kGrabSpinToFront};
            const std::array<std::uint32_t, 2> endReact{kMugEndReact, id::kGrabSpinToFront + 1};
            animator.playCombat(end, kGrabHold, AnimState::Hold);
            m_held->play(endReact, kGrabHeld, AnimState::Hold, TargetState::Held);
            m_rear = false;
        } else {
            animator.playCombat(kNone, kGrabRearHold, AnimState::Hold);
            m_held->play(kNone, kGrabRearHeld, AnimState::Hold, TargetState::Held);
        }
    } else if (const auto& mugging = m_combat.mugging(); m_combat.mode() == combat::CombatMode::Mugging &&
                                                         mugging.has_value() && m_held != nullptr &&
                                                         !animator.drivingClipPlaying()) {
        // While the stick is on target the struggle clips play.
        const bool onTarget = mugging->onTarget();
        if (onTarget != m_mugOnTarget) {
            m_mugOnTarget = onTarget;
            animator.playCombat(kNone, onTarget ? kMugStruggle : kMugLoop, AnimState::Hold);
            m_held->play(kNone, onTarget ? kMugStruggleReact : kMugLoopReact, AnimState::Hold, TargetState::Held);
        }
    }
    if (before == combat::CombatMode::Theft) {
        if (out.startAnim != id::kNone) {
            animator.playCombat(one(clipOf(out.startAnim)), kIdle, AnimState::Attack);
        }
        consumed = true;
    }
    if (out.startAnim != id::kNone && !consumed) {
        playAttack(out.startAnim, input, animator, heading);
    }

    // The tackle's hit: when its clip starts the victim goes down in front of the player, mounted.
    if (m_tacklePending && m_held != nullptr &&
        (animator.animId() == kTackleHit || animator.animId() == kMountingIdle)) {
        m_tacklePending = false;
        m_held->place(anim::add(input.position, anim::scale(facing(heading), kMountDistance)), heading + kPi);
        m_held->play(one(kTackleReact), kMountedIdle, AnimState::Hold, TargetState::Mounted);
    }
    if (out.hitAnim != id::kNone) {
        landHit(out.hitAnim, out.hitDamage, input);
    }

    // A hold ends when the victim has no health left, and a tackle when the power meter is empty (**Coney's
    // choice**: the research ends a grab at 0 power, which the dispatcher does; the tackle is taken to end alike).
    const combat::CombatMode mode = m_combat.mode();
    const bool holding = mode == combat::CombatMode::Grabbing || mode == combat::CombatMode::Tackling ||
                         mode == combat::CombatMode::Mugging;
    if (holding && m_held != nullptr &&
        (m_held->health().depleted() || (mode == combat::CombatMode::Tackling && m_combat.power().value() == 0))) {
        releaseHold(animator, false);
    }
}

void Fighter::playBlock(const FighterInput& input, HumanAnimator& animator) {
    const std::uint32_t wanted =
        input.stick.magnitude() > locomotionTuning().stickDeadZone ? kBlockShuffle : kBlockSustain;
    if (animator.state() == AnimState::Hold && animator.animId() == wanted) {
        return;
    }
    animator.playCombat(kNone, wanted, AnimState::Hold);
}

void Fighter::playAttack(int animId, const FighterInput& input, HumanAnimator& animator, float& heading) {
    const auto clip = one(clipOf(animId));
    // Mounted on a tackled victim, the strike returns to the mount.
    if (m_combat.mode() == combat::CombatMode::Tackling) {
        animator.playCombat(clip, kMountingIdle, AnimState::Hold);
        return;
    }
    steer(animId, input, heading);
    // A moving attack with the stick still at a run: the run resumes after it.
    if (isMovingAttack(animId) && input.stick.magnitude() > locomotionTuning().runThreshold) {
        animator.playCombatThenRun(clip);
        return;
    }
    animator.playCombat(clip, kAnimFightIdle, AnimState::Attack);
}

void Fighter::steer(int animId, const FighterInput& input, float& heading) {
    m_slideUpdates = 0;
    const float far = reachOf(animId);
    const TargetHuman* target = pickTarget(input, far);
    if (target == nullptr) {
        return;
    }
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
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(clipOf(animId)) : nullptr;
    const float reach = range != nullptr && range->reach > 0.0F ? range->reach : distance;
    const int updates = std::max(1, combat::attackTiming(animId, combat::combatTuning()).hit);
    const float perSecond = (distance - reach) / (static_cast<float>(updates) * kStepSeconds);
    m_slide = anim::scale(anim::Vec3{to.x / distance, to.y / distance, 0.0F}, perSecond);
    m_slideUpdates = updates;
}

void Fighter::placeHeld(const FighterInput& input, float heading) const {
    if (m_held == nullptr) {
        return;
    }
    m_held->place(anim::add(input.position, anim::scale(facing(heading), kGrabDistance)),
                  m_rear ? heading : heading + kPi);
}

void Fighter::startHold(TargetHuman& victim, const FighterInput& input, float& heading, bool tackle,
                        HumanAnimator& animator) {
    // Face the victim; it faces back.
    const anim::Vec3 to = anim::subtract(victim.position(), input.position);
    if (std::hypot(to.x, to.y) > 1e-4F) {
        heading = headingOf(to);
    }
    m_held = &victim;
    m_rear = false;
    if (tackle) {
        // The intro covers the distance; the victim waits for the hit clip.
        const std::array<std::uint32_t, 2> tackleClips{id::kTacklePlayerIntro, kTackleHit};
        animator.playCombat(tackleClips, kMountingIdle, AnimState::Hold);
        victim.face(input.position);
        victim.play(kNone, kIdle, AnimState::Hold, TargetState::Held);
        m_tacklePending = true;
        return;
    }
    const std::array<std::uint32_t, 2> grabClips{id::kGrabPlayerIntro, kGrabFrontEnd};
    animator.playCombat(grabClips, kGrabHold, AnimState::Hold);
    placeHeld(input, heading);
    victim.play(one(kGrabReactFromFront), kGrabHeld, AnimState::Hold, TargetState::Held);
}

void Fighter::playGrabAction(const combat::CombatOutput& out, HumanAnimator& animator) {
    const std::uint32_t hold = m_rear ? kGrabRearHold : kGrabHold;
    const std::uint32_t held = m_rear ? kGrabRearHeld : kGrabHeld;
    switch (out.grabAction) {
    case combat::GrabAction::Strike:
        // The strike and the victim's reaction (the next id), then both back to the hold.
        animator.playCombat(one(clipOf(out.startAnim)), hold, AnimState::Hold);
        if (m_held != nullptr) {
            m_held->play(one(clipOf(out.startAnim) + 1), held, AnimState::Hold, TargetState::Held);
        }
        break;
    case combat::GrabAction::PowerStrike: {
        // From the rear the victim is spun to the front first; both end in the front hold.
        const std::uint32_t strike = clipOf(out.startAnim);
        if (m_rear) {
            const std::array<std::uint32_t, 2> clips{id::kGrabSpinToFront, strike};
            const std::array<std::uint32_t, 2> reacts{id::kGrabSpinToFront + 1, strike + 1};
            animator.playCombat(clips, kGrabHold, AnimState::Hold);
            if (m_held != nullptr) {
                m_held->play(reacts, kGrabHeld, AnimState::Hold, TargetState::Held);
            }
            m_rear = false;
        } else {
            animator.playCombat(one(strike), kGrabHold, AnimState::Hold);
            if (m_held != nullptr) {
                m_held->play(one(strike + 1), kGrabHeld, AnimState::Hold, TargetState::Held);
            }
        }
        break;
    }
    case combat::GrabAction::Throw:
        // The throw lets go: the victim plays its reaction and lands on its back.
        animator.playCombat(one(clipOf(out.startAnim)), kAnimFightIdle, AnimState::Attack);
        if (m_held != nullptr) {
            m_held->play(one(clipOf(out.startAnim) + 1), kGroundedIdle, AnimState::Hold, TargetState::Grounded);
        }
        m_thrown = m_held;
        m_held = nullptr;
        m_rear = false;
        break;
    case combat::GrabAction::Spin: {
        // The spin to the other side, then that side's hold.
        m_rear = out.startAnim == id::kGrabSpinToRear;
        animator.playCombat(one(clipOf(out.startAnim)), m_rear ? kGrabRearHold : kGrabHold, AnimState::Hold);
        if (m_held != nullptr) {
            m_held->play(one(clipOf(out.startAnim) + 1), m_rear ? kGrabRearHeld : kGrabHeld, AnimState::Hold,
                         TargetState::Held);
        }
        break;
    }
    case combat::GrabAction::Mug: {
        // The victim is spun to a rear hold (unless it is there already), then the mugging loop.
        if (m_rear) {
            animator.playCombat(one(kMugIntro), kMugLoop, AnimState::Hold);
            if (m_held != nullptr) {
                m_held->play(one(kMugIntroReact), kMugLoopReact, AnimState::Hold, TargetState::Held);
            }
        } else {
            const std::array<std::uint32_t, 2> mug{id::kGrabSpinToRear, kMugIntro};
            const std::array<std::uint32_t, 2> mugReact{id::kGrabSpinToRear + 1, kMugIntroReact};
            animator.playCombat(mug, kMugLoop, AnimState::Hold);
            if (m_held != nullptr) {
                m_held->play(mugReact, kMugLoopReact, AnimState::Hold, TargetState::Held);
            }
        }
        m_rear = true;
        m_mugOnTarget = false;
        break;
    }
    case combat::GrabAction::LetGo:
        if (m_held != nullptr) {
            releaseHold(animator, true);
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

void Fighter::landHit(int animId, int damage, const FighterInput& input) {
    // The victim: the thrown one for a throw, the held one for a move in a hold, else whoever stands in reach.
    TargetHuman* victim = nullptr;
    bool heldMove = false;
    if (m_thrown != nullptr && isThrow(animId)) {
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
    const combat::AnimRange* range = m_ranges != nullptr ? m_ranges->find(clipOf(animId)) : nullptr;
    // A move in a hold plays its own victim clips; a free hit picks the victim's reaction.
    victim->hit(TargetHit{.damage = damage,
                          .attackAnim = animId,
                          .code = range != nullptr ? range->kind : 0,
                          .flags = range != nullptr ? range->flags : std::uint16_t{0},
                          .attacker = input.position,
                          .react = !heldMove});
    ++m_hitsLanded;
    m_damageDealt += damage;
}

void Fighter::releaseHold(HumanAnimator& animator, bool letGo) {
    const bool mounted = m_held->state() == TargetState::Mounted;
    m_combat.release();
    if (letGo) {
        animator.playCombat(one(id::kGrabLetGo), kAnimFightIdle, AnimState::Attack);
    } else {
        animator.playCombat(kNone, kAnimFightIdle, AnimState::Attack);
    }
    if (!m_held->health().depleted()) {
        // A mounted victim gets up; a let-go one plays its clip; a held one stands where it is.
        const auto rise = one(kGroundedRise);
        const auto freed = one(kGrabLetGoReact);
        std::span<const std::uint32_t> clips = kNone;
        if (mounted) {
            clips = rise;
        } else if (letGo) {
            clips = freed;
        }
        m_held->play(clips, kIdle, AnimState::Attack, TargetState::Standing);
    }
    m_held = nullptr;
    m_rear = false;
    m_tacklePending = false;
}

} // namespace coney::human
