// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "animation/anim_task.h"

namespace coney::human {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
// The sweep slides along what it hits up to this many times.
constexpr int kSweepPasses = 3;
// A push smaller than this means the body is clear of the wall.
constexpr float kClear = 1e-4F;

raycast::Vec3 toMesh(anim::Vec3 v) { return raycast::Vec3{v.x, v.y, v.z}; }
anim::Vec3 fromMesh(raycast::Vec3 v) { return anim::Vec3{v.x, v.y, v.z}; }

// The point of the triangle `a b c` nearest `p`: inside the face, on an edge or at a corner (by the triangle's
// Voronoi regions).
anim::Vec3 closestOnTriangle(anim::Vec3 p, anim::Vec3 a, anim::Vec3 b, anim::Vec3 c) {
    const anim::Vec3 ab = anim::subtract(b, a);
    const anim::Vec3 ac = anim::subtract(c, a);
    const anim::Vec3 ap = anim::subtract(p, a);
    const float d1 = anim::dot(ab, ap);
    const float d2 = anim::dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return a;
    }
    const anim::Vec3 bp = anim::subtract(p, b);
    const float d3 = anim::dot(ab, bp);
    const float d4 = anim::dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return anim::add(a, anim::scale(ab, d1 / (d1 - d3)));
    }
    const anim::Vec3 cp = anim::subtract(p, c);
    const float d5 = anim::dot(ab, cp);
    const float d6 = anim::dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return anim::add(a, anim::scale(ac, d2 / (d2 - d6)));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        return anim::add(b, anim::scale(anim::subtract(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }
    const float denominator = 1.0F / (va + vb + vc);
    return anim::add(a, anim::add(anim::scale(ab, vb * denominator), anim::scale(ac, vc * denominator)));
}

} // namespace

std::optional<anim::Vec3> nearestWallPush(const raycast::CollisionMesh& mesh, anim::Vec3 centre, float radius,
                                          float maxNormalZ, std::vector<std::uint16_t>& scratch) {
    // The triangles listed in the cells the sphere's box covers, each once.
    const auto low = mesh.cellOf(toMesh(anim::subtract(centre, anim::Vec3{radius, radius, radius})));
    const auto high = mesh.cellOf(toMesh(anim::add(centre, anim::Vec3{radius, radius, radius})));
    scratch.clear();
    for (std::uint32_t z = std::min(low[2], high[2]); z <= std::max(low[2], high[2]); ++z) {
        for (std::uint32_t y = std::min(low[1], high[1]); y <= std::max(low[1], high[1]); ++y) {
            for (std::uint32_t x = std::min(low[0], high[0]); x <= std::max(low[0], high[0]); ++x) {
                const auto list = mesh.cellTriangles(x, y, z);
                scratch.insert(scratch.end(), list.begin(), list.end());
            }
        }
    }
    std::ranges::sort(scratch);
    const auto [first, last] = std::ranges::unique(scratch);
    scratch.erase(first, last);

    // The nearest enabled wall whose face the sphere reaches.
    std::optional<anim::Vec3> push;
    float nearest = radius;
    const auto triangles = mesh.triangles();
    const auto vertices = mesh.vertices();
    for (const std::uint16_t index : scratch) {
        const raycast::CollisionTriangle& triangle = triangles[index];
        if ((triangle.flags & raycast::kTriangleEnabled) == 0) {
            continue;
        }
        anim::Vec3 n = fromMesh(mesh.faceNormal(index));
        if (anim::length(n) < 0.5F || std::abs(n.z) > maxNormalZ) {
            continue; // degenerate, or a floor or ceiling
        }
        const anim::Vec3 a = fromMesh(vertices[triangle.vertices[0]]);
        const anim::Vec3 b = fromMesh(vertices[triangle.vertices[1]]);
        const anim::Vec3 c = fromMesh(vertices[triangle.vertices[2]]);
        float distance = anim::dot(n, anim::subtract(centre, a));
        if (distance < 0.0F) {
            if ((triangle.flags & raycast::kTriangleTwoSided) == 0) {
                continue; // behind a one-sided wall
            }
            n = anim::scale(n, -1.0F);
            distance = -distance;
        }
        // How near the sphere is to the triangle: to its closest point, so that an edge or a corner between two
        // walls holds the body as well as their faces do. The push is along the face's normal.
        const float reach = anim::distance(centre, closestOnTriangle(centre, a, b, c));
        if (reach >= nearest) {
            continue;
        }
        nearest = reach;
        push = anim::scale(n, radius - distance);
    }
    return push;
}

Human::Human(const characters::AnimSet& anims, const AnimSlots& slots,
             std::span<const anim::Quat, anim::kPoseBones> bindRotations)
    : m_animator(anims, slots) {
    std::ranges::copy(bindRotations, m_bindRotations.begin());
}

float Human::speed() const { return std::hypot(m_velocity.x, m_velocity.y); }

