// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

#include "animation/anim_math.h"

// What the AI asks of the level's loose objects and store flags when a Warrior fetches something: the store fronts
// (flags of activity 14), the owned objects of an object zone, where an object is and who holds it, and the start of a
// pick-up. The play's level objects implement it (gamemodes/level_items.h); the steal command's tactic and goals use
// it (ai/steal_goals.h). Research: docs/research/ai-code.md#t2-warrior-steal, docs/research/ai.md#get-item,
// docs/research/crimes.md#owned-items, docs/research/flags.md#activities

namespace coney::ai {

class Brain;

/// The flag activity of a store front (`storeFront`, `storeJewelry`; docs/research/flags.md#activities).
inline constexpr int kStoreFlagActivity = 14;

/// A store front's flag: its handle, where it is, and its object zone (the low byte of its group, `+0xd8`).
struct StoreFlag {
    double handle = 0;
    anim::Vec3 position{};
    int zone = 0;
};

/// A loose object the AI may fetch: its handle and where it is.
struct LooseItem {
    double handle = 0;
    anim::Vec3 position{};
};

/// The level's side of the AI's fetching. It must outlive the tactics and goals given it.
class ItemServices {
  public:
    ItemServices() = default;
    ItemServices(const ItemServices&) = delete;
    ItemServices& operator=(const ItemServices&) = delete;
    ItemServices(ItemServices&&) = delete;
    ItemServices& operator=(ItemServices&&) = delete;
    virtual ~ItemServices() = default;

    /// Every enabled store-front flag (activity kStoreFlagActivity).
    [[nodiscard]] virtual std::vector<StoreFlag> stores() const = 0;
    /// The objects of zone `zone` still there and owned (`+0x109`), at most 64 (`ObjZone_ListObjects` given 64), in
    /// the zone's order.
    /// @orig 0x0039aa40 ObjZone_ListObjects (unknown)
    [[nodiscard]] virtual std::vector<LooseItem> ownedItems(int zone) const = 0;
    /// Where object `handle` is; nothing once it is gone (taken for good, destroyed).
    [[nodiscard]] virtual std::optional<anim::Vec3> itemPosition(double handle) const = 0;
    /// The handle of the human holding object `handle`; nothing when nobody does.
    [[nodiscard]] virtual std::optional<double> holder(double handle) const = 0;
    /// Starts `brain`'s human picking up object `handle` (his pick-up clip for its height); the take follows at the
    /// clip's event. False, doing nothing, when it cannot start (the object gone, no clip).
    virtual bool startPickUp(Brain& brain, double handle) = 0;
};

} // namespace coney::ai
