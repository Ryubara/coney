// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/loose_objects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iterator>

// Research: docs/research/physics.md#settle, docs/research/physics.md#movers

namespace coney::world_objects {

namespace {

// `a · b`: the rotation that applies `b`, then `a`.
anim::Quat multiply(anim::Quat a, anim::Quat b) {
    return anim::Quat{
        (a.w * b.x) + (a.x * b.w) + (a.y * b.z) - (a.z * b.y), (a.w * b.y) - (a.x * b.z) + (a.y * b.w) + (a.z * b.x),
        (a.w * b.z) + (a.x * b.y) - (a.y * b.x) + (a.z * b.w), (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z)};
}

// `v` turned by the unit quaternion `q`.
anim::Vec3 rotate(anim::Quat q, anim::Vec3 v) {
    const anim::Quat r = multiply(multiply(q, anim::Quat{v.x, v.y, v.z, 0.0F}), anim::Quat{-q.x, -q.y, -q.z, q.w});
    return anim::Vec3{r.x, r.y, r.z};
}

// The shortest turn taking unit vector `from` onto unit vector `to`; they are never opposite here (the settle wraps
// its turn to ±90°).
anim::Quat shortestArc(anim::Vec3 from, anim::Vec3 to) {
    const anim::Vec3 axis = anim::cross(from, to);
    return anim::normalise(anim::Quat{axis.x, axis.y, axis.z, 1.0F + anim::dot(from, to)});
}

// `q += ½ ω q dt`, normalised: the rotation after spinning at `omega` for `dt`.
// @orig 0x00335b08 Quat_IntegrateAngular (unknown)
anim::Quat integrateAngular(anim::Quat q, anim::Vec3 omega, float dt) {
    const anim::Quat spin = multiply(anim::Quat{omega.x, omega.y, omega.z, 0.0F}, q);
    const float h = 0.5F * dt;
    return anim::normalise(anim::Quat{q.x + (spin.x * h), q.y + (spin.y * h), q.z + (spin.z * h), q.w + (spin.w * h)});
}

// The local unit axes, by index.
constexpr std::array<anim::Vec3, 3> kAxes{anim::Vec3{1, 0, 0}, anim::Vec3{0, 1, 0}, anim::Vec3{0, 0, 1}};

// A move shorter than this is no move.
constexpr float kMinMove = 1e-6F;

} // namespace

std::optional<int> settleAxis(anim::Quat rotation, anim::Vec3 normal, int axisMask) {
    std::optional<int> best;
    float bestDot = 0.0F;
    for (int i = 0; i < 3; ++i) {
        if ((axisMask & (1 << i)) == 0) {
            continue;
        }
        // The positive direction only: the largest dot product is the smallest angle.
        const float d = anim::dot(rotate(rotation, kAxes.at(static_cast<std::size_t>(i))), normal);
        if (!best || d > bestDot) {
            best = i;
            bestDot = d;
        }
    }
    return best;
}

anim::Quat settleTarget(anim::Quat rotation, anim::Vec3 normal, int axisMask) {
    const std::optional<int> axis = settleAxis(rotation, normal, axisMask & kSettleAxesMask);
    if (!axis) {
        return rotation;
    }
    anim::Vec3 world = rotate(rotation, kAxes.at(static_cast<std::size_t>(*axis)));
    // Wrapped to ±90°: an axis pointing away from the normal lines up its negative direction instead.
    if (anim::dot(world, normal) < 0.0F) {
        world = anim::scale(world, -1.0F);
    }
    return anim::normalise(multiply(shortestArc(anim::normalise(world), anim::normalise(normal)), rotation));
}

bool SettleTurn::tick() {
    t = std::min(t + speed, 1.0F);
    speed += kAcceleration;
    return t >= 1.0F;
}

anim::Quat SettleTurn::rotation() const { return anim::slerp(start, end, t); }

LooseShape LooseShape::of(const ObjectType& type) {
    LooseShape shape;
    shape.centre = anim::Vec3{type.bodyCentre[0], type.bodyCentre[1], type.bodyCentre[2]};
    if (type.bodyShape == kBodyBox) {
        shape.kind = kBodyBox;
        shape.halfExtents = anim::Vec3{type.bodySize[0] / 2.0F, type.bodySize[1] / 2.0F, type.bodySize[2] / 2.0F};
    } else if (type.bodyShape == kBodySphere) {
        shape.kind = kBodySphere;
        shape.halfExtents = anim::Vec3{type.bodySize[0] / 2.0F, 0.0F, 0.0F};
    }
    return shape;
}

float LooseShape::reach(anim::Quat rotation, anim::Vec3 d) const {
    if (kind == kBodySphere) {
        return halfExtents.x;
    }
    if (kind != kBodyBox) {
        return 0.0F;
    }
    // A box reaches along d by the sum of its half-extents, each weighted by how far its axis points along d.
    return (std::fabs(anim::dot(rotate(rotation, kAxes[0]), d)) * halfExtents.x) +
           (std::fabs(anim::dot(rotate(rotation, kAxes[1]), d)) * halfExtents.y) +
           (std::fabs(anim::dot(rotate(rotation, kAxes[2]), d)) * halfExtents.z);
}

void LooseObjects::start(double handle, anim::Vec3 position, anim::Quat rotation, const Kind& kind, anim::Vec3 velocity,
                         anim::Vec3 angularVelocity) {
    LooseObject object;
    object.position = position;
    object.rotation = rotation;
    object.velocity = velocity;
    object.angularVelocity = angularVelocity;
    object.shape = kind.shape;
    object.axisMask = kind.axisMask;
    object.restitution = kind.restitution;
    m_objects.insert_or_assign(handle, object);
}

void LooseObjects::step(const RayTest& ray, const std::function<void(double, const LooseObject&)>& rested) {
    // The objects' updates first (the wheel), then the physics step's settle ticks, as one 1/30 s step holds them.
    for (auto& [handle, object] : m_objects) {
        if (object.airborne) {
            update(object, ray);
        }
    }
    for (int tick = 0; tick < kTicksPerStep; ++tick) {
        for (auto& [handle, object] : m_objects) {
            if (!object.settle) {
                continue;
            }
            const bool done = object.settle->tick();
            object.rotation = object.settle->rotation();
            if (done) {
                object.settle.reset();
                object.settling = false;
                object.settled = true;
            }
        }
    }
    // An object at rest is no longer in flight.
    for (auto it = m_objects.begin(); it != m_objects.end();) {
        if (!it->second.airborne && !it->second.settle) {
            if (rested) {
                rested(it->first, it->second);
            }
            it = m_objects.erase(it);
        } else {
            ++it;
        }
    }
}

const LooseObject* LooseObjects::find(double handle) const {
    const auto it = m_objects.find(handle);
    return it != m_objects.end() ? &it->second : nullptr;
}

std::size_t LooseObjects::settlesInUse() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_objects, [](const auto& entry) { return entry.second.settle.has_value(); }));
}

