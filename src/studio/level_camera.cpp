// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/level_camera.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace coney::studio {

namespace {

// Radians per degree.
constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;
// Below this a determinant or direction counts as zero.
constexpr float kEpsilon = 1e-7F;
// Radians the camera turns per pixel at sensitivity 1 on a display at scale 1
// (docs/guides/studio/level-view.md#fly-look): lower than the first build's 0.004, which the maintainer found too fast.
constexpr float kLookPerScaleOnePixel = 0.0025F;
// The acceleration grows by this factor every second of flying and stops at kMaxAcceleration
// (docs/guides/studio/level-view.md#fly-acceleration).
constexpr float kAccelerationPerSecond = 1.8F;
constexpr float kMaxAcceleration = 8.0F;

} // namespace

std::optional<PanelPoint> project(const LevelView& view, anim::Vec3 point) {
    const anim::Vec3 d = anim::subtract(point, view.pose.position);
    const float depth = anim::dot(d, view.pose.forward);
    if (depth <= view.nearClip) {
        return std::nullopt;
    }
    const float x = anim::dot(d, view.pose.right) / (depth * view.halfWidth);
    const float y = anim::dot(d, view.pose.up) / (depth * view.halfHeight);
    return PanelPoint{.x = (x + 1.0F) * 0.5F * view.width, .y = (1.0F - y) * 0.5F * view.height};
}

Ray rayThrough(const LevelView& view, PanelPoint point) {
    const float x = ((2.0F * point.x / view.width) - 1.0F) * view.halfWidth;
    const float y = (1.0F - (2.0F * point.y / view.height)) * view.halfHeight;
    const anim::Vec3 direction =
        anim::add(view.pose.forward, anim::add(anim::scale(view.pose.right, x), anim::scale(view.pose.up, y)));
    return Ray{.origin = view.pose.position, .direction = anim::normalise(direction)};
}

std::optional<float> hitTriangle(const Ray& ray, anim::Vec3 a, anim::Vec3 b, anim::Vec3 c) {
    // Möller and Trumbore's test, either side of the triangle.
    const anim::Vec3 ab = anim::subtract(b, a);
    const anim::Vec3 ac = anim::subtract(c, a);
    const anim::Vec3 p = anim::cross(ray.direction, ac);
    const float determinant = anim::dot(ab, p);
    if (std::abs(determinant) < kEpsilon) {
        return std::nullopt;
    }
    const float inverse = 1.0F / determinant;
    const anim::Vec3 s = anim::subtract(ray.origin, a);
    const float u = anim::dot(s, p) * inverse;
    if (u < 0.0F || u > 1.0F) {
        return std::nullopt;
    }
    const anim::Vec3 q = anim::cross(s, ab);
    const float v = anim::dot(ray.direction, q) * inverse;
    if (v < 0.0F || u + v > 1.0F) {
        return std::nullopt;
    }
    const float t = anim::dot(ac, q) * inverse;
    return t > 0.0F ? std::optional<float>(t) : std::nullopt;
}

std::optional<float> hitBounds(const Ray& ray, const Bounds& box) {
    // The slab test: where the ray enters and leaves each pair of faces.
    float enter = 0.0F;
    float leave = std::numeric_limits<float>::max();
    const float origin[3] = {ray.origin.x, ray.origin.y, ray.origin.z};
    const float direction[3] = {ray.direction.x, ray.direction.y, ray.direction.z};
    const float low[3] = {box.min.x, box.min.y, box.min.z};
    const float high[3] = {box.max.x, box.max.y, box.max.z};
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < kEpsilon) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        float near = (low[axis] - origin[axis]) / direction[axis];
        float far = (high[axis] - origin[axis]) / direction[axis];
        if (near > far) {
            std::swap(near, far);
        }
        enter = std::max(enter, near);
        leave = std::min(leave, far);
        if (enter > leave) {
            return std::nullopt;
        }
    }
    return enter;
}

std::optional<EntityId> pickEntity(const LevelDocument& level, const LevelView& view, PanelPoint point) {
    const Ray ray = rayThrough(view, point);
    std::optional<EntityId> best;
    float nearest = std::numeric_limits<float>::max();
    // The primitives' own triangles, so a click between the arms of a shape passes through.
    const auto& mesh = level.pickMesh();
    for (const auto& triangle : mesh.triangles) {
        const auto t =
            hitTriangle(ray, mesh.vertices[triangle.vertices[0]].position, mesh.vertices[triangle.vertices[1]].position,
                        mesh.vertices[triangle.vertices[2]].position);
        if (t && *t < nearest) {
            if (const auto id = level.entityOfPrimitive(triangle.primitive)) {
                nearest = *t;
                best = id;
            }
        }
    }
    // The points, by their boxes; a point standing on a primitive wins over the floor under it.
    for (const Entity& entity : level.entities()) {
        if (entity.kind == EntityKind::Primitive) {
            continue;
        }
        const auto t = hitBounds(ray, level.bounds(entity));
        if (t && *t <= nearest) {
            nearest = *t;
            best = entity.id;
        }
    }
    return best;
}

FlyCamera::FlyCamera(anim::Vec3 position, float yawDegrees, float pitchDegrees)
    : m_position(position), m_yaw(yawDegrees * kDegrees),
      m_pitch(std::clamp(pitchDegrees * kDegrees, -kMaxPitch, kMaxPitch)) {}

