// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/move_to_human_action.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>

#include "ai/attack_views.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/sectors.h"
#include "ai/threats.h"
#include "animation/anim_math.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// How far along the move its aim point lies, m (`MoveMeleeAction_Update`, `0x002fd4e8`).
constexpr float kAimAhead = 2.0F;
// The facings' cosine at or above which a man closed on counts as facing the same way as the mover (45°).
constexpr float kSameFacingCos = 0.707F;
// The class roles (`CharClass` `+0x11b`) that fight with no stance.
constexpr int kNoStanceRoleA = 6;
constexpr int kNoStanceRoleB = 7;
// The sectors the side pick reads: ahead, beside ahead, beside behind and behind the mover (or round the target).
constexpr int kAhead = 0;
constexpr int kAheadLeft = 1;
constexpr int kBehindLeft = 3;
constexpr int kBehind = 4;
constexpr int kBehindRight = 5;
constexpr int kAheadRight = 7;

// `v` scaled to length 1 across the ground; zero stays zero.
// @coney-only vector helper; the original normalises its quadwords in place
anim::Vec3 unitFlat(anim::Vec3 v) {
    const float length = std::hypot(v.x, v.y);
    return length > 1e-4F ? anim::Vec3{v.x / length, v.y / length, 0.0F} : anim::Vec3{};
}

// The unit forward of `body` (toward his sector 0): its transform's Y axis (`Quat_AxisY`).
// @orig 0x002fd4e8 MoveMeleeAction_Update (unknown)
anim::Vec3 forwardOf(const human::Human& body) {
    return unitFlat(anim::subtract(sectorPoint(body, kAhead, 1.0F), body.position()));
}

// Whether `target` is down, out of the fight or arrested (`Human_HasStateE0000`), in the reading the fight goal's
// place test takes: on the ground, or out of health.
// @orig 0x002fd4e8 MoveMeleeAction_Update (unknown)
bool downOrOut(const Brain& target) {
    const human::Human& t = target.human();
    return t.fighter().victim().grounded() || t.fighter().health().depleted() ||
           t.state() == human::TargetState::Grounded;
}

// Whether `target`'s human holds his victim from behind (state `0x80`).
// @orig 0x002239c0 Human_IsGrabbingRear (unknown)
bool grabbingRear(const Brain& target) {
    const human::Fighter& fighter = target.human().fighter();
    return fighter.held() != nullptr && fighter.fromRear();
}

} // namespace

bool fightsWithoutStance(const Brain& brain) {
    // **Coney stand-in**: the role through the gang's lookups (GangLookups::role); a brain with no gang has none.
    const Gang* gang = brain.gang();
    const std::optional<int> role = gang != nullptr ? gang->owner().roleOf(brain) : std::nullopt;
    return role == kNoStanceRoleA || role == kNoStanceRoleB;
}

ActionStatus MoveToHumanAction::start(Brain& brain) {
    m_untilMs = brain.nowMs() + m_limitMs;
    if (m_band) {
        m_noStance = fightsWithoutStance(brain);
        if (m_noStance) {
            brain.setFightStance(false);
        }
        m_savedBoost = brain.turnBoost();
    }
    return ActionStatus::Running;
}

ActionStatus MoveToHumanAction::update(Brain& brain) { return m_band ? keepBand(brain) : approach(brain); }

ActionStatus MoveToHumanAction::approach(Brain& brain) {
    const Brain* target = brain.target();
    if (target == nullptr || !Brain::fightable(*target) || brain.nowMs() >= m_untilMs) {
        brain.stopMove();
        return ActionStatus::Done;
    }
    const float distance = brain.distanceTo(*target);
    if (distance <= m_far) {
        brain.stopMove();
        return ActionStatus::Done;
    }
    const human::Speeds& speeds = brain.human().speeds();
    const anim::Vec3 position = brain.human().position();
    const anim::Vec3 way = anim::subtract(target->human().position(), position);
    // The aim, 2 m along the move (radius 0), as the band move sets it: in a fight stance the feet strafe toward it
    // while the facing turns to the heading.
    const float length = std::hypot(way.x, way.y);
    if (length > 1e-4F) {
        brain.setMoveAim(anim::add(position, anim::Vec3{kAimAhead * way.x / length, kAimAhead * way.y / length, 0.0F}),
                         0.0F);
    }
    brain.setMove(way, distance > kRunDistance ? speeds.run : speeds.walk);
    return ActionStatus::Running;
}

