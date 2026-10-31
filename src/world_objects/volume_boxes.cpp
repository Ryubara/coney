// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/volume_boxes.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace coney::world_objects {

const VolumeBox* VolumeBoxes::add(double handle, std::string_view name, int kind, const std::array<float, 3>& corner,
                                  const std::array<float, 3>& size, bool enabled) {
    // Kind 1 is refused. Any other kind but 0, 2 and 3 has no pool, and the original goes on with a null record:
    // Coney's choice is to make none. Stand-in: Coney's boxes come from no fixed pool, so none is ever full.
    if (!makesKind(kind)) {
        return nullptr;
    }
    VolumeBox box;
    box.handle = handle;
    box.name = std::string(name);
    box.kind = kind;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        // Stand-in: the corners are sorted where the original keeps them as given; no script adds a box with a
        // negative size, so the two agree for every box the game makes.
        box.low[axis] = std::min(corner[axis], corner[axis] + size[axis]);
        box.high[axis] = std::max(corner[axis], corner[axis] + size[axis]);
    }
    box.enabled = enabled;
    m_boxes.push_back(std::move(box));
    return &m_boxes.back();
}

void VolumeBoxes::rotate(double handle, const std::array<float, 4>& turn) {
    if (VolumeBox* box = find(handle)) {
        box->turn = turn;
    }
}

// @coney-only lookup over Coney's own list
VolumeBox* VolumeBoxes::find(double handle) {
    const auto found = std::ranges::find(m_boxes, handle, &VolumeBox::handle);
    return found == m_boxes.end() ? nullptr : &*found;
}

// @coney-only lookup over Coney's own list
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
    // By index, not by reference: a handler may add a box, which can move the others. A box's handle index is
    // 620 + its place in the list, since boxes take the first free index in the order added and none is freed alone.
    // Stand-in: the camera-inside mark (+0x69) the same walk keeps for boxes with flags byte +0x6a is not ported.
    for (std::size_t i = 0; i < m_boxes.size() && i < kUpdatedBoxes; ++i) {
        if ((kFirstHandleIndex + i) % kFramesPerUpdate != m_frame || !m_boxes[i].enabled) {
            continue;
        }
        // The box's own update (vtable +0x54); a turf box (kind 3) has none that sends anything.
        if (m_boxes[i].kind == 0) {
            updateBox(i, subjects, nowMs, send);
        } else if (m_boxes[i].kind == 2) {
            updatePlayerBox(i, subjects, nowMs, send);
        }
    }
    m_frame = (m_frame + 1) % kFramesPerUpdate;
}

bool VolumeBoxes::stillEnabled(std::size_t index, double handle) const {
    // A handler may disable the box or clear every box.
    return index < m_boxes.size() && m_boxes[index].handle == handle && m_boxes[index].enabled;
}

bool VolumeBoxes::stayDue(VolumeBox& box, std::uint64_t nowMs) const {
    // Due only once the time is strictly past the timer, which is then re-armed whether or not anyone is inside.
    if (nowMs <= box.nextStayMs) {
        return false;
    }
    box.nextStayMs = nowMs + (m_stayPeriod ? m_stayPeriod(box.handle) : kStayPeriodMs);
    return true;
}

void VolumeBoxes::addOccupant(VolumeBox& box, double human, std::size_t room) const {
    // Coney's choice: the original fills the first fixed slot that does not resolve, a hole or a stale handle
    // (inferred, as the sphere's 0x004f5cc8); Coney's list stays packed and appends, so the order of the list, and
    // of several 4s in one occupants' pass, may differ.
    if (box.occupants.size() < room) {
        box.occupants.push_back(human);
        return;
    }
    // A full list takes the first place whose handle no longer resolves, and none when every one does.
    if (m_resolves) {
        const auto stale = std::ranges::find_if(box.occupants, [this](double kept) { return !m_resolves(kept); });
        if (stale != box.occupants.end()) {
            *stale = human;
        }
    }
}

