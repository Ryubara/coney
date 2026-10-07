// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/steering.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ai/brain.h"
#include "ai/formations.h"
#include "ai/move_action.h"
#include "ai/sectors.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "raycast/collision_mesh.h"

namespace coney::ai {

namespace {

// `a` with its height dropped.
anim::Vec3 flat(anim::Vec3 a) { return {a.x, a.y, 0.0F}; }

// `a` in plan, of unit length (zero stays zero).
anim::Vec3 unitFlat(anim::Vec3 a) {
    const anim::Vec3 plan = flat(a);
    const float length = anim::length(plan);
    return length > 0.0F ? anim::scale(plan, 1.0F / length) : anim::Vec3{};
}

// The z of the 2D cross product of (b - a) and (c - a): its sign says which side of a-b c lies on.
float crossZ(anim::Vec3 a, anim::Vec3 b, anim::Vec3 c) {
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

// The four cases of a decision, by where the blocker stands and which way he heads.
enum class SteerCase : std::uint8_t { Standing, HeadOn, Overtake, Crossing };

// Whether the blocker stands: a player's human below kBlockerStandingSpeed; an AI's brain asking for no speed, or his
// steering's override running at none.
bool standing(const Brain& other) {
    if (other.type() == BrainType::Player) {
        return other.human().speed() < kBlockerStandingSpeed;
    }
    const auto& move = other.human().record().move;
    if (!move.has_value() || move->speed <= 0.0F) {
        return true;
    }
    const std::optional<float> held = other.steering().speedOverride();
    return held.has_value() && *held <= 0.0F;
}

// The end of a decision that finds nothing to steer round: a detour heads on for its held point, else the move keeps
// its own aim.
std::optional<anim::Vec3> keepHeld(const Brain& brain) {
    return brain.steering().avoiding != nullptr ? std::optional<anim::Vec3>{brain.steering().heldPoint} : std::nullopt;
}

// The scene's brains, as the list the steering looks through.
std::vector<Brain*> peersOf(const Brain& brain) {
    std::vector<Brain*> peers;
    if (brain.peers() != nullptr) {
        peers.reserve(brain.peers()->size());
        for (const std::unique_ptr<Brain>& peer : *brain.peers()) {
            peers.push_back(peer.get());
        }
    }
    return peers;
}

} // namespace

anim::Vec3 clampStep(anim::Vec3 from, anim::Vec3 to, float maxLength) {
    const anim::Vec3 way = flat(anim::subtract(to, from));
    const float distance = anim::length(way);
    if (distance <= maxLength || distance <= 0.0F) {
        return way;
    }
    return anim::scale(way, maxLength / distance);
}

std::optional<float> rayHitsHuman(anim::Vec3 origin, anim::Vec3 ray, anim::Vec3 centre) {
    const anim::Vec3 along = flat(ray);
    const float reach = anim::length(along);
    if (reach <= 0.0F) {
        return std::nullopt;
    }
    const anim::Vec3 offset = flat(anim::subtract(centre, origin));
    const float distance = anim::length(offset);
    // Too far to be reached, or behind the ray.
    if (distance > reach + kBlockerRadius) {
        return std::nullopt;
    }
    const anim::Vec3 unit = anim::scale(along, 1.0F / reach);
    const float projected = anim::dot(offset, unit);
    if (projected < 0.0F) {
        return std::nullopt;
    }
    // The ray's closest approach to the centre against the disc.
    const float radiusSquared = kBlockerRadius * kBlockerRadius;
    const float missSquared = distance * distance - projected * projected;
    if (missSquared > radiusSquared) {
        return std::nullopt;
    }
    // Where it crosses the disc's edge; already inside, at once.
    return std::max(projected - std::sqrt(radiusSquared - missSquared), 0.0F);
}

anim::Vec3 predictedStep(const Brain& brain) {
    const human::Human& human = brain.human();
    if (brain.type() == BrainType::Player) {
        return anim::scale(flat(human::facing(human.heading())), kStepSeconds * human.speed());
    }
    const auto& move = human.record().move;
    const float speed = move.has_value() ? move->speed : 0.0F;
    return clampStep(human.position(), brain.moveAim(), kStepSeconds * speed);
}

std::vector<Brain*> humansAround(const Brain& owner, std::span<Brain* const> others, float radius) {
    std::vector<Brain*> list;
    const anim::Vec3 at = owner.human().position();
    const float reach = radius * radius;
    for (Brain* other : others) {
        if (list.size() >= kSteerListMax) {
            break;
        }
        if (other == nullptr || other == &owner || other->human().outOfWorld()) {
            continue;
        }
        const anim::Vec3 offset = flat(anim::subtract(other->human().position(), at));
        if (anim::dot(offset, offset) <= reach) {
            list.push_back(other);
        }
    }
    return list;
}

std::optional<Blocker> findBlocker(const Brain& walker, std::span<Brain* const> list, anim::Vec3 position,
                                   anim::Vec3 direction, anim::Vec3 step) {
    std::optional<Blocker> best;
    float bestFraction = std::numeric_limits<float>::max();
    for (Brain* other : list) {
        if (other == nullptr || other == &walker || other == walker.target()) {
            continue;
        }
        // 1. Nobody behind the walker.
        const anim::Vec3 centre = other->human().position();
        if (anim::dot(flat(direction), flat(anim::subtract(centre, position))) < 0.0F) {
            continue;
        }
        // 2-3. The walker's step relative to his, swept against his disc.
        const anim::Vec3 relative = flat(anim::subtract(step, predictedStep(*other)));
        const float reach = anim::length(relative);
        const std::optional<float> hit = rayHitsHuman(position, relative, centre);
        if (!hit.has_value() || reach <= 0.0F) {
            continue;
        }
        // 4. The first met, as a fraction of the step.
        const float fraction = *hit / reach;
        if (fraction < bestFraction) {
            bestFraction = fraction;
            best = Blocker{.brain = other, .fraction = fraction};
        }
    }
    return best;
}

int giveWayStart(const human::Human& stander, anim::Vec3 moverPosition, anim::Vec3 moverStep) {
    // The closest point of the mover's line to the stander.
    const anim::Vec3 at = stander.position();
    const anim::Vec3 step = flat(moverStep);
    anim::Vec3 closest = flat(moverPosition);
    const float stepSquared = anim::dot(step, step);
    if (stepSquared > 0.0F) {
        const float along = anim::dot(flat(anim::subtract(at, moverPosition)), step) / stepSquared;
        closest = anim::add(closest, anim::scale(step, along));
    }
    // The sector of the way from it to him, carried on past him.
    const anim::Vec3 away = flat(anim::subtract(at, closest));
    return sectorOf(stander, anim::add(at, away));
}

std::optional<int> giveWaySector(Brain& stander, anim::Vec3 moverPosition, anim::Vec3 moverStep) {
    const int start = giveWayStart(stander.human(), moverPosition, moverStep);
    Sectors& sectors = stander.sectors(kSectorGiveWayAgeMs);
    for (const int offset : kGiveWayOrder) {
        if (sectors.free(stander, start + offset)) {
            return wrapSector(start + offset);
        }
    }
    return std::nullopt;
}

void setAvoiding(Brain& brain, Brain* who) {
    SteeringState& state = brain.steering();
    const bool was = state.avoiding != nullptr;
    state.avoiding = who;
    if (who != nullptr && !was) {
        brain.setTurnBoost(brain.turnBoost() + 1);
    } else if (who == nullptr && was) {
        brain.setTurnBoost(brain.turnBoost() - 1);
    }
}

void clearSteering(Brain& brain) {
    setAvoiding(brain, nullptr);
    brain.steering().score = -1e9F;
    brain.steering().sinceDetour = 0;
}

void resetAvoidance(Brain& brain) {
    SteeringState& state = brain.steering();
    state.heldPoint = {};
    setAvoiding(brain, nullptr);
    state.score = -1e9F;
    state.sinceDetour = 0;
    // The human's index staggers the slow counter's first x 10 (and, with a detail level, the decisions).
    state.slowDecisions = static_cast<std::uint8_t>(brain.slot() & 0xffU);
}

void setSteeringSpeed(Brain& brain, float speed, std::uint16_t updates) {
    brain.steering().overrideSpeed = speed;
    brain.steering().overrideLeft = updates;
}

float sideSign(anim::Vec3 origin, anim::Vec3 side, anim::Vec3 point) {
    return anim::dot(flat(side), flat(anim::subtract(point, origin))) >= 0.0F ? 1.0F : -1.0F;
}

bool segmentsCross(anim::Vec3 p1, anim::Vec3 p2, anim::Vec3 q1, anim::Vec3 q2) {
    return crossZ(p1, p2, q1) * crossZ(p1, p2, q2) < 0.0F && crossZ(q1, q2, p1) * crossZ(q1, q2, p2) < 0.0F;
}

float cornerRadius(anim::Vec3 from, anim::Vec3 to, anim::Vec3 axis) {
    const anim::Vec3 way = flat(anim::subtract(to, from));
    const float length = anim::length(way);
    if (length <= 0.0F) {
        return 0.0F;
    }
    const float across = std::fabs(anim::dot(anim::scale(way, 1.0F / length), unitFlat(axis)));
    return across > 0.0F ? length / (2.0F * across) : 0.0F;
}

float cornerSpeedLimit(const Brain& brain, anim::Vec3 point, float speed) {
    if (speed <= 0.0F) {
        return 0.0F;
    }
    const human::Human& human = brain.human();
    const human::Speeds& speeds = human.speeds();
    // 1. The circle tangent to the facing at the human through the point: its centre lies along his side axis.
    const anim::Vec3 forward = human::facing(human.heading());
    const float radius = cornerRadius(human.position(), point, anim::Vec3{forward.y, -forward.x, 0.0F});
    // 2. The speed whose chord in one update, turning at the gait's AI rate, stays on that circle.
    const auto allowed = [&](int gait) {
        const float turn = human::aiMaxTurn(static_cast<human::Gait>(gait), brain.turnBoost(), human.script().wounded);
        return 2.0F * radius * std::tan(turn * 0.5F) / human::kStepSeconds;
    };
    // 3. The speed's own gait, its allowance not capped by the gait's speed.
    const int start = static_cast<int>(human::gaitOfSpeed(speed, speeds));
    float best = allowed(start);
    if (best >= speed) {
        return speed;
    }
    // 4. Down the gaits while each allows at least as much, its own speed capping it.
    for (int gait = start - 1; gait >= 1; --gait) {
        const float trial = std::min(gaitSpeed(speeds, gait), allowed(gait));
        if (trial < best) {
            break;
        }
        best = trial;
    }
    // 5. Never above the speed asked.
    return std::min(speed, best);
}

bool groundProbeBelow(const raycast::CollisionMesh& mesh, anim::Vec3 point) {
    const raycast::Ray ray{.origin = raycast::Vec3{point.x, point.y, point.z + kGroundProbeLift},
                           .direction = raycast::kDown,
                           .length = kGroundProbeLength};
    const std::optional<raycast::RayHit> hit = mesh.rayCast(ray, {}, 0);
    return hit.has_value() && (hit->flags & kGroundProbeFlag) != 0;
}

bool tryDetour(Brain& brain, anim::Vec3 point, Brain& blocker, float fraction) {
    const RoutePlanner* planner = brain.planner();
    if (planner != nullptr && !planner->lineClear(brain.human().position(), point)) {
        return false;
    }
    if (brain.collision() != nullptr && groundProbeBelow(*brain.collision(), point)) {
        return false;
    }
    // The decisions since the detour are not restarted: a new detour taken while avoiding keeps the old count.
    setAvoiding(brain, &blocker);
    SteeringState& state = brain.steering();
    state.score = fraction;
    state.heldPoint = point;
    return true;
}

std::optional<anim::Vec3> steerAroundHumans(Brain& brain, const SteerRequest& request) {
    SteeringState& state = brain.steering();
    // The override counts down at every call, so one set now runs from the next update on.
    if (state.overrideLeft != 0) {
        --state.overrideLeft;
    }
    // 1. Off.
    if (!state.enabled) {
        return std::nullopt;
    }
    // 3. A decision (every update: the detail level is 0). While avoiding the arrival radius counts as 0, and a
    // walker at gait 0 heads on for the held point.
    state.score = -1e9F;
    const human::Human& human = brain.human();
    const anim::Vec3 position = human.position();
    const float speed = human.speed();
    float arrivalRadius = request.arrivalRadius;
    if (state.avoiding != nullptr) {
        arrivalRadius = 0.0F;
        if (speed < kSteerStandingSpeed) {
            return state.heldPoint;
        }
    }
    // 4. Nearly there.
    const anim::Vec3 toDestination = flat(anim::subtract(request.destination, position));
    if (anim::dot(toDestination, toDestination) <= kSteerNearDestination * kSteerNearDestination) {
        return std::nullopt;
    }
    // 5. The look-ahead: 0.75 s at the move's speed, once x 10 after 16 slow decisions in a row; the humans within
    // twice it.
    float lookAhead = kStepSeconds * request.moveSpeed;
    if (request.moveSpeed < kSlowMoveSpeed) {
        if (++state.slowDecisions >= kSlowDecisions) {
            lookAhead *= kSlowLookAheadScale;
            state.slowDecisions = 0;
        }
    } else {
        state.slowDecisions = 0;
    }
    const std::vector<Brain*> list = humansAround(brain, peersOf(brain), std::min(2.0F * lookAhead, kSteerListReach));
    // 6. A detour heads for its held point until it expires (4 x floor(11 - s) decisions).
    anim::Vec3 target = request.aim;
    if (state.avoiding != nullptr) {
        const float limit = 4.0F * std::floor(11.0F - speed);
        if (static_cast<float>(state.sinceDetour) < limit) {
            ++state.sinceDetour;
            target = state.heldPoint;
        } else {
            clearSteering(brain);
        }
    }
    // 7. My step: toward that point, at most the look-ahead and no farther than the way less the arrival radius.
    const anim::Vec3 way = flat(anim::subtract(target, position));
    const float wayLength = anim::length(way);
    if (wayLength <= 0.0F) {
        return keepHeld(brain);
    }
    const anim::Vec3 direction = anim::scale(way, 1.0F / wayLength);
    const anim::Vec3 step = anim::scale(direction, std::max(std::min(lookAhead, wayLength - arrivalRadius), 0.0F));
    // 8. The blocker.
    const std::optional<Blocker> blocker = findBlocker(brain, list, position, direction, step);
    if (!blocker.has_value()) {
        return keepHeld(brain);
    }
    Brain& other = *blocker->brain;
    const float t = blocker->fraction;
    // 9. A follower of my formation in the way stops for 5 updates.
    if (other.following() != nullptr && &other.following()->leader() == &brain) {
        setSteeringSpeed(other, 0.0F, 5);
    }
    // 10. A held detour whose contact is nearer stays.
    const anim::Vec3 contactStep = anim::scale(step, t);
    const float contactDistance = anim::length(contactStep);
    if (state.avoiding != nullptr &&
        anim::length(flat(anim::subtract(state.contactPoint, position))) < contactDistance) {
        return state.heldPoint;
    }
    state.contactPoint = anim::add(position, contactStep);
    // 11. The vectors: his position and predicted point, the bearing to him, his heading, and my side (perpendicular
    // to my move, 1 m); b says how straight ahead he is, h how alike our headings are.
    const human::Human& his = other.human();
    const anim::Vec3 at = his.position();
    const anim::Vec3 predicted =
        other.type() == BrainType::Player ? anim::add(at, flat(human::facing(his.heading()))) : other.moveAim();
    const anim::Vec3 bearing = unitFlat(anim::subtract(at, position));
    const anim::Vec3 hisHeading = unitFlat(anim::subtract(predicted, at));
    const anim::Vec3 side{direction.y, -direction.x, 0.0F};
    const float b = anim::dot(direction, bearing);
    const float h = anim::dot(direction, hisHeading);
    // 12. Right of way: the faster, the lower slot on a tie.
    const float hisSpeed = his.speed();
    const bool rightOfWay = speed > hisSpeed || (speed == hisSpeed && brain.slot() < other.slot());
    // 13-14. The case.
    SteerCase steerCase = SteerCase::Crossing;
    if (standing(other)) {
        steerCase = SteerCase::Standing;
    } else if (b >= kCos30 && h < -kCos45) {
        steerCase = SteerCase::HeadOn;
    } else if (b >= kCos30 && h > kCos45) {
        // Overtaking needs right of way; without it, follow.
        if (!rightOfWay) {
            return std::nullopt;
        }
        steerCase = SteerCase::Overtake;
    } else if (b >= kCos50 && b < kCos30 && std::fabs(h) > kCos45) {
        steerCase = SteerCase::Overtake;
    } else if (b < kCos50 && h >= kCos45) {
        // Off to the side and going the same way: no detour; without right of way, slow behind him.
        if (!rightOfWay) {
            setSteeringSpeed(brain, kSteerSlowShare * speed, 15);
        }
        return std::nullopt;
    }
    // 15. Both following routes to the same node, and I am the one held there (he is nearer it, so he would keep the
    // node's claim): follow him at 0.75 x his speed.
    if (brain.routeNode().has_value() && brain.routeNode() == other.routeNode() &&
        anim::length(flat(anim::subtract(request.aim, at))) <
            anim::length(flat(anim::subtract(request.aim, position)))) {
        setSteeringSpeed(brain, kSteerSlowShare * hisSpeed, 5);
        return request.aim;
    }
    // 16. Yielding to a near contact: stop for him, or match his speed when overtaking.
    if (steerCase != SteerCase::Standing && !rightOfWay && contactDistance < kYieldContact) {
        if (steerCase == SteerCase::Overtake) {
            setSteeringSpeed(brain, hisSpeed, 5);
        } else {
            setSteeringSpeed(brain, 0.0F, 1);
        }
        return request.aim;
    }
    // 17. The detour point.
    if (steerCase == SteerCase::Standing) {
        // 1 m from his centre, perpendicular to my move, on the side my line passes; else 1 m out in his sector
        // opposite the one it lies in.
        const float sign = anim::dot(side, bearing) >= 0.0F ? -1.0F : 1.0F;
        const anim::Vec3 beside = anim::add(at, anim::scale(side, sign));
        if (tryDetour(brain, beside, other, t)) {
            return beside;
        }
        (void)other.sectors(kSectorAgeMs);
        const anim::Vec3 opposite = sectorPoint(his, sectorOf(human, beside) + (kSectorCount / 2), 1.0F);
        return tryDetour(brain, opposite, other, t) ? opposite : beside;
    }
    anim::Vec3 detour;
    if (steerCase == SteerCase::Crossing) {
        // Where he will be when we meet, then 1 m to the side he comes from.
        const float sign = anim::dot(hisHeading, side) >= 0.0F ? -1.0F : 1.0F;
        const auto& move = his.record().move;
        const float hisMoveSpeed = move.has_value() ? move->speed : 0.0F;
        const anim::Vec3 ahead = clampStep(at, other.moveAim(), hisMoveSpeed * kStepSeconds * t);
        detour = anim::add(anim::add(at, ahead), anim::scale(side, sign));
    } else {
        // 1 m sideways from the contact: away from his side, or toward it when our paths cross.
        const float hisSide = sideSign(position, side, at);
        const float crossing = segmentsCross(position, request.aim, at, predicted) ? 1.0F : -1.0F;
        detour = anim::add(state.contactPoint, anim::scale(side, crossing * hisSide));
    }
    // 18. Too fast to turn to it: slow for an update instead.
    if (speed - cornerSpeedLimit(brain, detour, speed) > 1.0F) {
        setSteeringSpeed(brain, kSteerSlowShare * speed, 1);
        return request.aim;
    }
    // 19. The detour. The move's own aim is what is checked and held (the original's, kept): the detour point
    // steers this update only.
    (void)tryDetour(brain, request.aim, other, t);
    return detour;
}

} // namespace coney::ai
