// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/trigger_spheres.h"

#include <algorithm>
#include <utility>

namespace coney::world_objects {

namespace {

// `p` raised by `height` metres: the ends of Coney's clear-line test.
// @orig 0x00414548 TriggerSphere_Accepts (unknown)
std::array<float, 3> raised(const std::array<float, 3>& p, float height) { return {p[0], p[1], p[2] + height}; }

} // namespace

bool TriggerSpheres::configure(double object, bool armed, float radius, int mode, std::uint32_t intervalMs) {
    TriggerSphere* sphere = find(object);
    if (sphere == nullptr) {
        if (m_spheres.size() >= kCapacity) {
            return false;
        }
        sphere = &m_spheres.emplace_back();
        sphere->object = object;
    }
    sphere->armed = armed;
    sphere->radius = radius;
    sphere->mode = mode;
    sphere->handlerPeriodMs = intervalMs;
    return true;
}

bool TriggerSpheres::arm(double object, bool armed) {
    TriggerSphere* sphere = find(object);
    if (!armed) {
        if (sphere != nullptr) {
            sphere->armed = false;
            sphere->occupants.clear();
        }
        return true;
    }
    if (sphere == nullptr) {
        sphere = make(object);
        if (sphere == nullptr) {
            return false;
        }
    }
    sphere->armed = true;
    return true;
}

TriggerSphere* TriggerSpheres::make(double object) {
    if (m_spheres.size() >= kCapacity) {
        return nullptr;
    }
    // The defaults of 0x00414480.
    TriggerSphere& sphere = m_spheres.emplace_back();
    sphere.object = object;
    sphere.radius = 0.0F;
    sphere.mode = 1;
    sphere.stayPeriodMs = kDefaultStayPeriodMs;
    sphere.nextStayMs = 0;
    return &sphere;
}

bool TriggerSpheres::setRadius(double object, float radius) {
    TriggerSphere* sphere = find(object);
    if (sphere == nullptr) {
        sphere = make(object);
    }
    if (sphere == nullptr) {
        return false;
    }
    sphere->radius = radius;
    return true;
}

// @coney-only lookup over Coney's own list
TriggerSphere* TriggerSpheres::find(double object) {
    const auto found = std::ranges::find(m_spheres, object, &TriggerSphere::object);
    return found == m_spheres.end() ? nullptr : &*found;
}

// @coney-only lookup over Coney's own list
const TriggerSphere* TriggerSpheres::find(double object) const {
    const auto found = std::ranges::find(m_spheres, object, &TriggerSphere::object);
    return found == m_spheres.end() ? nullptr : &*found;
}

bool TriggerSpheres::accepts(const TriggerSphere& sphere, const std::array<float, 3>& centre,
                             const std::array<float, 3>& point) const {
    if (!withinRadius(sphere, centre, point)) {
        return false;
    }
    // Stand-in: modes 1 and 2 use Coney's clear-line test between points raised 1 m (the original's tests are open).
    if (sphere.mode == 0 || !m_clearLine) {
        return true;
    }
    const std::array<float, 3> from = sphere.mode == 2 ? raised(centre, kLineRaise) : centre;
    return m_clearLine(from, raised(point, kLineRaise));
}

void TriggerSpheres::update(std::span<const BoxSubject> subjects, std::uint64_t nowMs, const VolumeBoxes::Send& send) {
    const std::uint32_t turn = m_frame % kFramesPerCheck;
    ++m_frame;
    // Coney's guard: with no way to find objects no sphere can be checked.
    if (!m_locate) {
        return;
    }
    // By index, not by reference: a handler may configure a new sphere, which can move the pool.
    for (std::size_t i = turn; i < m_spheres.size(); i += kFramesPerCheck) {
        check(i, subjects, nowMs, send);
    }
}

bool TriggerSpheres::stillChecked(std::size_t index, double object) const {
    // Stand-in: Coney never frees one sphere, but a handler may clear the pool; a sphere gone counts as freed.
    return index < m_spheres.size() && m_spheres[index].object == object;
}

void TriggerSpheres::check(std::size_t index, std::span<const BoxSubject> subjects, std::uint64_t nowMs,
                           const VolumeBoxes::Send& send) {
    if (!m_spheres[index].armed) {
        return;
    }
    const double object = m_spheres[index].object;
    // The stay timer is re-armed whenever it falls due, whether or not anyone is inside, with a strict test.
    const bool stayDue = m_spheres[index].nextStayMs < nowMs;
    if (stayDue) {
        m_spheres[index].nextStayMs = nowMs + m_spheres[index].stayPeriodMs;
    }
    // Stand-in: the original's object always has a position; Coney skips a sphere whose object is not found.
    const std::optional<std::array<float, 3>> centre = m_locate(object);
    if (!centre) {
        return;
    }
    // The found humans: those within the radius, not the object's own human, at most 60, in Coney's subjects order.
    // Stand-in: a dead human is never found (what the original's search mask 1 leaves out is not traced).
    std::size_t found = 0;
    for (const BoxSubject& subject : subjects) {
        if (found >= VolumeBoxes::kMaxOccupants) {
            break;
        }
        if (!subject.alive || subject.handle == object || !withinRadius(m_spheres[index], *centre, subject.position)) {
            continue;
        }
        ++found;
        TriggerSphere& sphere = m_spheres[index];
        const auto listed = std::ranges::find(sphere.occupants, subject.handle);
        int message = 0;
        if (accepts(sphere, *centre, subject.position)) {
            if (listed == sphere.occupants.end()) {
                // Message 3 goes even when every place is taken.
                addOccupant(sphere, subject.handle);
                message = VolumeBoxes::kEntered;
            } else if (stayDue) {
                message = VolumeBoxes::kInside;
            } else {
                continue;
            }
        } else if (listed != sphere.occupants.end()) {
            sphere.occupants.erase(listed);
            message = VolumeBoxes::kLeft;
        } else {
            continue;
        }
        // Each message goes at once; a handler that disarmed or freed the sphere ends the check.
        if (send) {
            send(object, message, subject.handle);
        }
        if (!stillChecked(index, object) || !m_spheres[index].armed) {
            return;
        }
    }
    // The occupants: one whose handle no longer resolves is skipped and kept; one dead or no longer accepted leaves.
    std::size_t place = 0;
    while (stillChecked(index, object) && place < m_spheres[index].occupants.size()) {
        TriggerSphere& sphere = m_spheres[index];
        const double kept = sphere.occupants[place];
        const auto subject = std::ranges::find(subjects, kept, &BoxSubject::handle);
        const std::optional<std::array<float, 3>> where =
            subject != subjects.end() ? std::optional<std::array<float, 3>>(subject->position) : m_locate(kept);
        const bool dead = subject != subjects.end() && !subject->alive;
        if (!where || (!dead && accepts(sphere, *centre, *where))) {
            ++place;
            continue;
        }
        sphere.occupants.erase(sphere.occupants.begin() + static_cast<std::ptrdiff_t>(place));
        if (send) {
            send(object, VolumeBoxes::kLeft, kept);
        }
    }
}

void TriggerSpheres::addOccupant(TriggerSphere& sphere, double human) const {
    if (sphere.occupants.size() < VolumeBoxes::kMaxOccupants) {
        sphere.occupants.push_back(human);
        return;
    }
    // A full list takes the first place whose handle no longer resolves, and none when every one does.
    const auto stale = std::ranges::find_if(sphere.occupants, [this](double kept) { return !m_locate(kept); });
    if (stale != sphere.occupants.end()) {
        *stale = human;
    }
}

bool TriggerSpheres::withinRadius(const TriggerSphere& sphere, const std::array<float, 3>& centre,
                                  const std::array<float, 3>& point) {
    const float dx = point[0] - centre[0];
    const float dy = point[1] - centre[1];
    const float dz = point[2] - centre[2];
    return (dx * dx) + (dy * dy) + (dz * dz) <= sphere.radius * sphere.radius;
}

} // namespace coney::world_objects
