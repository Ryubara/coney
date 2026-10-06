// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/formations.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>

#include "ai/brain.h"
#include "ai/route_planner.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// Metres to sixteenths of a metre, rounded to the nearest and clamped to an s16.
std::int16_t toUnits(float metres) {
    const float units = std::round(metres * kFormationUnitsPerMetre);
    return static_cast<std::int16_t>(std::clamp(units, -32768.0F, 32767.0F));
}

// Sixteenths of a metre to metres.
float toMetres(std::int16_t units) { return static_cast<float>(units) / kFormationUnitsPerMetre; }

// The horizontal squared distance between two points.
float planDistanceSquared(anim::Vec3 a, anim::Vec3 b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

// The z of the 2D cross product of (b - a) and (c - a): its sign says which side of a-b c lies on.
float side(anim::Vec3 a, anim::Vec3 b, anim::Vec3 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

// Whether segments p1-p2 and q1-q2 cross in plan (touching does not count), as `0x00337308` asks.
bool cross(anim::Vec3 p1, anim::Vec3 p2, anim::Vec3 q1, anim::Vec3 q2) {
    return side(p1, p2, q1) * side(p1, p2, q2) < 0.0F && side(q1, q2, p1) * side(q1, q2, p2) < 0.0F;
}

} // namespace

Formation::Formation(Brain& leader) : m_leader(&leader) {}

void Formation::setSlotCount(int count, int allowed, std::uint64_t nowMs) {
    m_slotCount = std::clamp(count, 0, static_cast<int>(kFormationSlots));
    m_allowed = std::clamp(allowed < 0 ? count : allowed, 0, static_cast<int>(kFormationSlots));
    plan(nowMs);
}

void Formation::setSlotSet(int set, std::uint64_t nowMs) {
    if (set < 0 || set >= static_cast<int>(kFormationSets)) {
        return;
    }
    m_set = set;
    plan(nowMs);
}

void Formation::setSlot(int slot, float x, float y, int set, std::uint64_t nowMs) {
    if (slot < 0 || slot >= static_cast<int>(kFormationSlots) || set < 0 || set >= static_cast<int>(kFormationSets)) {
        return;
    }
    FollowSlot& entry = m_sets[static_cast<std::size_t>(set)][static_cast<std::size_t>(slot)];
    entry.offset = {toUnits(x), toUnits(y), 0};
    plan(nowMs);
}

void Formation::setDefaultSlot(int slot, float x, float y, int set) {
    if (slot < 0 || slot >= static_cast<int>(kFormationSlots) || set < 0 || set >= static_cast<int>(kFormationSets)) {
        return;
    }
    m_sets[static_cast<std::size_t>(set)][static_cast<std::size_t>(slot)].offset = {toUnits(x), toUnits(y), 0};
}

bool Formation::join(Brain& follower) {
    if (follower.following() == this) {
        return true;
    }
    if (m_followers.size() >= kFormationFollowers || &follower == m_leader) {
        return false;
    }
    m_followers.push_back(Follower{.brain = &follower});
    follower.setFollowing(this);
    return true;
}

void Formation::leave(Brain& follower) {
    std::erase_if(m_followers, [&follower](const Follower& f) { return f.brain == &follower; });
    for (Follower& entry : m_followers) {
        if (entry.behind == &follower) {
            entry.behind = nullptr;
        }
    }
    if (follower.following() == this) {
        follower.setFollowing(nullptr);
    }
}

void Formation::update(std::uint64_t nowMs) {
    if (m_followers.empty()) {
        return;
    }
    // The leader has just stopped.
    const float speed = std::hypot(m_leader->human().velocity().x, m_leader->human().velocity().y);
    if (speed <= kStoppedSpeed && m_lastSpeed >= kStoppedSpeed) {
        m_stopped = true;
    }
    m_lastSpeed = speed;
    const bool moved =
        planDistanceSquared(m_leader->human().position(), m_planPoint) >= kPlanMoveDistance * kPlanMoveDistance;
    if (m_stopped || m_nextPlanMs <= nowMs || moved) {
        plan(nowMs);
    }
}

void Formation::plan(std::uint64_t nowMs) {
    m_stopped = false;
    const human::Human& leader = m_leader->human();
    // 1. When to plan next, and from where: 2 m ahead while the leader runs.
    m_nextPlanMs = nowMs + (leader.gait() == human::Gait::Walk ? kPlanWalkingMs : kPlanOtherMs);
    m_planHeading = leader.heading();
    m_planPoint = leader.position();
    const anim::Vec3 forward = human::facing(m_planHeading);
    if (leader.gait() >= human::Gait::Run) {
        m_planPoint = anim::add(m_planPoint, anim::scale(forward, kPlanLeadDistance));
    }
    // 2. The slots' points: the offset turned by the leader's heading (x to his right, y ahead); usable when he can
    // walk there, up to the followers allowed.
    const anim::Vec3 right{forward.y, -forward.x, 0.0F};
    const auto set = static_cast<std::size_t>(m_set);
    m_usable[set].fill(false);
    int usable = 0;
    for (int slot = 0; slot < m_slotCount && usable < m_allowed; ++slot) {
        FollowSlot& entry = m_sets[set][static_cast<std::size_t>(slot)];
        const anim::Vec3 point = anim::add(m_planPoint, anim::add(anim::scale(right, toMetres(entry.offset[0])),
                                                                  anim::scale(forward, toMetres(entry.offset[1]))));
        entry.point = {toUnits(point.x), toUnits(point.y), toUnits(point.z)};
        const RoutePlanner* planner = m_leader->planner();
        if (planner == nullptr || planner->lineClear(leader.position(), point)) {
            m_usable[set][static_cast<std::size_t>(slot)] = true;
            ++usable;
        }
    }
    // 3. Followers whose human is gone leave; then the assignment.
    std::erase_if(m_followers, [](const Follower& f) { return f.brain == nullptr; });
    assign();
}

void Formation::assign() {
    const auto set = static_cast<std::size_t>(m_set);
    for (Follower& entry : m_followers) {
        entry.slot = -1;
        entry.behind = nullptr;
    }
    // Each usable slot takes its nearest unassigned follower.
    for (int slot = 0; slot < m_slotCount; ++slot) {
        if (!m_usable[set][static_cast<std::size_t>(slot)]) {
            continue;
        }
        const FollowSlot& entry = m_sets[set][static_cast<std::size_t>(slot)];
        const anim::Vec3 point{toMetres(entry.point[0]), toMetres(entry.point[1]), toMetres(entry.point[2])};
        Follower* nearest = nullptr;
        float best = std::numeric_limits<float>::max();
        for (Follower& candidate : m_followers) {
            if (candidate.slot >= 0) {
                continue;
            }
            if (const float d = planDistanceSquared(candidate.brain->human().position(), point); d < best) {
                best = d;
                nearest = &candidate;
            }
        }
        if (nearest != nullptr) {
            nearest->slot = slot;
        }
    }
    // The rest queue nearest-first behind the slotted followers in turn (behind the leader when none has a slot).
    std::vector<Brain*> heads;
    for (const Follower& entry : m_followers) {
        if (entry.slot >= 0) {
            heads.push_back(entry.brain);
        }
    }
    if (heads.empty()) {
        heads.push_back(m_leader);
    }
    std::size_t turn = 0;
    while (true) {
        Follower* nearest = nullptr;
        float best = std::numeric_limits<float>::max();
        for (Follower& candidate : m_followers) {
            if (candidate.slot >= 0 || candidate.behind != nullptr) {
                continue;
            }
            const float d = planDistanceSquared(candidate.brain->human().position(), heads[turn]->human().position());
            if (d < best) {
                best = d;
                nearest = &candidate;
            }
        }
        if (nearest == nullptr) {
            break;
        }
        nearest->behind = heads[turn];
        heads[turn] = nearest->brain;
        turn = (turn + 1) % heads.size();
    }
    // Slotted pairs whose paths cross swap slots, for up to kSwapPasses passes while any does.
    for (int pass = 0; pass < kSwapPasses; ++pass) {
        bool swapped = false;
        for (Follower& a : m_followers) {
            for (Follower& b : m_followers) {
                if (&a == &b || a.slot < 0 || b.slot < 0) {
                    continue;
                }
                const std::optional<anim::Vec3> to = slotPoint(*a.brain);
                const std::optional<anim::Vec3> other = slotPoint(*b.brain);
                if (to && other && cross(a.brain->human().position(), *to, b.brain->human().position(), *other)) {
                    std::swap(a.slot, b.slot);
                    swapped = true;
                }
            }
        }
        if (!swapped) {
            break;
        }
    }
}

std::optional<anim::Vec3> Formation::slotPoint(const Brain& follower) const {
    const Follower* entry = this->follower(follower);
    if (entry == nullptr || entry->slot < 0) {
        return std::nullopt;
    }
    const FollowSlot& slot = m_sets[static_cast<std::size_t>(m_set)][static_cast<std::size_t>(entry->slot)];
    return anim::Vec3{toMetres(slot.point[0]), toMetres(slot.point[1]), toMetres(slot.point[2])};
}

const Follower* Formation::follower(const Brain& brain) const {
    const auto found = std::ranges::find_if(m_followers, [&brain](const Follower& f) { return f.brain == &brain; });
    return found == m_followers.end() ? nullptr : &*found;
}

const FollowSlot& Formation::slot(int set, int slot) const {
    return m_sets.at(static_cast<std::size_t>(set)).at(static_cast<std::size_t>(slot));
}

bool Formation::usable(int set, int slot) const {
    return m_usable.at(static_cast<std::size_t>(set)).at(static_cast<std::size_t>(slot));
}

Formation* Formations::of(Brain& leader, bool make) {
    const auto found = std::ranges::find_if(
        m_formations, [&leader](const std::unique_ptr<Formation>& f) { return &f->leader() == &leader; });
    if (found != m_formations.end()) {
        return found->get();
    }
    if (!make || m_formations.size() >= kFormationPool) {
        return nullptr;
    }
    m_formations.push_back(std::make_unique<Formation>(leader));
    // The pool's records keep the default slots the configuration wrote.
    for (std::size_t set = 0; set < m_defaults.size(); ++set) {
        for (std::size_t slot = 0; slot < m_defaults.at(set).size(); ++slot) {
            const auto [x, y] = m_defaults.at(set).at(slot);
            m_formations.back()->setDefaultSlot(static_cast<int>(slot), x, y, static_cast<int>(set));
        }
    }
    return m_formations.back().get();
}

void Formations::setDefaults(int set, std::span<const std::pair<float, float>> slots) {
    if (set < 0 || set >= static_cast<int>(kFormationSets)) {
        return;
    }
    const std::size_t count = std::min(slots.size(), kFormationSlots);
    m_defaults.at(static_cast<std::size_t>(set))
        .assign(slots.begin(), slots.begin() + static_cast<std::ptrdiff_t>(count));
    for (const std::unique_ptr<Formation>& formation : m_formations) {
        for (std::size_t slot = 0; slot < count; ++slot) {
            formation->setDefaultSlot(static_cast<int>(slot), slots[slot].first, slots[slot].second, set);
        }
    }
}

void Formations::update(std::uint64_t nowMs) {
    for (const std::unique_ptr<Formation>& formation : m_formations) {
        formation->update(nowMs);
    }
}

void Formations::forget(Brain& brain) {
    if (Formation* followed = brain.following(); followed != nullptr) {
        followed->leave(brain);
    }
    const auto own = std::ranges::find_if(
        m_formations, [&brain](const std::unique_ptr<Formation>& f) { return &f->leader() == &brain; });
    if (own != m_formations.end()) {
        for (const Follower& entry : (*own)->followers()) {
            entry.brain->setFollowing(nullptr);
        }
        m_formations.erase(own);
    }
}

} // namespace coney::ai
