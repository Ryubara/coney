// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_bodies.h"

#include <algorithm>
#include <cmath>

namespace coney::world_objects {

namespace {

// Below this a push or a distance counts as none.
constexpr float kTiny = 1e-5F;

// The component of `v` along axis `i` (0 x, 1 y, 2 z).
float axisOf(anim::Vec3 v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }

// `v` with component `i` set to `value`.
anim::Vec3 withAxis(anim::Vec3 v, int i, float value) {
    if (i == 0) {
        v.x = value;
    } else if (i == 1) {
        v.y = value;
    } else {
        v.z = value;
    }
    return v;
}

} // namespace

std::optional<ObjectBody> bodyOf(const SpawnRecord& record, const ObjectType& type) {
    if (type.bodyShape != kBodyBox && type.bodyShape != kBodySphere) {
        return std::nullopt;
    }
    const anim::Quat rotation =
        anim::normalise(anim::Quat{record.rotation[0], record.rotation[1], record.rotation[2], record.rotation[3]});
    const anim::Vec3 origin{record.position[0], record.position[1], record.position[2]};
    anim::Mat34 pose = anim::transform(rotation, origin);
    // The type's centre is an offset in the object's own frame.
    pose.t = anim::transformPoint(pose, anim::Vec3{type.bodyCentre[0], type.bodyCentre[1], type.bodyCentre[2]});
    ObjectBody body{.handle = record.handle, .shape = type.bodyShape, .pose = pose, .layers = type.bodyWord};
    if (type.bodyShape == kBodyBox) {
        body.half = anim::Vec3{type.bodySize[0] / 2.0F, type.bodySize[1] / 2.0F, type.bodySize[2] / 2.0F};
        if (body.half.x <= 0.0F || body.half.y <= 0.0F || body.half.z <= 0.0F) {
            return std::nullopt;
        }
    } else {
        body.radius = type.bodySize[0] / 2.0F;
        if (body.radius <= 0.0F) {
            return std::nullopt;
        }
    }
    return body;
}

std::optional<anim::Vec3> spherePush(const ObjectBody& body, anim::Vec3 centre, float radius) {
    if (body.shape == kBodySphere) {
        const anim::Vec3 away = anim::subtract(centre, body.pose.t);
        const float d = anim::length(away);
        const float reach = radius + body.radius;
        if (d >= reach) {
            return std::nullopt;
        }
        // Concentric: out along x, so the push is never undefined.
        const anim::Vec3 way = d > kTiny ? anim::scale(away, 1.0F / d) : anim::Vec3{1.0F, 0.0F, 0.0F};
        return anim::scale(way, reach - d);
    }
    // The box: the centre in its frame, clamped to the box for the closest point.
    const anim::Mat34 toLocal = anim::inverseRigid(body.pose);
    const anim::Vec3 local = anim::transformPoint(toLocal, centre);
    const anim::Vec3 closest{std::clamp(local.x, -body.half.x, body.half.x),
                             std::clamp(local.y, -body.half.y, body.half.y),
                             std::clamp(local.z, -body.half.z, body.half.z)};
    const anim::Vec3 away = anim::subtract(local, closest);
    const float d = anim::length(away);
    anim::Vec3 push{};
    if (d > kTiny) {
        if (d >= radius) {
            return std::nullopt;
        }
        push = anim::scale(away, (radius - d) / d);
    } else {
        // Inside: out through the face nearest the centre.
        int best = 0;
        float least = body.half.x - std::fabs(local.x);
        for (int i = 1; i < 3; ++i) {
            const float gap = axisOf(body.half, i) - std::fabs(axisOf(local, i));
            if (gap < least) {
                least = gap;
                best = i;
            }
        }
        const float side = axisOf(local, best) >= 0.0F ? 1.0F : -1.0F;
        push = withAxis(anim::Vec3{}, best, side * (least + radius));
    }
    return anim::transformDirection(body.pose, push);
}

std::optional<anim::Vec3> ObjectBodies::pushOut(anim::Vec3 centre, float radius, int layers, anim::Vec3 move) const {
    std::optional<anim::Vec3> deepest;
    float most = 0.0F;
    const bool moving = anim::length(move) > kTiny;
    for (const ObjectBody& body : m_bodies) {
        if ((body.layers & layers) == 0) {
            continue;
        }
        const std::optional<anim::Vec3> push = spherePush(body, centre, radius);
        if (!push) {
            continue;
        }
        const anim::Vec3 flat{push->x, push->y, 0.0F};
        const float size = anim::length(flat);
        if (size < kTiny || (moving && anim::dot(flat, move) > 0.0F)) {
            continue;
        }
        if (size > most) {
            most = size;
            deepest = flat;
        }
    }
    return deepest;
}

std::vector<double> ObjectBodies::touching(anim::Vec3 centre, float radius, int layers) const {
    std::vector<double> found;
    for (const ObjectBody& body : m_bodies) {
        if ((body.layers & layers) != 0 && spherePush(body, centre, radius).has_value()) {
            found.push_back(body.handle);
        }
    }
    return found;
}

} // namespace coney::world_objects
