// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/trigger_spheres.h"

#include <algorithm>
#include <utility>

namespace coney::world_objects {

namespace {

// One message a sphere's check queues for after the check.
struct Message {
    int message = 0;
    double human = 0;
};

// `p` raised by `height` metres.
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

TriggerSphere* TriggerSpheres::find(double object) {
    const auto found = std::ranges::find(m_spheres, object, &TriggerSphere::object);
    return found == m_spheres.end() ? nullptr : &*found;
}

const TriggerSphere* TriggerSpheres::find(double object) const {
    const auto found = std::ranges::find(m_spheres, object, &TriggerSphere::object);
    return found == m_spheres.end() ? nullptr : &*found;
}

bool TriggerSpheres::accepts(const TriggerSphere& sphere, const std::array<float, 3>& centre,
                             const std::array<float, 3>& point) const {
    const float dx = point[0] - centre[0];
    const float dy = point[1] - centre[1];
    const float dz = point[2] - centre[2];
    if ((dx * dx) + (dy * dy) + (dz * dz) > sphere.radius * sphere.radius) {
        return false;
    }
    if (sphere.mode == 0 || !m_clearLine) {
        return true;
    }
    const std::array<float, 3> from = sphere.mode == 2 ? raised(centre, kLineRaise) : centre;
    return m_clearLine(from, raised(point, kLineRaise));
}

void TriggerSpheres::update(std::span<const BoxSubject> subjects, std::uint64_t nowMs, const VolumeBoxes::Send& send) {
    const std::uint32_t turn = m_frame % kFramesPerCheck;
    ++m_frame;
    if (!m_locate) {
        return;
    }
    // By index, not by reference: a handler may configure a new sphere, which can move the pool.
    for (std::size_t i = turn; i < m_spheres.size(); i += kFramesPerCheck) {
        TriggerSphere& sphere = m_spheres[i];
        if (!sphere.armed) {
            continue;
        }
        const std::optional<std::array<float, 3>> centre = m_locate(sphere.object);
        if (!centre) {
            continue;
        }
        std::vector<Message> messages;
        const bool stayDue = nowMs >= sphere.nextStayMs;
        bool stayed = false;
        // The living humans inside: new ones enter, kept ones stay.
        for (const BoxSubject& subject : subjects) {
            if (!subject.alive || subject.handle == sphere.object || !accepts(sphere, *centre, subject.position)) {
                continue;
            }
            if (std::ranges::find(sphere.occupants, subject.handle) == sphere.occupants.end()) {
                if (sphere.occupants.size() < VolumeBoxes::kMaxOccupants) {
                    sphere.occupants.push_back(subject.handle);
                    messages.push_back({VolumeBoxes::kEntered, subject.handle});
                }
            } else if (stayDue) {
                messages.push_back({VolumeBoxes::kInside, subject.handle});
                stayed = true;
            }
        }
        if (stayed) {
            sphere.nextStayMs = nowMs + sphere.stayPeriodMs;
        }
        // Kept humans that left, died or are gone.
        std::erase_if(sphere.occupants, [&](double kept) {
            const auto found = std::ranges::find(subjects, kept, &BoxSubject::handle);
            if (found != subjects.end() && found->alive && accepts(sphere, *centre, found->position)) {
                return false;
            }
            messages.push_back({VolumeBoxes::kLeft, kept});
            return true;
        });
        // A handler may change the spheres, so the messages go once the sphere is done with.
        const double object = sphere.object;
        for (const Message& message : messages) {
            if (send) {
                send(object, message.message, message.human);
            }
        }
    }
}

} // namespace coney::world_objects
