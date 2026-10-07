// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "animation/anim_math.h"

namespace coney::human {
class Human;
} // namespace coney::human

// The neighbour sectors: who stands round a human in eight 45° sectors, rebuilt at most once per a caller's maximum age
// and kept per brain. The fight's footwork, the grab and snap tries, the steering round humans and giving way read it.
// A wall probe per sector runs lazily, once per rebuild, when a reader needs it.
// Research: docs/research/ai.md#neighbour-sectors

namespace coney::ai {

class Brain;

/// The sectors round a human: 8, sector 0 straight ahead, the index rising anticlockwise (2 his left, 4 behind, 6 his
/// right).
inline constexpr int kSectorCount = 8;
/// How far round the owner a rebuild looks for humans, m (`Humans_FindAhead(1.5, …)`).
inline constexpr float kSectorReach = 1.5F;
/// How far out the wall probe tests a walkable line, m.
inline constexpr float kSectorProbeDistance = 1.5F;
/// The squared distances below which the nearest human sets the sector's flags 3 (within 1.22 m) or 1. **Coney
/// reading**: the original compares metres-looking thresholds against a squared distance; kept as it does.
inline constexpr float kSectorVeryCloseSquared = 1.5F;
inline constexpr float kSectorOccupiedSquared = 2.5F;
/// The squared distance within which a free player sets flag 8 in an AI owner's record (5.5 m).
inline constexpr float kSectorFreePlayerSquared = 30.25F;
/// The maximum ages callers pass, ms: the footwork, the steering and the fight's tries, and giving way.
inline constexpr std::uint64_t kSectorAgeMs = 1000;
inline constexpr std::uint64_t kSectorGiveWayAgeMs = 500;

/// A sector's flags (`+4`).
namespace sector_flag {
inline constexpr std::uint16_t kOccupied = 1;   ///< Someone in the sector (within 1.5 m).
inline constexpr std::uint16_t kVeryClose = 2;  ///< Someone within 1.22 m.
inline constexpr std::uint16_t kWall = 4;       ///< No walkable line 1.5 m out (the wall probe).
inline constexpr std::uint16_t kFreePlayer = 8; ///< A player within 5.5 m not part of this fight (AI owners only).
} // namespace sector_flag

/// One sector's entry (8 bytes at `+8k`).
struct Sector {
    const Brain* nearest = nullptr; ///< `+0`: the nearest human in the sector (null when none).
    std::uint16_t flags = 0;        ///< `+4`: sector_flag bits.
    bool probePending = true;       ///< `+6`: the wall probe has still to run.
    std::uint8_t count = 0;         ///< `+7`: how many humans stand in the sector within 1.5 m.
};

/// The sector of `point` round `owner` (`Sectors_SectorOf`, `Human_GetSectorOf`): the bearing of the point less the
/// owner's heading, wrapped to [0, 2π); sector k is centred on k × 45° with edges at ±22.5°. A bearing exactly on an
/// edge goes to the lower index on the left half (0 to π) and to the higher on the right half.
/// @orig 0x0029eb10 Sectors_SectorOf (unknown)
/// @orig 0x0029eb38 Human_GetSectorOf (unknown)
[[nodiscard]] int sectorOf(const human::Human& owner, anim::Vec3 point);

/// The point `distance` from `owner` toward sector `k`'s centre, along his **current** heading + k × 45°, at his
/// height (`Sectors_GetPoint`).
/// @orig 0x0029e350 Sectors_GetPoint (unknown)
[[nodiscard]] anim::Vec3 sectorPoint(const human::Human& owner, int k, float distance);

/// +1 or -1: the shorter way round from sector `from` to sector `to` (+1 anticlockwise; +1 for opposite sectors or the
/// same one). **Coney reading**: which way a tie goes is not traced.
/// @orig 0x0029ec90 Sectors_TurnWay (unknown)
[[nodiscard]] int sectorTurnWay(int from, int to);

/// The sector of `index` wrapped into 0-7.
[[nodiscard]] constexpr int wrapSector(int index) { return ((index % kSectorCount) + kSectorCount) % kSectorCount; }

/// A brain's sector record (60 records of `0x48` at `0x006e8318`, one per human slot). Brain::sectors() refreshes it.
class Sectors {
  public:
    /// Clears every handle, the heading and the refresh time, and arms every probe: as made, and at `Brain_Init`.
    /// @orig 0x0029e250 Sectors_Construct (unknown)
    /// @orig 0x0029e2b0 Sectors_Reset (unknown)
    void reset();

