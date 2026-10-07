// SPDX-License-Identifier: GPL-3.0-or-later
#include "camera/path_camera.h"

#include <algorithm>
#include <utility>

#include "camera/locked_camera.h"

namespace coney::camera {

namespace {

// How far ahead of the camera the look-at point is put, metres.
constexpr float kLookAhead = 3.0F;

// The Catmull-Rom point `t` (0-1) of the way from `p1` to `p2`, `p0` and `p3` their neighbours.
anim::Vec3 catmullRom(anim::Vec3 p0, anim::Vec3 p1, anim::Vec3 p2, anim::Vec3 p3, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    const auto axis = [t, t2, t3](float a, float b, float c, float d) {
        return 0.5F * ((2.0F * b) + (-a + c) * t + (2.0F * a - 5.0F * b + 4.0F * c - d) * t2 +
                       (-a + 3.0F * b - 3.0F * c + d) * t3);
    };
    return anim::Vec3{axis(p0.x, p1.x, p2.x, p3.x), axis(p0.y, p1.y, p2.y, p3.y), axis(p0.z, p1.z, p2.z, p3.z)};
}

} // namespace

anim::Quat orientationOf(float headingDegrees, float pitchDegrees, float rollDegrees) {
    return scriptedOrientation(headingDegrees, pitchDegrees, rollDegrees);
}

void PathCamera::setup(const CameraView& start, float seconds, std::string onEnd, float fieldOfView, float farClip) {
    m_points.clear();
    m_points.push_back(PathPoint{.position = start.position,
                                 .orientation = start.orientation,
                                 .seconds = std::max(seconds, 0.0F),
                                 .onReach = {}});
    m_onEnd = std::move(onEnd);
    m_fieldOfView = fieldOfView > 0.0F ? fieldOfView : start.fieldOfView;
    m_nearClip = start.nearClip;
    m_farClip = std::min(farClip > 0.0F ? farClip : start.farClip, LockedCamera::kMaxFarClip);
    m_segment = 0;
    m_elapsed = 0.0F;
    m_active = false;
    m_finished = false;
}

void PathCamera::addPoint(const PathPoint& point) {
    PathPoint kept = point;
    kept.seconds = std::max(kept.seconds, 0.0F);
    if (m_points.size() < kMaxPoints) {
        m_points.push_back(std::move(kept));
    } else {
        m_points.back() = std::move(kept);
    }
}

void PathCamera::activate() {
    if (m_active) {
        return;
    }
    m_segment = 0;
    m_elapsed = 0.0F;
    m_active = !m_points.empty();
    m_finished = false;
}

void PathCamera::reverse(std::string onEnd) {
    // Segment k of the reversed path is segment n - 2 - k of the old one, so its time moves with it; the new last
    // point's time is unused.
    const std::size_t n = m_points.size();
    std::vector<float> times(n, 0.0F);
    for (std::size_t k = 0; k + 1 < n; ++k) {
        times[k] = m_points[n - 2 - k].seconds;
    }
    std::ranges::reverse(m_points);
    for (std::size_t k = 0; k < n; ++k) {
        m_points[k].seconds = times[k];
    }
    m_onEnd = std::move(onEnd);
    m_segment = 0;
    m_elapsed = 0.0F;
    m_active = !m_points.empty();
    m_finished = false;
}

void PathCamera::update(float seconds, std::vector<std::string>& fired) {
    if (!m_active || m_finished) {
        return;
    }
    m_elapsed += seconds;
    // Each segment whose time is up: its end point is reached.
    while (m_segment + 1 < m_points.size() && m_elapsed >= m_points[m_segment].seconds) {
        m_elapsed -= m_points[m_segment].seconds;
        ++m_segment;
        if (!m_points[m_segment].onReach.empty()) {
            fired.push_back(m_points[m_segment].onReach);
        }
    }
    if (m_segment + 1 >= m_points.size()) {
        m_finished = true;
        m_elapsed = 0.0F;
        if (!m_onEnd.empty()) {
            fired.push_back(m_onEnd);
        }
    }
}

CameraView PathCamera::view() const {
    if (m_points.empty()) {
        return CameraView{};
    }
    const std::size_t last = m_points.size() - 1;
    const std::size_t i = std::min(m_segment, last);
    const std::size_t next = std::min(i + 1, last);
    const float span = m_points[i].seconds;
    const float t = next == i || span <= 0.0F ? 0.0F : std::clamp(m_elapsed / span, 0.0F, 1.0F);
    const anim::Vec3 position = catmullRom(m_points[i > 0 ? i - 1 : 0].position, m_points[i].position,
                                           m_points[next].position, m_points[std::min(next + 1, last)].position, t);
    const anim::Quat orientation = anim::slerp(m_points[i].orientation, m_points[next].orientation, t);
    const anim::Vec3 forward =
        anim::transformDirection(anim::matrixFromQuat(orientation), anim::Vec3{0.0F, 1.0F, 0.0F});
    return CameraView{.position = position,
                      .orientation = orientation,
                      .lookAt = anim::add(position, anim::scale(forward, kLookAhead)),
                      .fieldOfView = m_fieldOfView,
                      .nearClip = m_nearClip,
                      .farClip = m_farClip};
}

} // namespace coney::camera
