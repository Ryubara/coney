// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "animation/anim_math.h"

// A leader's formation: up to nine follow slots around the leader in four sets, and up to nine followers. Every so
// often (1 or 2 s, or when the leader stops or has moved a metre from the planned spot) the formation plans again:
// each slot's point is the leader's position plus its offset turned by his heading (2 m ahead while he runs), a slot
// is usable when the leader can walk to it, each usable slot takes its nearest unassigned follower, the rest queue
// behind the slotted ones, and pairs whose paths cross swap. GoalTrackHuman's followers walk to their slot's point.
// Research: docs/research/ai.md#formations

namespace coney::ai {

class Brain;

/// A formation's slot sets, slots per set and followers (`+0x000`, `+0x1b0`), and the pool of formations.
inline constexpr std::size_t kFormationSets = 4;
inline constexpr std::size_t kFormationSlots = 9;
inline constexpr std::size_t kFormationFollowers = 9;
inline constexpr std::size_t kFormationPool = 42;
/// A new formation's slot count and followers allowed in slots (`0x00294ad8`).
inline constexpr int kDefaultFollowSlots = 6;
/// Slot offsets and points are kept in sixteenths of a metre.
inline constexpr float kFormationUnitsPerMetre = 16.0F;
/// The plan's period: 1000 ms while the leader walks (gait 2), else 2000 ms; and the leader's distance from the
/// planned spot (m) and speed (m/s) that call for a plan at once.
inline constexpr std::uint64_t kPlanWalkingMs = 1000;
inline constexpr std::uint64_t kPlanOtherMs = 2000;
inline constexpr float kPlanMoveDistance = 1.0F;
inline constexpr float kStoppedSpeed = 0.005F;
/// While the leader runs the plan point is this far ahead of him, m.
inline constexpr float kPlanLeadDistance = 2.0F;
/// Passes of the crossing-paths swap.
inline constexpr int kSwapPasses = 3;

/// One follow slot of a set: its offset from the leader and its point in the world at the last plan, in 1/16 m.
struct FollowSlot {
    std::array<std::int16_t, 3> offset{};
    std::array<std::int16_t, 3> point{};
};

/// One follower: its brain, its slot (-1 none) and the human it queues behind (null for a slotted follower).
struct Follower {
    Brain* brain = nullptr;
    int slot = -1;
    Brain* behind = nullptr;
};

/// One leader's formation.
class Formation {
  public:
    /// The formation of `leader` (which must outlive it): kDefaultFollowSlots slots, as many allowed, set 0.
    /// @orig 0x00294ad8 Formation_Init (unknown)
    explicit Formation(Brain& leader);

    /// Its leader (`+0x244`).
    [[nodiscard]] Brain& leader() const { return *m_leader; }

    /// `BrSetNumFollowSlots(leader, count, allowed)`: the slots (`+0x271`) and how many followers may take one
    /// (`+0x272`; `count` when negative); plans again.
    /// @orig 0x00295dd8 Formation_SetSlotCount (unknown)
    void setSlotCount(int count, int allowed, std::uint64_t nowMs);
    /// `BrSetFollowSlotSet(leader, set)`: the set in use (`+0x270`); plans again.
    /// @orig 0x00292ac0 Follow_SelectSlotSet (unknown)
    void setSlotSet(int set, std::uint64_t nowMs);
    /// `BrSetFollowSlot(leader, slot, {x, y}, set)`: slot `slot` of set `set` sits at (x, y) m from the leader, x to
    /// his right and y ahead, kept in 1/16 m; plans again. Nothing for a slot or set out of range.
    /// @orig 0x00295ec8 Formation_SetSlot (unknown)
    void setSlot(int slot, float x, float y, int set, std::uint64_t nowMs);

    /// Writes slot `slot` of set `set` at (x, y) m without planning (`CfgSetDefaultFollowSlotSet`'s defaults); nothing
    /// for a slot or set out of range.
    void setDefaultSlot(int slot, float x, float y, int set);

    /// `follower` joins (a free follower entry, its brain's formation set). Returns false when it is full.
    /// @orig 0x00295f28 Formation_Join (unknown)
    bool join(Brain& follower);
    /// `follower` leaves.
    /// @orig 0x00296028 Formation_Leave (unknown)
    void leave(Brain& follower);

