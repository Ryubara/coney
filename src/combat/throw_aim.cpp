// SPDX-License-Identifier: GPL-3.0-or-later
// The aiming state of a throw (docs/research/objects.md#throws).
#include "combat/throw_aim.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "combat/throw_velocity.h"

namespace coney::combat {

namespace {

// The frames a second the arc is stepped at.
constexpr float kTraceRate = 60.0F;

// The heading (radians, 0 facing +y) of the horizontal direction from `from` to `to`; `fallback` when they stand on
// one spot.
float headingTo(anim::Vec3 from, anim::Vec3 to, float fallback) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    if (dx == 0.0F && dy == 0.0F) {
        return fallback;
    }
    return std::atan2(-dx, dy);
}

// A stick offset's step outside the dead zone: the offset less kAimStickLess, signed; 0 inside it.
int stickStep(int offset) {
    if (std::abs(offset) <= kAimDeadZone) {
        return 0;
    }
    return offset > 0 ? offset - kAimStickLess : offset + kAimStickLess;
}

} // namespace

anim::Vec3 throwerToWorld(anim::Vec3 local, float heading) {
    // The heading turns the frame about z: local +y is facing(heading) = (−sin h, cos h).
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    return anim::Vec3{(local.x * c) - (local.y * s), (local.x * s) + (local.y * c), local.z};
}

void ThrowAimState::enter(anim::Vec3 origin, float heading) {
    m_active = true;
    m_firstFrame = true;
    m_origin = origin;
    m_heading = heading;
    m_pitch = kAimStartPitch;
    m_target = 0;
    m_faceFrames = 0;
    m_arcPoints = 0;
}

void ThrowAimState::leave() {
    m_active = false;
    m_pitch = kAimStartPitch;
    m_target = 0;
    m_faceFrames = 0;
    m_arcPoints = 0;
}

void ThrowAimState::trace(const AimInput& input, const AimWorld& world, int side) {
    const anim::Vec3 offset{kAimReleaseOffset[0], kAimReleaseOffset[1], kAimReleaseOffset[2]};
    m_release = anim::add(m_origin, throwerToWorld(offset, m_heading));
    m_velocity = throwerToWorld(aimedThrowVelocity(input.weightFactor, m_pitch), m_heading);
    // The arc: 108 steps of 1/60 s from the release point, gravity on the z speed, every sixth point kept.
    anim::Vec3 at = m_release;
    anim::Vec3 speed = m_velocity;
    m_arc[0] = at;
    for (int step = 1; step < kAimTraceSteps; ++step) {
        at = anim::add(at, anim::scale(speed, 1.0F / kTraceRate));
        speed.z -= kAimTraceGravityStep;
        if (step % kAimTraceKeep == 0) {
            m_arc.at(static_cast<std::size_t>(step / kAimTraceKeep)) = at;
        }
    }
    constexpr int kSegments = kAimTracePoints - 1;
    // Pass 1: the first segment meeting a human the throw may target.
    int candidateSegment = -1;
    AimHit candidate;
    if (world.humans) {
        for (int i = 0; i < kSegments; ++i) {
            const auto a = static_cast<std::size_t>(i);
            if (const std::optional<AimHit> hit = world.humans(m_arc[a], m_arc[a + 1], input.radius, side)) {
                candidateSegment = i;
                candidate = *hit;
                break;
            }
        }
    }
    // Pass 2: anything in the way up to the candidate's segment; a contact ends the arc unless it is in that segment
    // and no nearer than the candidate.
    const int lastSegment = candidateSegment >= 0 ? candidateSegment : kSegments - 1;
    if (world.anything) {
        for (int i = 0; i <= lastSegment; ++i) {
            const auto a = static_cast<std::size_t>(i);
            const std::optional<AimHit> hit = world.anything(m_arc[a], m_arc[a + 1], input.radius);
            if (!hit || (i == candidateSegment && hit->fraction >= candidate.fraction)) {
                continue;
            }
            m_arc[a + 1] = anim::add(m_arc[a], anim::scale(anim::subtract(m_arc[a + 1], m_arc[a]), hit->fraction));
            m_arcPoints = i + 2;
            m_target = 0;
            m_aimed = hit->handle;
            return;
        }
    }
    if (candidateSegment >= 0) {
        const auto a = static_cast<std::size_t>(candidateSegment);
        m_arc[a + 1] = anim::add(m_arc[a], anim::scale(anim::subtract(m_arc[a + 1], m_arc[a]), candidate.fraction));
        m_arcPoints = candidateSegment + 2;
        m_target = candidate.handle;
        m_aimed = candidate.handle;
        return;
    }
    m_arcPoints = kAimTracePoints;
    m_target = 0;
    m_aimed = 0;
}

bool ThrowAimState::step(const AimInput& input, const AimWorld& world) {
    if (!m_active) {
        return false;
    }
    // The first frame alone: under the follow camera he faces the way it looks.
    if (m_firstFrame) {
        m_firstFrame = false;
        if (input.followHeading) {
            m_heading = *input.followHeading;
        }
    }
    const int turn = stickStep(input.stickX);
    const int lift = stickStep(input.stickY);
    const int side = turn == 0 ? 0 : (turn > 0 ? 1 : -1);
    const double before = m_target;
    trace(input, world, side);
    if (m_target != 0 && m_target != before) {
        m_faceFrames = kAimFaceFrames;
    }
    // A human target: straight at him on the frames after he is found, and while the stick rests.
    if (m_target != 0 && (m_faceFrames > 0 || (turn == 0 && lift == 0))) {
        if (m_faceFrames > 0) {
            --m_faceFrames;
        }
        if (const std::optional<anim::Vec3> point = world.targetPoint ? world.targetPoint(m_target) : std::nullopt) {
            m_heading = headingTo(m_release, *point, m_heading);
        }
        return false;
    }
    // Otherwise the stick: x turns him (right turns clockwise), y pitches the aim (up raises it).
    m_heading -= static_cast<float>(turn) * kAimRate;
    m_pitch = std::clamp(m_pitch - (static_cast<float>(lift) * kAimRate), -kAimPitchLimit, kAimPitchLimit);
    return turn != 0;
}

} // namespace coney::combat