    /// Whether a rebuild is due at `nowMs` for a caller's `maxAgeMs`: now ≥ the refresh time + the age. **Coney
    /// choice**: a record never built is always due (the original's game clock is far past 0 when a level plays).
    [[nodiscard]] bool due(std::uint64_t nowMs, std::uint64_t maxAgeMs) const;

    /// Rebuilds the record round `owner` at `nowMs` from `others` (every brain of the scene; `owner` and humans out of
    /// the world are skipped): the heading stored, each human within 1.5 m counted in his sector and the nearest kept,
    /// the flags rewritten from the nearest's squared distance (3 below 1.5, else 1 below 2.5, 0 when empty), and for
    /// an AI owner flag 8 in the sector of each free player within 5.5 m (not busy, not the owner's target or its
    /// target's target, and not targeting either). Downed and busy humans count as neighbours.
    /// @orig 0x0029e5d8 Sectors_Update (unknown)
    void rebuild(const Brain& owner, std::span<const Brain* const> others, std::uint64_t nowMs);

    /// Sector `k` (0-7) as it stands (no probe).
    [[nodiscard]] const Sector& operator[](int k) const { return m_sectors[static_cast<std::size_t>(wrapSector(k))]; }
    /// The owner's heading at the last rebuild (`+0x40`) and its time, ms (`+0x44`).
    [[nodiscard]] float heading() const { return m_heading; }
    [[nodiscard]] std::uint64_t refreshedMs() const { return m_refreshedMs; }

    /// Whether sector `k` is blocked: at once when someone is within 1.22 m; otherwise, after the probe, when there is
    /// a very close human, a wall or a free player.
    /// @orig 0x0029e9c8 Sectors_IsBlocked (unknown)
    [[nodiscard]] bool blocked(const Brain& owner, int k);
    /// Whether sector `k` is free: none of flags 1, 2 or 8, then (after the probe) no wall.
    /// @orig 0x0029ea48 Sectors_IsFree (unknown)
    [[nodiscard]] bool free(const Brain& owner, int k);
    /// Whether sector `k` has a wall (after the probe).
    /// @orig 0x0029ea10 Sectors_IsWall (unknown)
    [[nodiscard]] bool wall(const Brain& owner, int k);
    /// Sector `k`'s cost: after the probe, its flags + 2 × its count.
    /// @orig 0x0029e980 Sectors_GetCost (unknown)
    [[nodiscard]] int cost(const Brain& owner, int k);
    /// Whether no sector has flag 1 or 2: no human within 1.5 m at all.
    /// @orig 0x0029eaa0 Sectors_AllClear (unknown)
    [[nodiscard]] bool allClear() const;
    /// Whether `human` is the nearest in some sector.
    /// @orig 0x0029ead8 Sectors_IsHeldBy (unknown)
    [[nodiscard]] bool heldBy(const Brain& human) const;

    /// Drops `other` from the record (it is going away).
    void forget(const Brain& other);

  private:
    // The wall probe of sector `k` when still pending: flag 4 when the owner cannot walk straight 1.5 m out at the
    // stored heading + k × 45° (the navigation mesh's walkable line; no planner, no wall). **Coney stand-in**: Coney
    // has no trains, so the train-path half of the test never fails.
    // @orig 0x0029e4b0 Sectors_ProbeWall (unknown)
    // @orig 0x002221e0 Human_CanWalkStraightTo (unknown)
    void probe(const Brain& owner, int k);

    std::array<Sector, kSectorCount> m_sectors{};
    float m_heading = 0.0F;          // +0x40
    std::uint64_t m_refreshedMs = 0; // +0x44
    bool m_built = false;
};

} // namespace coney::ai
