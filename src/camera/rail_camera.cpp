// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/rail_camera.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <tuple>
#include <utility>

namespace coney::camera {

namespace {

constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
// Where the camera starts before it has a rail: this far behind (-y) and above the look-at point (**Coney choice**).
constexpr float kNoRailBack = 3.0F;
constexpr float kNoRailUp = 1.0F;
// The fixed pitch's look-at point lies this far from the camera in plan.
constexpr float kPitchReach = 6.0F;
// A pitch of exactly 0 is taken as this (0x0013fb30).
constexpr float kZeroPitch = -0.05F;
// The hand-over between segments starts at this share and grows by kBlendStep an update, ending at 1 or once the
// step is under kBlendDone metres.
constexpr float kBlendStart = 0.1F;
constexpr float kBlendStep = 0.05F;
constexpr float kBlendDone = 0.01F;

// The length of a vector across the ground.
float flatLength(anim::Vec3 v) { return std::hypot(v.x, v.y); }

} // namespace

void EasedValue::set(float value, float seconds) {
    target = value;
    secondsLeft = std::max(seconds, 0.0F);
    if (secondsLeft <= 0.0F) {
        current = value;
    }
}

void EasedValue::step(float seconds) {
    if (secondsLeft <= seconds || secondsLeft <= 0.0F) {
        current = target;
        secondsLeft = 0.0F;
        return;
    }
    current += (target - current) * (seconds / secondsLeft);
    secondsLeft -= seconds;
}

void RailCamera::setup(const RailSetup& setup) {
    m_setup = setup;
    m_setup.farClip = std::min(setup.farClip, kMaxFarClip);
    m_count = 0;
    m_segment = 0;
    m_mode = RailMode::Level;
    m_distance.set(0.0F, 0.0F);
    m_shift.set(0.0F, 0.0F);
    m_lookShift.set(0.0F, 0.0F);
    m_height.set(-1.0F, 0.0F);
    m_pitch.set(-std::numeric_limits<float>::max(), 0.0F);
    m_heightOffPending = false;
    m_fieldOfView.set(setup.fieldOfView, 0.0F);
    m_lookHeight.set(setup.offset.z, 0.0F);
    m_blend = 0.0F;
    m_held = false;
}

void RailCamera::addPoint(anim::Vec3 point) {
    m_points.at(std::min(m_count, kMaxPoints - 1)) = point;
    m_count = std::min(m_count + 1, kMaxPoints);
}

void RailCamera::setLead(float lead, float seconds, bool ahead) {
    m_mode = ahead ? RailMode::Ahead : RailMode::Behind;
    m_lead.set(lead, m_lead.current < 0.0F ? 0.0F : seconds);
}

float RailCamera::presentPitch() const {
    const anim::Vec3 toward = anim::subtract(m_lookAt, m_position);
    return std::atan2(toward.z, flatLength(toward)) / kRadians;
}

void RailCamera::modify(std::uint32_t param, float value, float seconds) {
    switch (param) {
    case kDistance:
        // Switched on, it eases from the present distance in plan to the target point.
        if (m_distance.current <= 0.0F && value > 0.0F) {
            m_distance.set(flatLength(anim::subtract(m_position, m_target)), 0.0F);
        }
        m_distance.set(std::max(value, 0.0F), seconds);
        break;
    case kHeight:
        // Switched on from off it starts from the present height; set negative it eases back there, then goes off.
        if (value < 0.0F && m_height.current < 0.0F) {
            break;
        }
        if (m_height.current < 0.0F) {
            m_height.set(m_position.z - m_feet.z, 0.0F);
        }
        m_heightOffPending = value < 0.0F;
        m_height.set(value < 0.0F ? m_position.z - m_feet.z : value, seconds);
        break;
    case kFieldOfView:
        if (value >= 0.0F) {
            m_fieldOfView.set(value, seconds);
        }
        break;
    case kShift:
        m_shift.set(value, seconds);
        break;
    case kLookShift:
        m_lookShift.set(value, seconds);
        break;
    case kPitch:
        if (value < kAngleOff) {
            m_pitch.set(-std::numeric_limits<float>::max(), 0.0F);
            break;
        }
        if (m_pitch.current < kAngleOff) {
            m_pitch.set(presentPitch(), 0.0F);
        }
        m_pitch.set(value == 0.0F ? kZeroPitch : value, seconds);
        break;
    case kLookHeight:
        m_lookHeight.set(value, seconds);
        break;
    default:
        break;
    }
}

anim::Vec3 RailCamera::flatDirection() const {
    if (m_count < 2) {
        return anim::Vec3{0.0F, 1.0F, 0.0F};
    }
    const anim::Vec3 along = anim::subtract(m_points.at(m_segment + 1), m_points.at(m_segment));
    const float flat = flatLength(along);
    return flat > 0.0F ? anim::Vec3{along.x / flat, along.y / flat, 0.0F} : anim::Vec3{0.0F, 1.0F, 0.0F};
}

std::pair<float, float> RailCamera::project(std::size_t i, anim::Vec3 p) const {
    const anim::Vec3 a = m_points.at(i);
    const anim::Vec3 ab = anim::subtract(m_points.at(i + 1), a);
    const float length = anim::length(ab);
    if (length <= 0.0F) {
        return {0.0F, 0.0F};
    }
    return {anim::dot(anim::subtract(p, a), ab) / length, length};
}

void RailCamera::makePoints(anim::Vec3 feet) {
    const anim::Vec3 offset{m_setup.offset.x, m_setup.offset.y, m_lookHeight.current};
    if (m_mode != RailMode::Level) {
        // The leading modes: the offset added, the look-at point the target point.
        m_target = anim::add(feet, offset);
        m_lookAt = m_target;
        return;
    }
    const anim::Vec3 d = flatDirection();
    m_target = feet;
    m_lookAt = feet;
    m_target.z += m_lookHeight.current;
    if (m_shift.current != 0.0F) {
        m_target = anim::add(m_target, anim::scale(d, m_shift.current));
        m_lookAt = anim::add(m_target, anim::scale(d, m_lookShift.current));
    } else if (m_lookShift.current == 0.0F) {
        m_lookAt = anim::add(m_lookAt, offset);
    } else {
        m_lookAt.z += m_lookHeight.current;
        m_lookAt = anim::add(m_lookAt, anim::scale(d, m_lookShift.current));
    }
}

void RailCamera::damp(anim::Vec3 oldTarget, anim::Vec3 oldLook) {
    // The rise tests need a segment with a length in plan.
    const bool riseTests =
        m_count >= 2 && flatLength(anim::subtract(m_points.at(m_segment + 1), m_points.at(m_segment))) > 0.0F;
    const auto dampOne = [riseTests](anim::Vec3 old, anim::Vec3 now) {
        const anim::Vec3 delta = anim::subtract(now, old);
        const float plan = flatLength(delta);
        const float rise = delta.z;
        if (riseTests && rise > 0.5F) {
            anim::Vec3 out = anim::add(old, anim::scale(delta, 0.5F));
            out.z = old.z + ((out.z - old.z) * 0.25F);
            return out;
        }
        if (plan > 1.0F) {
            return anim::add(old, anim::scale(delta, 0.25F));
        }
        if (riseTests && rise > 0.1F) {
            const float f = 1.0F - (1.875F * (rise - 0.1F));
            anim::Vec3 out = anim::add(old, anim::scale(delta, std::min(2.0F * f, 1.0F)));
            out.z = old.z + ((out.z - old.z) * f);
            return out;
        }
        if (plan > 0.4F) {
            return anim::add(old, anim::scale(delta, 1.0F - (1.25F * (plan - 0.4F))));
        }
        return now;
    };
    m_target = dampOne(oldTarget, m_target);
    m_lookAt = dampOne(oldLook, m_lookAt);
}

void RailCamera::chooseSegment() {
    if (m_count < 2) {
        return;
    }
    // The rail point nearest the target point starts the segment; the last point gives the last segment.
    std::size_t nearest = 0;
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < m_count; ++i) {
        if (const float away = anim::distance(m_points.at(i), m_target); away < best) {
            best = away;
            nearest = i;
        }
    }
    std::size_t chosen = std::min(nearest, m_count - 2);
    // Outside it, the previous or the next segment the target point projects inside.
    const auto inside = [this](std::size_t i) {
        const auto [t, length] = project(i, m_target);
        return t >= 0.0F && t <= length;
    };
    if (!inside(chosen)) {
        if (chosen > 0 && inside(chosen - 1)) {
            --chosen;
        } else if (chosen + 2 < m_count && inside(chosen + 1)) {
            ++chosen;
        }
    }
    if (chosen != m_segment && m_placed) {
        m_blend = kBlendStart;
    }
    m_segment = chosen;
}