ActionStatus MoveToHumanAction::keepBand(Brain& brain) {
    Brain* target = brain.target();
    // Done in a standing reaction, or once the man is no longer up (`Handle_IsUp`). **Coney stand-ins**: the reaction
    // is a reaction goal on top, and "up" is Brain::fightable().
    if (brain.reactionGoal() != nullptr || target == nullptr || !Brain::fightable(*target)) {
        brain.stopMove();
        return ActionStatus::Done;
    }
    const bool expired = m_untilMs < brain.nowMs();
    const anim::Vec3 position = brain.human().position();
    const anim::Vec3 there = target->human().position();
    const float distance = brain.distanceTo(*target);
    // Too near or too far starts a radial step; closing within the far melee range raises the turn boost on a man
    // facing the same way, one who is down or out, or a moving empty-handed one while the mover holds something.
    if (m_radial == 0 && !expired) {
        if (distance < m_near) {
            m_radial = -kBandSteps;
        } else if (distance > m_far) {
            m_radial = kBandSteps;
            if (distance < brain.meleeFar()) {
                const float cosine = anim::dot(forwardOf(brain.human()), forwardOf(target->human()));
                const bool moving = target->human().gait() != human::Gait::Standing;
                // **Coney stand-in** for Human_IsHoldingObject (0x00224060): a weapon's anim set, not the held
                // object's kind (docs/research/ai-fights.md#q-score-held-object).
                const bool armedOnBare =
                    brain.human().fighter().animSet() != 0 && target->human().fighter().animSet() == 0;
                if (cosine >= kSameFacingCos || downOrOut(*target) || (moving && armedOnBare)) {
                    brain.setTurnBoost(m_savedBoost + 1);
                }
            }
        }
    }
    // The side pick runs every update; with no side step running and before the limit, its side starts one and ends
    // the radial step.
    const int side = pickSide(brain, *target);
    if (m_side == 0 && !expired && (side == kSideLeft || side == kSideRight)) {
        m_side = side == kSideLeft ? kBandSteps : -kBandSteps;
        m_radial = 0;
    }
    // The move: the radial step's direction, then the side on top. With no stance it starts from the raw offset to
    // him (metres long), so a side step alone only leans off the line to him, as the original adds its unit side axis
    // to that offset before normalising. **Coney choice**: across the ground; the original's vectors are 3D.
    const anim::Vec3 offset = anim::subtract(there, position);
    anim::Vec3 way = m_noStance ? anim::Vec3{offset.x, offset.y, 0.0F} : anim::Vec3{};
    const human::Speeds& speeds = brain.human().speeds();
    float speed = 0.0F;
    if (m_radial > 0) {
        --m_radial;
        way = unitFlat(offset);
        speed = m_noStance ? speeds.walk : speeds.run;
    } else if (m_radial < 0) {
        ++m_radial;
        way = unitFlat(anim::subtract(position, there));
        speed = speeds.walk;
    }
    if (m_side != 0) {
        const float sign = m_side > 0 ? 1.0F : -1.0F;
        m_side += m_side > 0 ? -1 : 1;
        if (speed == 0.0F) {
            speed = speeds.walk;
        }
        // The mover's side axis (`Quat_AxisX`), toward his sector 2.
        const anim::Vec3 across = unitFlat(anim::subtract(sectorPoint(brain.human(), kSideLeft, 1.0F), position));
        way = unitFlat(anim::add(way, anim::Vec3{sign * across.x, sign * across.y, 0.0F}));
    }
    // Settled in the band; past the limit, done once both steps are over.
    if (distance >= m_near && distance <= m_far && m_side == 0 && std::abs(m_radial) < kBandSettledSteps) {
        // **Coney stand-in** for the stance's own facing: a man settled in the band stands turned to him.
        if (!m_noStance && std::hypot(offset.x, offset.y) > 1e-4F) {
            brain.setMoveAim(position, 0.0F);
            brain.setMoveHeading(human::headingOf(offset), 0.0F);
        } else {
            brain.stopMove();
        }
        return ActionStatus::Done;
    }
    if (expired && m_side == 0 && m_radial == 0) {
        brain.stopMove();
        return ActionStatus::Done;
    }
    // The heading: the move's with no stance, else to him; the aim 2 m along the move. The human strafes toward the
    // aim while in the fight stance (Brain::fightStance()), whether or not its fighter holds a target. **Coney
    // stand-in**: the original's
    // `+0x11c` (set for every class but the "no stance" ones) only forbids the one-clip arrival, which Coney does not
    // build, so it is not kept.
    const anim::Vec3 face = m_noStance ? way : offset;
    brain.setMoveAim(anim::add(position, anim::Vec3{kAimAhead * way.x, kAimAhead * way.y, 0.0F}), 0.0F);
    if (std::hypot(face.x, face.y) > 1e-4F) {
        brain.setMoveHeading(human::headingOf(face), speed);
    } else {
        brain.setMoveHeading(brain.human().heading(), 0.0F);
    }
    return ActionStatus::Running;
}

