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
// Where the camera starts before it has a rail: this far behind (-y) and above the look-at point (**Coney's choice**).
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
// Mode 3 (docs/research/camera.md#rail-mode3): the look-at point stands this far from the camera in plan; it moves at
// most this share of the gap to its goal in one update, at the target's forward speed plus the base speed (`+0x1a4`);
// the segment hand-over grows by kTrailBlendGrowth an update and ends at 1 or once the step is under kTrailBlendDone in
// plan; a camera this near (squared) to the rail's first or last point holds there.
constexpr float kTrailReach = 2.0F;
constexpr float kTrailGapShare = 0.4F;
constexpr float kTrailBaseSpeed = 1.0F;
constexpr float kTrailBlendGrowth = 1.15F;
constexpr float kTrailBlendDone = 0.3F;
constexpr float kTrailEndSquared = 1e-10F;

// The length of a vector across the ground. @coney-only a maths helper
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

float RailCamera::mode3Pitch() const { return m_mode3Pitch.current / kRadians; }

void RailCamera::activate() {
    // Every eased setting the activation names jumps to its target with no time left; the look-at height and mode
    // 3's margin keep easing.
    for (EasedValue* value :
         {&m_lead, &m_distance, &m_height, &m_fieldOfView, &m_shift, &m_lookShift, &m_pitch, &m_mode3Pitch}) {
        value->set(value->target, 0.0F);
    }
    m_blend = 0.0F;
    m_starting = true;
    m_undamped = true;
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
    case kMode3Pitch:
        // At or above -360 degrees mode 3 with that pitch, in radians; below it off, and mode 0 again.
        if (value < kAngleOff) {
            m_mode3Pitch.set(-std::numeric_limits<float>::max(), 0.0F);
            if (m_mode == RailMode::Trailing) {
                m_mode = RailMode::Level;
            }
            break;
        }
        m_mode = RailMode::Trailing;
        // Switched on from off it eases from the camera's present pitch (CamRail_Update's CamRail_PitchToPoint).
        if (m_mode3Pitch.current < kAngleOff * kRadians) {
            m_mode3Pitch.set(presentPitch() * kRadians, 0.0F);
        }
        m_mode3Pitch.set(value * kRadians, seconds);
        break;
    case kMode3Margin:
        m_mode3Margin.set(value, seconds);
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

void RailCamera::makePoints(anim::Vec3 feet) { // stand-in: one target, so the mean is his feet
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
    // The rise tests need a segment with a length in plan (Coney's choice: any; the original's, 1e-5 squared).
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

void RailCamera::chooseSegmentFor(anim::Vec3 p) {
    if (m_count < 2) {
        return;
    }
    // **Coney's stand-in** (CamRail_ChooseSegment): the original also takes the segment between two rail points
    // equally near when the point projects inside it, keeps the last point's index in modes 1 and 2, measures the
    // inside test in plan in mode 3, and starts the hand-over even before the first placement (#956).
    // The rail point nearest the target point starts the segment; the last point gives the last segment.
    std::size_t nearest = 0;
    float best = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < m_count; ++i) {
        if (const float away = anim::distance(m_points.at(i), p); away < best) {
            best = away;
            nearest = i;
        }
    }
    std::size_t chosen = std::min(nearest, m_count - 2);
    // Outside it, the previous or the next segment the target point projects inside.
    const auto inside = [this, p](std::size_t i) {
        const auto [t, length] = project(i, p);
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
    // The height: a ceiling over the target's feet. Stand-in: 0 is on here, off in the original (#1017).
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
    // end, the camera keeps last update's view. **Coney's choice**: what clears the hold is not traced, so it holds
    // only while that is so.
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
    // The target's place on the rail: its nearest point of the segments in plan. Stand-in (#1017): no segment steps.
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
    // The lead ahead or behind, clamped to the rail's ends. Stand-in (#1017): walked along the rail's segments.
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

void RailCamera::update(std::optional<anim::Vec3> feet, bool leadLook, float seconds, anim::Vec3 velocity) {
    if (seconds <= 0.0F && m_placed) {
        return;
    }
    placeFor(feet, leadLook, seconds, velocity);
    // An update with time ends the activation's start and its undamped step, in any mode.
    if (seconds > 0.0F) {
        m_starting = false;
        m_undamped = false;
    }
}

void RailCamera::placeFor(std::optional<anim::Vec3> feet, bool leadLook, float seconds, anim::Vec3 velocity) {
    // The settings eased toward their targets; a height on its way off switches off once there.
    for (EasedValue* value : {&m_lead, &m_distance, &m_height, &m_fieldOfView, &m_shift, &m_lookShift, &m_pitch,
                              &m_lookHeight, &m_mode3Pitch, &m_mode3Margin}) {
        value->step(seconds);
    }
    if (m_heightOffPending && m_height.secondsLeft <= 0.0F) {
        m_height.set(-1.0F, 0.0F);
        m_heightOffPending = false;
    }
    if (m_mode == RailMode::Trailing && m_count >= 2) {
        placeTrailing(feet, velocity, seconds);
        return;
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
    if (m_placed && !m_undamped) {
        damp(oldTarget, oldLook);
    }
    if (m_mode == RailMode::Level) {
        placeLevel(*feet);
    } else {
        placeLeading(*feet, leadLook);
    }
    m_placed = true;
}

anim::Vec3 RailCamera::pitchedLook(anim::Vec3 from, anim::Vec3 toward) const {
    anim::Vec3 out = from;
    const anim::Vec3 flat{toward.x - from.x, toward.y - from.y, 0.0F};
    if (const float plan = flatLength(flat); plan > 0.0F) {
        out.x += flat.x / plan * kTrailReach;
        out.y += flat.y / plan * kTrailReach;
    }
    out.z += kTrailReach * std::tan(m_mode3Pitch.current);
    return out;
}

anim::Vec3 RailCamera::projectTrailing(anim::Vec3 p) const {
    // Along the segment's unit vector by the plan distance over its plan length, so the height follows the rail.
    const anim::Vec3 a = m_points.at(m_segment);
    const anim::Vec3 b = m_points.at(m_segment + 1);
    const anim::Vec3 ab = anim::subtract(b, a);
    const float length = anim::length(ab);
    if (length <= 0.0F) {
        return a;
    }
    const anim::Vec3 u = anim::scale(ab, 1.0F / length);
    const float flatSquared = (u.x * u.x) + (u.y * u.y);
    if (flatSquared <= 0.0F) {
        return a;
    }
    const float s = (((p.x - a.x) * u.x) + ((p.y - a.y) * u.y)) / flatSquared;
    // Clamped only before the rail's first point and past its last.
    if (m_segment == 0 && s < 0.0F) {
        return a;
    }
    if (m_segment + 2 == m_count && s > length) {
        return b;
    }
    return anim::add(a, anim::scale(u, s));
}

void RailCamera::placeTrailing(std::optional<anim::Vec3> feet, anim::Vec3 velocity, float seconds) {
    const anim::Vec3 offset{m_setup.offset.x, m_setup.offset.y, m_lookHeight.current};
    // 1. The start, on the first update after an activation (or the first ever): level with the target point.
    if (m_starting || !m_placed) {
        if (!feet) {
            return;
        }
        m_feet = *feet;
        m_target = anim::add(*feet, offset);
        chooseSegmentFor(m_target);
        m_position = projectTrailing(m_target);
        m_lookAt = pitchedLook(m_position, m_target);
        m_goal = m_lookAt;
        m_placed = true;
        // **Coney's stand-in**: the original also sets `+0x3ea` from the side the first target faces, which nothing
        // reads (docs/research/camera.md#rail-mode3).
        return;
    }
    // No target: the camera keeps its place.
    if (!feet) {
        return;
    }
    m_feet = *feet;
    // 2. **Coney stand-in**: the keep-in-frame step (0x00140d68), which pushes the targets back inside the view's
    // sides by the margin (setting 7) and out of walls, is not built; Coney's rail has one target, so the hindmost is
    // he.
    // 3. The target point: his feet raised by the offset's height only.
    m_target = anim::Vec3{feet->x, feet->y, feet->z + offset.z};
    // 4. The goal: the target point's foot on the rail and 2 m toward him at the pitch, taken only when ahead of the
    // look-at point along the segment: the camera never goes back.
    // The finish of both paths below, moving or not: with the height setting above 0 the camera is no higher than
    // that over the target's feet.
    const float feetZ = feet->z;
    const auto applyCeiling = [this, feetZ] {
        if (m_height.current > 0.0F) {
            m_position.z = std::min(m_position.z, feetZ + m_height.current);
        }
    };
    const bool heldBefore = m_held;
    const anim::Vec3 start = m_position;
    chooseSegmentFor(m_target);
    const anim::Vec3 goal = pitchedLook(projectTrailing(m_target), m_target);
    const anim::Vec3 d = flatDirection();
    const bool ahead = ((goal.x - m_lookAt.x) * d.x) + ((goal.y - m_lookAt.y) * d.y) > 0.0F;
    bool move = false;
    if (ahead) {
        m_goal = goal;
        move = !heldBefore;
    }
    // 5. The ends: a camera at the rail's first point (on segment 0) or its last (on the last segment) holds there,
    // until CamLockRail(false).
    const anim::Vec3 fromFirst = anim::subtract(start, m_points.at(0));
    const anim::Vec3 fromLast = anim::subtract(start, m_points.at(m_count - 1));
    if ((m_segment == 0 && anim::dot(fromFirst, fromFirst) < kTrailEndSquared) ||
        (m_segment + 2 >= m_count && anim::dot(fromLast, fromLast) < kTrailEndSquared)) {
        m_held = true;
    }
    if (!move || m_held) {
        m_goal = m_lookAt;
        applyCeiling();
        return;
    }
    // 6. The move: toward the goal by the target's forward speed plus 1 m/s, at most 40% of the gap; the segment
    // chosen again for the moved point (so it crosses joints) and the camera its foot on the rail.
    const float forward = (velocity.x * d.x) + (velocity.y * d.y);
    const float speed = (forward > 0.0F ? forward + kTrailBaseSpeed : kTrailBaseSpeed) * seconds;
    const anim::Vec3 gap = anim::subtract(m_goal, m_lookAt);
    const float gapLength = anim::length(gap);
    const float step = std::min(gapLength * kTrailGapShare, speed);
    if (gapLength > 0.0F) {
        m_lookAt = anim::add(m_lookAt, anim::scale(gap, step / gapLength));
    }
    chooseSegmentFor(m_lookAt);
    anim::Vec3 placed = projectTrailing(m_lookAt);
    // 7. The hand-over between segments: a share of the step that grows by 15% an update.
    if (m_blend > 0.0F) {
        const anim::Vec3 change = anim::subtract(placed, start);
        const float grown = m_blend * kTrailBlendGrowth;
        if (((change.x * change.x) + (change.y * change.y)) < kTrailBlendDone * kTrailBlendDone || grown >= 1.0F) {
            m_blend = 0.0F;
        } else {
            placed = anim::add(start, anim::scale(change, m_blend));
            m_blend = grown;
        }
    }
    m_position = placed;
    // 8. The look-at point again 2 m from the camera toward where it was, at the pitch.
    m_lookAt = pitchedLook(m_position, m_lookAt);
    applyCeiling();
}

CameraView RailCamera::view() const {
    return viewLookingAt(m_position, m_lookAt, m_fieldOfView.current, m_setup.nearClip, m_setup.farClip);
}

} // namespace coney::camera
