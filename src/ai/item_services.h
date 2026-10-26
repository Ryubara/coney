// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

#include "animation/anim_math.h"

// What the AI asks of the level's loose objects, glass and store flags when a Warrior fetches or smashes something:
// the store fronts (flags of activity 14), the owned objects of an object zone, the world objects a Warrior may
// smash, the glass panes, where an object is, who holds it and whether it is broken, the gangs' claims on objects, and
// the start of a pick-up. The play's level objects implement it (gamemodes/level_items.h); the steal command's tactic
// and goals use it (ai/steal_goals.h, ai/smash_goals.h). Research: docs/research/ai-code.md#t2-warrior-steal,
// docs/research/ai.md#get-item, docs/research/crew-wreck.md, docs/research/objects.md#pane,
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

/// A glass pane as the AI weighs it for smashing (docs/research/objects.md#pane).
struct PaneView {
    double handle = 0;
    anim::Vec3 centre{};  ///< `+0x10`.
    anim::Vec3 normal{};  ///< `+0x80`: the unit normal, its local y axis.
    float height = 0.0F;  ///< `+0xd4`, metres.
    bool alarmed = false; ///< Both alarm bits (`+0xf8`) set.
};

/// What a held object lets the wreck order do with it (`WarriorVandalStealGoal_TryVandalise`,
/// docs/research/crew-wreck.md).
enum class HeldUse : std::uint8_t {
    None,   ///< Nothing in hand.
    Breaks, ///< A breaking object (object flag `0x10000`): it may strike a smash target where it stands.
    Swings, ///< A throwable, or an object of kind 4 or 6: he swings it.
    Other,  ///< Anything else: no wreck while he holds it.
};

/// A world object a Warrior may smash: one that passes `Object_IsVandalisable` and is not broken
/// (docs/research/ai-scripting.md, the riot's smash; docs/research/crew-wreck.md).
struct SmashTarget {
    double handle = 0;
    anim::Vec3 position{};
    int animSet = 0; ///< Its type's anim set (`+0x87`): 4 and 5 are carried and thrown, not struck where it stands.
    bool heldBreaks = false; ///< Its type's word `+0x5e` has bit 4: a held breaking object strikes it where it stands.
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
    /// The world objects within `radius` of `centre` that a Warrior may smash (SmashTarget), in no hand and not in
    /// flight, nearest first (`ObjectManager_FindObjects`, at most 384). None by default.
    /// @orig 0x00394f30 Object_IsVandalisable (unknown)
    [[nodiscard]] virtual std::vector<SmashTarget> smashTargets(anim::Vec3 /*centre*/, float /*radius*/) const {
        return {};
    }
    /// The glass panes whose centre is within `radius` of `centre` that are whole and have their body (vtable
    /// `+0xf4`), nearest first (`GlassTaskManager_ListInRange`, at most 64). None by default.
    /// @coney-only the AI's view of the level's glass; the original asks the glass task manager directly
    [[nodiscard]] virtual std::vector<PaneView> panes(anim::Vec3 /*centre*/, float /*radius*/) const { return {}; }
    /// Whether object `handle` is in flight, thrown or falling (its flag `0x4000000`): GetItem waits for it to land.
    /// False by default.
    [[nodiscard]] virtual bool inFlight(double /*handle*/) const { return false; }
    /// What the held object `handle` lets the wreck order do (HeldUse). HeldUse::Other by default.
    [[nodiscard]] virtual HeldUse heldUse(double /*handle*/) const { return HeldUse::Other; }
    /// Whether object or pane `handle` is broken (a pane's flag `0x800000`; a world object's hit counters `+0x10d` or
    /// `+0x10e` run down) or gone. False by default.
    [[nodiscard]] virtual bool broken(double /*handle*/) const { return false; }
    /// Where object or pane `handle` is (a pane's centre); nothing once it is gone. itemPosition() by default.
    [[nodiscard]] virtual std::optional<anim::Vec3> objectPosition(double handle) const { return itemPosition(handle); }

    /// Claims object or pane `handle` for `gang` until `untilMs` (object `+0xec` the gang, `+0xf0` the time).
    /// @coney-only the claims kept beside the objects, where the original writes them into the object
    void claim(double handle, int gang, std::uint64_t untilMs) { m_claims[handle] = Claim{gang, untilMs}; }
    /// Whether `handle` is claimed by `gang` at `nowMs`.
    /// @coney-only the claims kept beside the objects, where the original reads them from the object
    [[nodiscard]] bool claimedBy(double handle, int gang, std::uint64_t nowMs) const {
        const auto found = m_claims.find(handle);
        return found != m_claims.end() && found->second.gang == gang && nowMs < found->second.untilMs;
    }
    /// Drops the claim on `handle` (`+0xec` cleared).
    void unclaim(double handle) { m_claims.erase(handle); }

    /// Starts `brain`'s human picking up object `handle` (his pick-up clip for its height); the take follows at the
    /// clip's event. False, doing nothing, when it cannot start (the object gone, no clip).
    virtual bool startPickUp(Brain& brain, double handle) = 0;

  private:
    // A gang's claim on an object (`+0xec`, `+0xf0`).
    struct Claim {
        int gang = 0;
        std::uint64_t untilMs = 0;
    };
    std::map<double, Claim> m_claims;
};

} // namespace coney::ai
