// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/sectors.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

#include "ai/brain.h"
#include "ai/route_planner.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"

namespace coney::ai {

namespace {

constexpr float kSectorWidth = std::numbers::pi_v<float> / 4.0F;
constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;

// The squared distance in plan between two points.
float planSquared(anim::Vec3 a, anim::Vec3 b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return dx * dx + dy * dy;
}

// Whether a player is busy for the free-player flag (`Human_IsBusy`). **Coney stand-in**: busy while not standing or
// while his record holds a busy bit, as Coney's fighter states stand for the original's state word.
bool busy(const human::Human& human) {
    return human.state() != human::TargetState::Standing || (human.animator().flags() & human::kBusyFlags) != 0;
}

} // namespace

int sectorOf(const human::Human& owner, anim::Vec3 point) {
    const anim::Vec3 way = anim::subtract(point, owner.position());
    float bearing = human::headingOf(way) - owner.heading();
    bearing = std::fmod(bearing, kTwoPi);
    if (bearing < 0.0F) {
        bearing += kTwoPi;
    }
    // On the left half an edge goes to the lower index, on the right half to the higher.
    const float half = kSectorWidth * 0.5F;
    const int index = bearing <= std::numbers::pi_v<float>
                          ? static_cast<int>(std::ceil((bearing - half) / kSectorWidth))
                          : static_cast<int>(std::floor((bearing + half) / kSectorWidth));
    return wrapSector(index);
}

anim::Vec3 sectorPoint(const human::Human& owner, int k, float distance) {
    const anim::Vec3 way = human::facing(owner.heading() + static_cast<float>(wrapSector(k)) * kSectorWidth);
    const anim::Vec3 at = owner.position();
    return anim::Vec3{at.x + way.x * distance, at.y + way.y * distance, at.z};
}

int sectorTurnWay(int from, int to) {
    const int ahead = wrapSector(to - from);
    return ahead <= kSectorCount / 2 ? 1 : -1;
}

void Sectors::reset() {
    m_sectors.fill(Sector{});
    m_heading = 0.0F;
    m_refreshedMs = 0;
    m_built = false;
}

bool Sectors::due(std::uint64_t nowMs, std::uint64_t maxAgeMs) const {
    return !m_built || nowMs >= m_refreshedMs + maxAgeMs;
}

void Sectors::rebuild(const Brain& owner, std::span<const Brain* const> others, std::uint64_t nowMs) {
    // 1. The heading kept; every sector emptied with its probe armed.
    const human::Human& self = owner.human();
    const anim::Vec3 at = self.position();
    m_heading = self.heading();
    m_sectors.fill(Sector{});
    // 2-3. Each human within 1.5 m counts in his sector; the nearest is kept with his squared distance.
    std::array<float, kSectorCount> nearest{};
    nearest.fill(std::numeric_limits<float>::max());
    const float reach = kSectorReach * kSectorReach;
    for (const Brain* other : others) {
        if (other == nullptr || other == &owner || other->human().outOfWorld()) {
            continue;
        }
        const float squared = planSquared(at, other->human().position());
        if (squared > reach) {
            continue;
        }
        const auto k = static_cast<std::size_t>(sectorOf(self, other->human().position()));
        Sector& sector = m_sectors[k];
        sector.count = static_cast<std::uint8_t>(std::min(sector.count + 1, 255));
        if (squared < nearest[k]) {
            nearest[k] = squared;
            sector.nearest = other;
        }
    }
    // 4. The flags from the nearest's squared distance: 3 below 1.5, else 1 below 2.5; an empty sector 0.
    for (std::size_t k = 0; k < m_sectors.size(); ++k) {
        if (m_sectors[k].nearest == nullptr) {
            continue;
        }
        if (nearest[k] < kSectorVeryCloseSquared) {
            m_sectors[k].flags = sector_flag::kOccupied | sector_flag::kVeryClose;
        } else if (nearest[k] < kSectorOccupiedSquared) {
            m_sectors[k].flags = sector_flag::kOccupied;
        }
    }
    // 5. For an AI owner, flag 8 where a free player stands within 5.5 m.
    if (owner.type() != BrainType::Player) {
        const Brain* target = owner.target();
        const Brain* targetsTarget = target != nullptr ? target->target() : nullptr;
        for (const Brain* other : others) {
            if (other == nullptr || other == &owner || other->type() != BrainType::Player ||
                other->human().outOfWorld() || busy(other->human())) {
                continue;
            }
            if (planSquared(at, other->human().position()) >= kSectorFreePlayerSquared || other == target ||
                other == targetsTarget) {
                continue;
            }
            if (other->target() == &owner || (target != nullptr && other->target() == target)) {
                continue;
            }
            m_sectors[static_cast<std::size_t>(sectorOf(self, other->human().position()))].flags |=
                sector_flag::kFreePlayer;
        }
    }
    // 6. The time.
    m_refreshedMs = nowMs;
    m_built = true;
}

void Sectors::probe(const Brain& owner, int k) {
    Sector& sector = m_sectors[static_cast<std::size_t>(wrapSector(k))];
    if (!sector.probePending) {
        return;
    }
    sector.probePending = false;
    const RoutePlanner* planner = owner.planner();
    if (planner == nullptr) {
        return;
    }
    const anim::Vec3 at = owner.human().position();
    const anim::Vec3 way = human::facing(m_heading + static_cast<float>(wrapSector(k)) * kSectorWidth);
    const anim::Vec3 point{at.x + way.x * kSectorProbeDistance, at.y + way.y * kSectorProbeDistance, at.z};
    if (!planner->lineClear(at, point)) {
        sector.flags |= sector_flag::kWall;
    }
}

bool Sectors::blocked(const Brain& owner, int k) {
    if (((*this)[k].flags & sector_flag::kVeryClose) != 0) {
        return true;
    }
    probe(owner, k);
    return ((*this)[k].flags & (sector_flag::kVeryClose | sector_flag::kWall | sector_flag::kFreePlayer)) != 0;
}

bool Sectors::free(const Brain& owner, int k) {
    if (((*this)[k].flags & (sector_flag::kOccupied | sector_flag::kVeryClose | sector_flag::kFreePlayer)) != 0) {
        return false;
    }
    probe(owner, k);
    return ((*this)[k].flags & sector_flag::kWall) == 0;
}

bool Sectors::wall(const Brain& owner, int k) {
    probe(owner, k);
    return ((*this)[k].flags & sector_flag::kWall) != 0;
}

int Sectors::cost(const Brain& owner, int k) {
    probe(owner, k);
    const Sector& sector = (*this)[k];
    return static_cast<int>(sector.flags) + 2 * static_cast<int>(sector.count);
}

bool Sectors::allClear() const {
    return std::ranges::none_of(m_sectors, [](const Sector& sector) {
        return (sector.flags & (sector_flag::kOccupied | sector_flag::kVeryClose)) != 0;
    });
}

bool Sectors::heldBy(const Brain& human) const {
    return std::ranges::any_of(m_sectors, [&human](const Sector& sector) { return sector.nearest == &human; });
}

void Sectors::forget(const Brain& other) {
    for (Sector& sector : m_sectors) {
        if (sector.nearest == &other) {
            sector.nearest = nullptr;
        }
    }
}

} // namespace coney::ai