    /// One step at `nowMs`: plans when the leader has just stopped, at the plan time, or once he is kPlanMoveDistance
    /// from the planned spot; nothing without followers.
    /// @orig 0x002956d0 Formation_Plan (unknown)
    void update(std::uint64_t nowMs);
    /// Plans now: the next plan time (kPlanWalkingMs while the leader walks, else kPlanOtherMs), the plan point and
    /// heading, the slots' points and which are usable, then the assignment. **Coney choices**: a slot is usable when
    /// the leader's brain's planner finds the line to it walkable, or always without a planner (the original tests a
    /// line of sight, `0x002221e0`); the slot's point keeps the leader's height (no ground ray); the swap of crossing
    /// paths runs over the slotted followers.
    /// @orig 0x00294f38 Formation_PlaceSlots (unknown)
    /// @orig 0x002953c8 Formation_AssignSlots (unknown)
    void plan(std::uint64_t nowMs);

    /// `follower`'s slot point in the world, m; nothing when it has no slot.
    [[nodiscard]] std::optional<anim::Vec3> slotPoint(const Brain& follower) const;
    /// `follower`'s entry; null when it does not follow.
    [[nodiscard]] const Follower* follower(const Brain& brain) const;
    /// The followers, the set in use, the slot count and the followers allowed in slots.
    [[nodiscard]] const std::vector<Follower>& followers() const { return m_followers; }
    [[nodiscard]] int slotSet() const { return m_set; }
    [[nodiscard]] int slotCount() const { return m_slotCount; }
    [[nodiscard]] int allowed() const { return m_allowed; }
    /// Slot `slot` of set `set`; and whether it was usable at the last plan.
    [[nodiscard]] const FollowSlot& slot(int set, int slot) const;
    [[nodiscard]] bool usable(int set, int slot) const;
    /// When it plans next, ms.
    [[nodiscard]] std::uint64_t nextPlanMs() const { return m_nextPlanMs; }

  private:
    // Each usable slot, in order, takes its nearest unassigned follower; the rest queue nearest-first, round robin,
    // behind the slotted followers (or the leader); then up to kSwapPasses passes swap slotted pairs whose paths cross.
    void assign();

    Brain* m_leader;
    std::array<std::array<FollowSlot, kFormationSlots>, kFormationSets> m_sets{};
    std::array<std::array<bool, kFormationSlots>, kFormationSets> m_usable{};
    std::vector<Follower> m_followers;
    anim::Vec3 m_planPoint;                // +0x220
    float m_planHeading = 0.0F;            // +0x230
    float m_lastSpeed = 0.0F;              // +0x240
    std::uint64_t m_nextPlanMs = 0;        // +0x248
    bool m_stopped = false;                // +0x274
    int m_set = 0;                         // +0x270
    int m_slotCount = kDefaultFollowSlots; // +0x271
    int m_allowed = kDefaultFollowSlots;   // +0x272
};

/// The formations of a level (the pool at `0x006ceaf0`), one per leader, made on first use.
class Formations {
  public:
    /// `leader`'s formation; made when `make` and it has none (null when the pool is full).
    /// @orig 0x0021d428 Human_GetFormation (unknown)
    [[nodiscard]] Formation* of(Brain& leader, bool make);
    /// Every formation's step (`Formations_Update`).
    /// @orig 0x00293c68 Formations_Update (unknown)
    void update(std::uint64_t nowMs);
    /// `CfgSetDefaultFollowSlotSet`: set `set`'s slots, (x, y) m each, written into every formation of the pool, so
    /// those made later have them too.
    /// @orig 0x00294788 Cfg_SetDefaultFollowSlotSet (unknown)
    void setDefaults(int set, std::span<const std::pair<float, float>> slots);
    /// `brain` is going away: it leaves the formation it follows, and its own formation goes.
    void forget(Brain& brain);
    /// The formations in use.
    [[nodiscard]] std::size_t size() const { return m_formations.size(); }

  private:
    std::vector<std::unique_ptr<Formation>> m_formations;
    // The default slots of each set, (x, y) m, kept for the formations made later; empty until set.
    std::array<std::vector<std::pair<float, float>>, kFormationSets> m_defaults;
};

} // namespace coney::ai