void Human::spawn(const raycast::CollisionMesh* mesh, anim::Vec3 position, float headingDegrees) {
    m_heading = wrapAngle(headingDegrees * kPi / 180.0F);
    m_velocity = anim::Vec3{};
    m_airborne = false;
    m_outOfWorld = false;
    m_airborneUpdates = 0;
    m_blockedUpdates = 0;
    m_turn = TurnState{};
    // Creation's snap: a ray from 1 m above, 2.5 m down; on a hit the feet go 0.01 above it.
    if (mesh != nullptr) {
        const raycast::Ray ray{.origin = toMesh(anim::add(position, anim::Vec3{0.0F, 0.0F, kSnapAbove})),
                               .direction = raycast::kDown,
                               .length = kSpawnLength};
        if (const auto hit = mesh->rayCast(ray, {}, 0); hit) {
            position.z = position.z + kSnapAbove - hit->t + kSpawnGap;
            m_groundNormal = fromMesh(hit->normal);
        }
    }
    m_position = position;
    m_lastGround = position;
}

void Human::locomote() {
    const Speeds& speeds = m_animator.speeds();
    const float current = speed();
    const float target = targetSpeed(m_intent.magnitude, speeds);
    const float wanted = m_intent.angle - kPi / 2.0F; // the stick's heading
    const Gait gaitNow = gaitOfSpeed(current, speeds);
    float newSpeed = approachSpeed(current, target, kStepSeconds);

    // A run stopped hard or turned back skids: the velocity is zeroed.
    const anim::Vec3 moving =
        current > 1e-6F ? anim::scale(anim::Vec3{m_velocity.x, m_velocity.y, 0.0F}, 1.0F / current) : facing(m_heading);
    if (skids(gaitNow, current, speeds, m_lastMagnitude, m_intent.magnitude, moving, facing(wanted))) {
        newSpeed = 0.0F;
    } else if (target > 0.0F) {
        // Turn toward the stick, limited by the gait and eased.
        m_heading = turnToward(m_heading, wanted, maxTurn(gaitNow), m_turn);
    }
    // While a start clip plays the clip alone moves the human; otherwise the velocity follows the facing.
    const anim::Vec3 direction = facing(m_heading);
    const float horizontal = m_animator.startClipPlaying() ? 0.0F : newSpeed;
    m_velocity = anim::Vec3{direction.x * horizontal, direction.y * horizontal, m_velocity.z};
}

void Human::applyRootMotion(const anim::Pose& pose) {
    const anim::RootMotion root = anim::rootMotionOf(pose);
    // The clip's velocity is in the character's axes (facing +y): turn it by the heading, cap it, add it.
    const float c = std::cos(m_heading);
    const float s = std::sin(m_heading);
    anim::Vec3 world{root.velocity.x * c - root.velocity.y * s, root.velocity.x * s + root.velocity.y * c,
                     root.velocity.z};
    if (const float length = anim::length(world); length > kMaxSpeed) {
        world = anim::scale(world, kMaxSpeed / length);
    }
    m_velocity.x += world.x;
    m_velocity.y += world.y;
    // The turn is per 1/30 s.
    m_heading = wrapAngle(m_heading + root.turn * 30.0F * kStepSeconds);
}

std::optional<anim::Vec3> Human::sweep(const raycast::CollisionMesh& mesh, anim::Vec3 from, anim::Vec3 displacement) {
    // Move, then push the body's sphere out of the nearest wall; each push slides it along that wall. Still in a wall
    // after three passes: blocked.
    anim::Vec3 feet = anim::add(from, displacement);
    for (int pass = 0; pass < kSweepPasses; ++pass) {
        const anim::Vec3 centre = anim::add(feet, anim::Vec3{0.0F, 0.0F, kBodyCentreHeight});
        const auto push = nearestWallPush(mesh, centre, kBodyRadius, kFloorNormalZ, m_nearby);
        if (!push || anim::length(*push) < kClear) {
            return feet;
        }
        feet = anim::add(feet, *push);
    }
    const anim::Vec3 centre = anim::add(feet, anim::Vec3{0.0F, 0.0F, kBodyCentreHeight});
    const auto push = nearestWallPush(mesh, centre, kBodyRadius, kFloorNormalZ, m_nearby);
    if (!push || anim::length(*push) < kClear) {
        return feet;
    }
    return std::nullopt;
}

void Human::snapToGround(const raycast::CollisionMesh& mesh, anim::Vec3 feet) {
    const raycast::Ray ray{.origin = toMesh(anim::add(feet, anim::Vec3{0.0F, 0.0F, kSnapAbove})),
                           .direction = raycast::kDown,
                           .length = kSnapLength};
    if (const auto hit = mesh.rayCast(ray, {}, 0); hit) {
        // The feet go exactly onto the hit.
        feet.z = feet.z + kSnapAbove - hit->t;
        m_position = feet;
        m_lastGround = feet;
        m_groundNormal = fromMesh(hit->normal);
        return;
    }
    // Nothing within 0.5 m below the feet: the human starts to fall.
    m_position = feet;
    m_airborne = true;
    m_airborneUpdates = 0;
}