void RailCamera::placeLevel(anim::Vec3 feet) {
    const anim::Vec3 before = m_position;
    const anim::Vec3 lookBefore = m_lookAt;
    if (!m_held) {
        chooseSegment();
    }
    // The foot of the target point on the segment, clamped to its ends; within the reach of setting 0 in plan.
    anim::Vec3 q = m_points.at(m_segment);
    float t = 0.0F;
    float length = 0.0F;
    if (m_count >= 2) {
        std::tie(t, length) = project(m_segment, m_target);
        const anim::Vec3 a = m_points.at(m_segment);
        const anim::Vec3 ab = anim::subtract(m_points.at(m_segment + 1), a);
        q = anim::add(a, anim::scale(ab, length > 0.0F ? std::clamp(t, 0.0F, length) / length : 0.0F));
    }
    if (m_distance.current > 0.0F) {
        const anim::Vec3 away{m_target.x - q.x, m_target.y - q.y, 0.0F};
        const float plan = flatLength(away);
        if (plan > m_distance.current) {
            const float move = plan - m_distance.current;
            q.x += away.x / plan * move;
            q.y += away.y / plan * move;
        }
    }
    // The hand-over between segments.
    if (m_blend > 0.0F && m_placed) {
        const anim::Vec3 step = anim::scale(anim::subtract(q, before), m_blend);
        q = anim::add(before, step);
        m_blend = std::min(m_blend + kBlendStep, 1.0F);
        if (m_blend >= 1.0F || anim::length(step) < kBlendDone) {
            m_blend = 0.0F;
        }
    }
    // The height: a ceiling over the target's feet.
    if (m_height.current >= 0.0F) {
        q.z = std::min(q.z, feet.z + m_height.current);
    }
    // The fixed pitch: a look-at point 6 m from the camera toward the target in plan.
    if (m_pitch.current >= kAngleOff) {
        const anim::Vec3 toward{m_target.x - q.x, m_target.y - q.y, 0.0F};
        const float plan = flatLength(toward);
        if (plan > 0.0F) {
            m_lookAt = anim::Vec3{q.x + (toward.x / plan * kPitchReach), q.y + (toward.y / plan * kPitchReach),
                                  q.z + (kPitchReach * std::tan(m_pitch.current * kRadians))};
        }
    }
    // The ends: at the first point with the look-at point behind the rail's start, or at the last with it beyond the
    // end, the camera keeps last update's view. **Coney choice**: what clears the hold is not traced, so it holds only
    // while that is so.
    m_held = false;
    if (m_count >= 2 && m_placed) {
        const bool atStart = m_segment == 0 && t <= 0.0F && project(0, m_lookAt).first < 0.0F;
        const auto [lookT, lastLength] = project(m_count - 2, m_lookAt);
        const bool atEnd = m_segment == m_count - 2 && t >= length && lookT > lastLength;
        if (atStart || atEnd) {
            m_held = true;
            m_position = before;
            m_lookAt = lookBefore;
            return;
        }
    }
    m_position = q;
}