FlyCamera FlyCamera::at(const sandbox::Viewpoint& view) {
    return FlyCamera(view.position, view.yawDegrees, view.pitchDegrees);
}

float FlyCamera::lookRadiansPerPixel(float displayScale, float sensitivity) {
    // A display scale of nothing (a failed query) counts as 1, so the look never divides by zero.
    const float scale = displayScale > 0.0F ? displayScale : 1.0F;
    return kLookPerScaleOnePixel * sensitivity / scale;
}

float FlyCamera::accelerationAfter(float movingSeconds) {
    return std::min(std::pow(kAccelerationPerSecond, std::max(movingSeconds, 0.0F)), kMaxAcceleration);
}

void FlyCamera::step(const FlyInput& input, float seconds) {
    if (!input.flying) {
        m_movingSeconds = 0.0F; // letting go of the right button starts the acceleration over
        // A glide eases in: the fraction covered is the square of the fraction of its time, so each step covers more
        // than the one before, and the last lands exactly on the target.
        if (gliding()) {
            // A step that ends within a thousandth of a second of the end ends it, so float sums of steps never add
            // one.
            m_glideSeconds += seconds;
            if (m_glideSeconds > kGlideSeconds - 0.001F) {
                m_glideSeconds = kGlideSeconds;
            }
            const float t = m_glideSeconds / kGlideSeconds;
            m_position = t >= 1.0F ? m_glideTo : anim::lerp(m_glideFrom, m_glideTo, t * t);
        }
        return;
    }
    m_glideSeconds = kGlideSeconds; // the user took the camera back
    // 1. The wheel changes the base speed, which stays.
    if (input.wheel != 0.0F) {
        m_speed = std::clamp(m_speed * std::pow(kWheelFactor, input.wheel), kMinSpeed, kMaxSpeed);
    }
    // 2. The mouse turns: right turns the view clockwise from above (the heading falls), down looks down.
    m_yaw = std::remainder(m_yaw - (input.lookX * input.lookRadiansPerPixel), 2.0F * std::numbers::pi_v<float>);
    m_pitch = std::clamp(m_pitch - (input.lookY * input.lookRadiansPerPixel), -kMaxPitch, kMaxPitch);
    // 3. The keys fly along the view and sideways, and straight up or down; Shift and Alt scale the speed.
    const ViewPose now = pose();
    anim::Vec3 move = anim::add(anim::scale(now.forward, input.forward), anim::scale(now.right, input.strafe));
    move = anim::add(move, anim::Vec3{0.0F, 0.0F, input.rise});
    const float length = anim::length(move);
    if (length > 1.0F) {
        move = anim::scale(move, 1.0F / length);
    }
    // The acceleration's clock runs while a movement key is held and starts over when none is; Shift and Alt multiply
    // the speed the acceleration has reached and do not restart it.
    const bool moving = input.forward != 0.0F || input.strafe != 0.0F || input.rise != 0.0F;
    float speed = m_speed * acceleration();
    m_movingSeconds = moving ? m_movingSeconds + seconds : 0.0F;
    if (input.fast) {
        speed *= kFastFactor;
    }
    if (input.slow) {
        speed *= kSlowFactor;
    }
    m_position = anim::add(m_position, anim::scale(move, speed * seconds));
}

void FlyCamera::frame(const Bounds& box, float halfHeight) {
    constexpr float kMinDistance = 1.0F;
    const anim::Vec3 middle = anim::scale(anim::add(box.min, box.max), 0.5F);
    const float radius = 0.5F * anim::length(anim::subtract(box.max, box.min));
    // The sphere fits when its radius over the distance is the sine of the half angle the view window spans.
    const float halfAngle = std::atan(halfHeight);
    const float distance = std::max(kMinDistance, radius / std::sin(halfAngle));
    m_glideFrom = m_position;
    m_glideTo = anim::subtract(middle, anim::scale(pose().forward, distance));
    m_glideSeconds = 0.0F;
}

ViewPose FlyCamera::pose() const {
    // The game's heading looks along (-sin yaw, cos yaw); the pitch tilts it up.
    const float sy = std::sin(m_yaw);
    const float cy = std::cos(m_yaw);
    const float sp = std::sin(m_pitch);
    const float cp = std::cos(m_pitch);
    ViewPose pose;
    pose.position = m_position;
    pose.forward = anim::Vec3{-sy * cp, cy * cp, sp};
    pose.right = anim::Vec3{cy, sy, 0.0F};
    pose.up = anim::cross(pose.right, pose.forward);
    return pose;
}

float FlyCamera::yawDegrees() const { return m_yaw / kDegrees; }

float FlyCamera::pitchDegrees() const { return m_pitch / kDegrees; }

LevelView levelView(const ViewPose& pose, float width, float height, float halfHeight, float nearClip) {
    const float aspect = height > 0.0F ? width / height : 1.0F;
    return LevelView{.pose = pose,
                     .halfWidth = halfHeight * aspect,
                     .halfHeight = halfHeight,
                     .nearClip = nearClip,
                     .width = width,
                     .height = height};
}

} // namespace coney::studio
