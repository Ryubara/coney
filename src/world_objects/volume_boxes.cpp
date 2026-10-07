// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/volume_boxes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace coney::world_objects {

namespace {

// One message a box's update queues for after the update.
struct Message {
    int message = 0;
    double human = 0;
};

} // namespace

const VolumeBox& VolumeBoxes::add(double handle, std::string_view name, int kind, const std::array<float, 3>& corner,
                                  const std::array<float, 3>& size, bool enabled) {
    VolumeBox box;
    box.handle = handle;
    box.name = std::string(name);
    box.kind = kind;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        // A negative size still gives a box: the corners are sorted (**Coney choice**: not traced).
        box.low[axis] = std::min(corner[axis], corner[axis] + size[axis]);
        box.high[axis] = std::max(corner[axis], corner[axis] + size[axis]);
    }
    box.enabled = enabled;
    m_boxes.push_back(std::move(box));
    return m_boxes.back();
}

void VolumeBoxes::rotate(double handle, const std::array<float, 4>& turn) {
    if (VolumeBox* box = find(handle)) {
        box->turn = turn;
    }
}

VolumeBox* VolumeBoxes::find(double handle) {
    const auto found = std::ranges::find(m_boxes, handle, &VolumeBox::handle);
    return found == m_boxes.end() ? nullptr : &*found;
}

const VolumeBox* VolumeBoxes::find(double handle) const {
    const auto found = std::ranges::find(m_boxes, handle, &VolumeBox::handle);
    return found == m_boxes.end() ? nullptr : &*found;
}

bool VolumeBoxes::inside(const VolumeBox& box, const std::array<float, 3>& point) {
    if (point[2] < box.low[2] || point[2] > box.high[2]) {
        return false;
    }
    // The corners are turned about the centre by the matrix; the point is turned back by its inverse instead and
    // tested against the unturned half extents.
    const float cx = (box.low[0] + box.high[0]) * 0.5F;
    const float cy = (box.low[1] + box.high[1]) * 0.5F;
    const float dx = point[0] - cx;
    const float dy = point[1] - cy;
    const auto [m00, m01, m10, m11] = box.turn;
    const float det = (m00 * m11) - (m01 * m10);
    if (std::fabs(det) < 1e-6F) {
        return false;
    }
    const float x = ((m11 * dx) - (m01 * dy)) / det;
    const float y = ((m00 * dy) - (m10 * dx)) / det;
    return std::fabs(x) <= (box.high[0] - box.low[0]) * 0.5F && std::fabs(y) <= (box.high[1] - box.low[1]) * 0.5F;
}

void VolumeBoxes::update(std::span<const BoxSubject> subjects, std::uint64_t nowMs, const Send& send) {
    for (VolumeBox& box : m_boxes) {
        if (!box.enabled || (box.kind != 0 && box.kind != 2)) {
            continue;
        }
        // A kind-2 box tests the two players' humans only.
        const bool playersOnly = box.kind == 2;
        const std::size_t room = playersOnly ? kMaxPlayerOccupants : kMaxOccupants;
        std::vector<Message> messages;
        // The living humans inside: new ones enter, kept ones stay.
        const bool stayDue = nowMs >= box.nextStayMs;
        bool stayed = false;
        for (const BoxSubject& subject : subjects) {
            if (!subject.alive || (playersOnly && !subject.player) || !inside(box, subject.position)) {
                continue;
            }
            if (std::ranges::find(box.occupants, subject.handle) == box.occupants.end()) {
                if (box.occupants.size() < room) {
                    box.occupants.push_back(subject.handle);
                    messages.push_back({kEntered, subject.handle});
                }
            } else if (stayDue) {
                messages.push_back({kInside, subject.handle});
                stayed = true;
            }
        }
        if (stayed) {
            box.nextStayMs = nowMs + kStayPeriodMs;
        }
        // Kept humans that left, died or are gone.
        std::erase_if(box.occupants, [&](double kept) {
            const auto found = std::ranges::find(subjects, kept, &BoxSubject::handle);
            if (found != subjects.end() && found->alive && inside(box, found->position)) {
                return false;
            }
            // A handle that no longer names anything is skipped: no message, and its entry stays.
            if (found == subjects.end() && m_resolves && !m_resolves(kept)) {
                return false;
            }
            messages.push_back({kLeft, kept});
            return true;
        });
        // A handler may change the boxes, so the messages go once the box is done with.
        const double handle = box.handle;
        for (const Message& message : messages) {
            if (send) {
                send(handle, message.message, message.human);
            }
        }
    }
}

void VolumeBoxes::sendDamage(const std::array<float, 3>& human, const std::function<void(double box)>& send) const {
    // The human's position decides, not the object's: a box hears the damage done from inside it.
    for (const VolumeBox& box : m_boxes) {
        if (box.enabled && box.kind == 0 && inside(box, human)) {
            send(box.handle);
        }
    }
}

} // namespace coney::world_objects