void Human::land(anim::Vec3 feet) {
    m_lastLandingSpeed = m_velocity.z;
    m_position = feet;
    m_lastGround = feet;
    m_airborne = false;
    m_airborneUpdates = 0;
    m_velocity.z = 0.0F;
}

void Human::moveOnGround(const raycast::CollisionMesh& mesh) {
    // On the ground the velocity has no z: walking climbs only through the snap. A slope slows the human, uphill and
    // downhill alike.
    m_velocity.z = 0.0F;
    const float factor = slopeFactor(m_groundNormal.z);
    const anim::Vec3 displacement{m_velocity.x * factor * kStepSeconds, m_velocity.y * factor * kStepSeconds, 0.0F};
    anim::Vec3 feet = m_position;
    if (const auto moved = sweep(mesh, m_position, displacement); moved) {
        feet = *moved;
        m_blockedUpdates = 0;
    } else {
        // Blocked: the body stays and its horizontal velocity goes.
        m_velocity.x = 0.0F;
        m_velocity.y = 0.0F;
        ++m_blockedUpdates;
    }
    snapToGround(mesh, feet);
}

void Human::moveInAir(const raycast::CollisionMesh& mesh) {
    const anim::Vec3 displacement = anim::scale(m_velocity, kStepSeconds);
    anim::Vec3 feet = anim::add(m_position, displacement);
    // Walls still push the falling body: sweep horizontally, then fall straight.
    if (const auto moved = sweep(mesh, m_position, anim::Vec3{displacement.x, displacement.y, 0.0F}); moved) {
        feet = anim::Vec3{moved->x, moved->y, m_position.z + displacement.z};
        m_blockedUpdates = 0;
    } else {
        feet = anim::Vec3{m_position.x, m_position.y, m_position.z + displacement.z};
        ++m_blockedUpdates;
    }
    // The landing test: the segment from the body's upper point to the moved feet; a floor on it is a landing.
    const anim::Vec3 top = anim::add(m_position, anim::Vec3{0.0F, 0.0F, kLandingTestHeight});
    const anim::Vec3 segment = anim::subtract(feet, top);
    const float length = anim::length(segment);
    if (length > 1e-6F) {
        const raycast::Ray ray{
            .origin = toMesh(top), .direction = toMesh(anim::scale(segment, 1.0F / length)), .length = length};
        if (const auto hit = mesh.rayCast(ray, {}, 0); hit && hit->normal.z > kFloorNormalZ) {
            land(anim::add(top, anim::scale(segment, hit->t / length)));
            m_groundNormal = fromMesh(hit->normal);
            return;
        }
    }
    m_position = feet;
}

void Human::step(const HumanInput& input, const raycast::CollisionMesh* mesh) {
    // 1. The stick, turned by the camera.
    m_lastMagnitude = m_intent.magnitude;
    m_intent = stickIntent(input.stickX, input.stickY, input.cameraForward);
    if (m_outOfWorld) {
        return;
    }
    // 2. The animation's step, and the root motion its pose carries.
    m_animator.advance(kStepSeconds);
    const anim::Pose pose = m_animator.pose(m_bindRotations);
    // 3. The locomotion sets the velocity; the clip's root motion is added to it.
    locomote();
    applyRootMotion(pose);
    // 4. A velocity this long is a bug: drop it.
    if (anim::length(m_velocity) > kMaxSpeed) {
        m_velocity = anim::Vec3{};
    }
    // 5. Gravity while airborne, from the second airborne update, capped.
    if (m_airborne) {
        ++m_airborneUpdates;
        if (m_airborneUpdates >= 2) {
            m_velocity.z = std::max(m_velocity.z - kGravity * kStepSeconds, -kMaxFallSpeed);
        }
    } else {
        m_airborneUpdates = 0;
    }
    if (mesh != nullptr) {
        // 6. Out of the world: far below the lowest ground, the human stops (a mission failure in the original).
        if (m_position.z < mesh->lowestZ() - kOutOfWorldDepth) {
            m_outOfWorld = true;
            m_velocity = anim::Vec3{};
            return;
        }
        // 7. Stuck in the air for two seconds: back to the last ground.
        if (m_airborne && m_airborneUpdates > kStuckUpdates && m_blockedUpdates >= kStuckUpdates) {
            land(m_lastGround);
        } else if (m_airborne) {
            moveInAir(*mesh);
        } else {
            moveOnGround(*mesh);
        }
    } else {
        m_position = anim::add(m_position, anim::scale(m_velocity, kStepSeconds));
    }
    // 8. The animation state for what the human now does.
    m_animator.choose(AnimInputs{.speed = speed(),
                                 .wantsMove = targetSpeed(m_intent.magnitude, speeds()) > 0.0F,
                                 .wantsRun = m_intent.magnitude > kRunThreshold,
                                 .airborne = m_airborne});
}

} // namespace coney::human