void RailCamera::placeLeading(anim::Vec3 feet, bool leadLook) {
    // The target's place on the rail: its nearest point of the segments in plan.
    float bestAway = std::numeric_limits<float>::max();
    float along = 0.0F;
    float total = 0.0F;
    for (std::size_t i = 0; i + 1 < m_count; ++i) {
        const auto [t, length] = project(i, feet);
        const float clamped = std::clamp(t, 0.0F, length);
        const anim::Vec3 ab = anim::subtract(m_points.at(i + 1), m_points.at(i));
        const anim::Vec3 at = anim::add(m_points.at(i), anim::scale(ab, length > 0.0F ? clamped / length : 0.0F));
        if (const float away = flatLength(anim::subtract(at, feet)); away < bestAway) {
            bestAway = away;
            along = total + clamped;
        }
        total += length;
    }
    // The lead ahead or behind, clamped to the rail's ends.
    const float sense = m_mode == RailMode::Ahead ? 1.0F : -1.0F;
    float left = std::clamp(along + (sense * m_lead.current), 0.0F, total);
    anim::Vec3 direction{0.0F, 1.0F, 0.0F};
    m_position = m_points.at(0);
    for (std::size_t i = 0; i + 1 < m_count; ++i) {
        const anim::Vec3 ab = anim::subtract(m_points.at(i + 1), m_points.at(i));
        const float length = anim::length(ab);
        if (length > 0.0F) {
            direction = anim::scale(ab, 1.0F / length);
        }
        if (left <= length || i + 2 == m_count) {
            m_position = anim::add(m_points.at(i), anim::scale(direction, std::min(left, length)));
            break;
        }
        left -= length;
    }
    // Raised to the target's height plus the setting while it is above 0.
    if (m_height.current > 0.0F) {
        m_position.z = feet.z + m_height.current;
    }
    if (leadLook) {
        m_lookAt = anim::add(m_lookAt, anim::scale(direction, sense * m_lead.current));
    }
}

void RailCamera::update(std::optional<anim::Vec3> feet, bool leadLook, float seconds) {
    if (seconds <= 0.0F && m_placed) {
        return;
    }
    // The settings eased toward their targets; a height on its way off switches off once there.
    for (EasedValue* value :
         {&m_lead, &m_distance, &m_height, &m_fieldOfView, &m_shift, &m_lookShift, &m_pitch, &m_lookHeight}) {
        value->step(seconds);
    }
    if (m_heightOffPending && m_height.secondsLeft <= 0.0F) {
        m_height.set(-1.0F, 0.0F);
        m_heightOffPending = false;
    }
    if (!feet) {
        return;
    }
    m_feet = *feet;
    const anim::Vec3 oldTarget = m_target;
    const anim::Vec3 oldLook = m_lookAt;
    makePoints(*feet);
    if (m_count == 0) {
        if (!m_placed) {
            m_position = anim::Vec3{m_lookAt.x, m_lookAt.y - kNoRailBack, m_lookAt.z + kNoRailUp};
        }
        m_placed = true;
        return;
    }
    if (m_placed) {
        damp(oldTarget, oldLook);
    }
    if (m_mode == RailMode::Level) {
        placeLevel(*feet);
    } else {
        placeLeading(*feet, leadLook);
    }
    m_placed = true;
}

CameraView RailCamera::view() const {
    return viewLookingAt(m_position, m_lookAt, m_fieldOfView.current, m_setup.nearClip, m_setup.farClip);
}

} // namespace coney::camera