void VolumeBoxes::updateBox(std::size_t index, std::span<const BoxSubject> subjects, std::uint64_t nowMs,
                            const Send& send) {
    const double handle = m_boxes[index].handle;
    const bool due = stayDue(m_boxes[index], nowMs);
    // The found humans, in Coney's subjects order. Stand-in: a box tests every subject with its inside test, where the
    // original first takes at most 60 within its bounding sphere.
    for (const BoxSubject& subject : subjects) {
        if (!stillEnabled(index, handle)) {
            return;
        }
        // A dead human is passed over here; the occupants' pass below sends him 4.
        if (!subject.alive) {
            continue;
        }
        VolumeBox& box = m_boxes[index];
        const auto listed = std::ranges::find(box.occupants, subject.handle);
        int message = 0;
        if (inside(box, subject.position)) {
            if (listed == box.occupants.end()) {
                // Message 3 goes even when every place is taken.
                addOccupant(box, subject.handle, kMaxOccupants);
                message = kEntered;
            } else if (due) {
                message = kInside;
            } else {
                continue;
            }
        } else if (listed != box.occupants.end()) {
            box.occupants.erase(listed);
            message = kLeft;
        } else {
            continue;
        }
        // Each message goes at once.
        if (send) {
            send(handle, message, subject.handle);
        }
    }
    // The occupants, while the box is enabled: one whose handle no longer resolves is skipped and kept; a dead one
    // gets 4 and then loses his place; one no longer inside loses his place and then gets 4.
    std::size_t place = 0;
    while (stillEnabled(index, handle) && place < m_boxes[index].occupants.size()) {
        VolumeBox& box = m_boxes[index];
        const double kept = box.occupants[place];
        const auto subject = std::ranges::find(subjects, kept, &BoxSubject::handle);
        if (subject == subjects.end() && m_resolves && !m_resolves(kept)) {
            ++place;
            continue;
        }
        // Stand-in: a human that resolves but is no subject (no position Coney can test) is no longer inside.
        if (subject != subjects.end() && !subject->alive) {
            if (send) {
                send(handle, kLeft, kept);
            }
            // The handler may have changed the list: the entry is dropped only where it still is.
            if (stillEnabled(index, handle) && place < m_boxes[index].occupants.size() &&
                m_boxes[index].occupants[place] == kept) {
                m_boxes[index].occupants.erase(m_boxes[index].occupants.begin() + static_cast<std::ptrdiff_t>(place));
            }
            continue;
        }
        if (subject != subjects.end() && inside(box, subject->position)) {
            ++place;
            continue;
        }
        box.occupants.erase(box.occupants.begin() + static_cast<std::ptrdiff_t>(place));
        if (send) {
            send(handle, kLeft, kept);
        }
    }
}

void VolumeBoxes::updatePlayerBox(std::size_t index, std::span<const BoxSubject> subjects, std::uint64_t nowMs,
                                  const Send& send) {
    const double handle = m_boxes[index].handle;
    const bool due = stayDue(m_boxes[index], nowMs);
    // The two players' humans. Stand-in: the first two players among Coney's subjects, by handle, for the original's
    // player slots in order; a player who is no subject does not resolve and is passed over.
    std::size_t players = 0;
    for (const BoxSubject& subject : subjects) {
        if (!subject.player) {
            continue;
        }
        if (players == kMaxPlayerOccupants || !stillEnabled(index, handle)) {
            return;
        }
        ++players;
        VolumeBox& box = m_boxes[index];
        const auto listed = std::ranges::find(box.occupants, subject.handle);
        int message = 0;
        if (subject.alive && inside(box, subject.position)) {
            if (listed == box.occupants.end()) {
                addOccupant(box, subject.handle, kMaxPlayerOccupants);
                message = kEntered;
            } else if (due) {
                message = kInside;
            } else {
                continue;
            }
        } else if (listed != box.occupants.end()) {
            box.occupants.erase(listed);
            message = kLeft;
        } else {
            continue;
        }
        // Each message goes at once.
        if (send) {
            send(handle, message, subject.handle);
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

void VolumeBoxes::sendTheft(const std::array<float, 3>& spot, const std::function<void(double box)>& send) const {
    for (const VolumeBox& box : m_boxes) {
        if (box.enabled && box.kind == 0 && inside(box, spot)) {
            send(box.handle);
        }
    }
}

} // namespace coney::world_objects