int MoveToHumanAction::pickSide(Brain& brain, const Brain& target) {
    // Backing off, the sectors behind the mover; otherwise those ahead.
    const bool backing = m_radial < 0;
    const int ahead = backing ? kBehind : kAhead;
    const int besideRight = backing ? kBehindRight : kAheadRight;
    const int besideLeft = backing ? kBehindLeft : kAheadLeft;
    Sectors& sectors = brain.sectors(kSectorAgeMs);
    // A radial step blocked by someone other than the target ends, and a free side is taken.
    if (m_radial != 0 && sectors[ahead].nearest != &target && sectors.blocked(brain, ahead)) {
        m_radial = 0;
        if (sectors.blocked(brain, besideRight)) {
            if (!sectors.blocked(brain, besideLeft)) {
                if (!sectors.blocked(brain, kSideLeft)) {
                    return kSideLeft;
                }
                if (sectors.free(brain, kSideRight)) {
                    return kSideRight;
                }
            } else if (sectors.free(brain, kSideRight) && sectors.free(brain, kSideLeft)) {
                // **Coney stand-in**: the brain's handle's low bit for bit 0x10000 of the human's handle.
                return (static_cast<std::uint64_t>(brain.handle()) & 1U) != 0 ? kSideLeft : kSideRight;
            } else if (sectors.cost(brain, kSideRight) <= sectors.cost(brain, kSideLeft)) {
                if (sectors.free(brain, kSideRight)) {
                    return kSideRight;
                }
            } else if (sectors.free(brain, kSideLeft)) {
                return kSideLeft;
            }
        } else if (sectors.blocked(brain, besideLeft)) {
            if (!sectors.blocked(brain, kSideRight)) {
                return kSideRight;
            }
            if (!sectors.blocked(brain, kSideLeft)) {
                return kSideLeft;
            }
        } else {
            if (sectors.free(brain, kSideRight)) {
                return kSideRight;
            }
            if (sectors.free(brain, kSideLeft)) {
                return kSideLeft;
            }
        }
    }
    // The cheaper side by the costs.
    const int right = sectors.cost(brain, kSideRight) + sectors.cost(brain, besideRight);
    const int left = sectors.cost(brain, kSideLeft) + sectors.cost(brain, besideLeft);
    if (left > right) {
        return kSideRight;
    }
    if (right > left) {
        return kSideLeft;
    }
    // Even: straight in, unless he is in a rear grab and a threat; then round to his back (his front when held).
    const bool held = grabbedFromRear(target);
    if ((!held && !grabbingRear(target)) || !isThreat(brain, target)) {
        return kSideNone;
    }
    const int mine = sectorOf(target.human(), brain.human().position());
    const int wanted = held ? kAhead : kBehind;
    if (mine == wanted) {
        return kSideNone;
    }
    if (target.fightBook().places.holds(&brain)) {
        brain.setTurnBoost(m_savedBoost + 1);
    }
    const int low = held ? kSideLeft : kSideRight;
    const int high = held ? kSideRight : kSideLeft;
    return mine > kBehind ? high : low;
}

bool MoveToHumanAction::abort(Brain& brain) {
    brain.stopMove();
    if (m_band) {
        // A man with no stance turns to a live target (`Brain_IsTargetInvalid` `0x0028d4b0`). **Coney stand-in**:
        // "live" is Brain::fightable(); its turf test is left out.
        if (const Brain* target = brain.target(); m_noStance && target != nullptr && Brain::fightable(*target)) {
            const anim::Vec3 to = anim::subtract(target->human().position(), brain.human().position());
            if (std::hypot(to.x, to.y) > 1e-4F) {
                brain.setMoveHeading(human::headingOf(to), 0.0F);
            }
        }
        brain.setTurnBoost(m_savedBoost);
    }
    return true;
}

} // namespace coney::ai