void LooseObjects::update(LooseObject& object, const RayTest& ray) {
    // The update after the holder let go, and every update while a settle turns the object, lasts 1/60 s; the others
    // their interval of 2 ticks.
    const float dt = object.firstUpdate || object.settling ? kShortUpdateSeconds : kUpdateSeconds;
    object.firstUpdate = false;
    // Velocity first, then the move with it.
    object.velocity.z -= kGravity * dt;
    const anim::Vec3 move = anim::scale(object.velocity, dt);
    const float length = anim::length(move);
    if (length > kMinMove) {
        const anim::Vec3 d = anim::scale(move, 1.0F / length);
        const anim::Vec3 centre = anim::add(object.position, rotate(object.rotation, object.shape.centre));
        const std::optional<RayContact> hit =
            ray ? ray(centre, d, length + object.shape.reach(object.rotation, d)) : std::nullopt;
        if (hit) {
            contact(object, anim::add(centre, anim::scale(d, hit->distance)), *hit);
        } else {
            object.position = anim::add(object.position, move);
        }
    }
    // The spin turns it unless a settle does.
    if (!object.settling && object.airborne) {
        object.rotation = integrateAngular(object.rotation, object.angularVelocity, dt);
    }
}

void LooseObjects::contact(LooseObject& object, anim::Vec3 point, const RayContact& contact) {
    const anim::Vec3 n = anim::normalise(contact.normal);
    const bool floor = !contact.body && n.z > kFloorNormalZ;
    // Moved back to kBackOff in front of the surface.
    const float reach = object.shape.reach(object.rotation, anim::scale(n, -1.0F));
    const anim::Vec3 centre = anim::add(point, anim::scale(n, reach + kBackOff));
    object.position = anim::subtract(centre, rotate(object.rotation, object.shape.centre));
    // A settled object's floor contact (answer 0x10003): it stops, at rest.
    if (floor && object.settled) {
        object.velocity = {};
        object.angularVelocity = {};
        object.airborne = false;
        object.settled = false;
        object.grounded = true;
        return;
    }
    // The first floor contact starts a settle onto the nearest face the type allows, while a slot is free.
    if (floor && !object.settling && (object.axisMask & kSettleAxesMask) != 0 && settlesInUse() < kSettleSlots) {
        object.settle = SettleTurn{.start = object.rotation,
                                   .end = settleTarget(object.rotation, n, object.axisMask),
                                   .t = 0.0F,
                                   .speed = 0.0F};
        object.settling = true;
        object.angularVelocity = {};
    }
    // Answer 2 (0x20002 on a floor): the velocity bounces off the normal with the type's restitution.
    // @orig 0x0033d8f0 PhysicsVec_Bounce (physics.cpp)
    const float into = anim::dot(object.velocity, n);
    if (into < 0.0F) {
        object.velocity = anim::subtract(object.velocity, anim::scale(n, (1.0F + object.restitution) * into));
    }
    // On a floor the move keeps the mesh's contact scale: the velocity scaled by it, its z only when upward.
    if (floor) {
        object.velocity.x *= kMeshContactScale;
        object.velocity.y *= kMeshContactScale;
        if (object.velocity.z > 0.0F) {
            object.velocity.z *= kMeshContactScale;
        }
    }
}

} // namespace coney::world_objects
