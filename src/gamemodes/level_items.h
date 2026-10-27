// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

#include "ai/item_services.h"
#include "gamemodes/level_pickups.h"
#include "world_objects/flags.h"
#include "world_objects/level_objects.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

// The level's loose objects and store fronts as the AI fetches them (ai::ItemServices): the store-front flags, a
// zone's owned objects, where an object is and who holds it, an AI human's pick-up and, after the characters' step,
// the takes their pick-ups reached. Research: docs/research/ai-code.md#t2-warrior-steal,
// docs/research/ai/index.md#get-item, docs/research/crimes.md#owned-items

namespace coney {

namespace ai {
class Brains;
} // namespace ai

/// The AI's view of a level's loose objects, over its flags, its pick-ups (and through them its spawn records and
/// object types), all of which must outlive it.
class LevelItems final : public ai::ItemServices {
  public:
    /// The most objects a zone lists (`ObjZone_ListObjects` given 64).
    static constexpr std::size_t kZoneListMost = 64;

    LevelItems(const world_objects::WorldFlags& flags, LevelPickups& pickups, world_objects::SpawnRecords& records,
               const world_objects::ObjectTypes& types)
        : m_flags(flags), m_pickups(pickups), m_records(records), m_types(types) {}

    /// The enabled flags of activity ai::kStoreFlagActivity, each with the low byte of its group as its zone.
    [[nodiscard]] std::vector<ai::StoreFlag> stores() const override;
    /// The records of zone `zone` that are owned, not removed, not hidden and in no hand, while the zone is enabled;
    /// at most kZoneListMost, in the records' order.
    [[nodiscard]] std::vector<ai::LooseItem> ownedItems(int zone) const override;
    /// The record's place; nothing once it is removed.
    [[nodiscard]] std::optional<anim::Vec3> itemPosition(double handle) const override;
    /// Who holds it (LevelPickups::holderOf()).
    [[nodiscard]] std::optional<double> holder(double handle) const override;
    /// The human's pick-up (human::Human::startPickUp()) with the clip for the object's type and height above his
    /// feet (world_objects::pickupClip()). **Coney's reading**: the AI's pick-up action (`PickUpItemAction`, `0x21`)
    /// is not on the page, so it plays the player's pick-up.
    bool startPickUp(ai::Brain& brain, double handle) override;

    /// The level's panes and struck props (null: no panes, and no prop is broken), which must outlive it.
    void setObjects(const world_objects::LevelObjects* objects) { m_objects = objects; }
    /// The live records within `radius` of `centre` that are not removed, hidden, in a hand or in flight
    /// (LevelPickups::airborne(), the object flag bit 26 the search skips), not broken, of a type that may be smashed
    /// (smashable()), nearest first, at most kSmashListMost (smashTargetOf()).
    /// @orig 0x0029d5f0 Ai_FindObject (unknown)
    [[nodiscard]] std::vector<ai::SmashTarget> smashTargets(anim::Vec3 centre, float radius) const override;
    /// The panes within `radius` of `centre` that are neither broken nor hidden, nearest first, at most
    /// kPaneListMost. **Coney's reading**: "has its body" (vtable `+0xf4`) is taken as not hidden.
    /// @orig 0x0029d928 Glass_ClaimPane (unknown)
    [[nodiscard]] std::vector<ai::PaneView> panes(anim::Vec3 centre, float radius) const override;
    /// A pane's broken flag; a record removed, or a struck prop with a hit counter run down.
    /// @coney-only the level's side of ItemServices: reads Coney's pane and prop state
    [[nodiscard]] bool broken(double handle) const override;
    /// Whether object `handle` is in flight (LevelPickups::airborne(), the object flag bit 26).
    /// @coney-only the level's side of ItemServices: reads the play mode's flying objects
    [[nodiscard]] bool inFlight(double handle) const override;
    /// A pane's centre, else itemPosition().
    /// @coney-only the level's side of ItemServices: a position lookup
    [[nodiscard]] std::optional<anim::Vec3> objectPosition(double handle) const override;
    /// HeldUse::Swings for a type of anim set 4, 5 or 6 or a knife; HeldUse::Other otherwise. **Coney stand-in**:
    /// the breaking flag (`0x10000`) is not read, so no held object counts as HeldUse::Breaks.
    /// @coney-only stand-in: object classes for the breaking flag the original reads
    [[nodiscard]] ai::HeldUse heldUse(double handle) const override;

    /// The most smash targets and panes a search lists (`ObjectManager_FindObjects` 384,
    /// `GlassTaskManager_ListInRange` 64).
    static constexpr std::size_t kSmashListMost = 384;
    static constexpr std::size_t kPaneListMost = 64;

    /// `Object_IsVandalisable` on a type: worth 1 or more (`+0x5a`) unless of class `dyn_masks`, not of kind 30
    /// (`TYPE_BREAKANDENTER_DOOR`), 40 (`TYPE_EXPLOSIVE`) or 42 (`TYPE_FIREBARREL`), and carried (anim set 4 or 5) or
    /// of a class that strikes break. **Coney stand-in**: the original's own test, its record's `+0x40` & `0x30`, is
    /// not read; the classes strikes break (`dyn_masks`, `overhead_weapon`, `dyn_cashreg`) stand for it. The excluded
    /// vandalise zone (an object whose zone `+0x114` is `0x005148bc`, set by `CfgExcludedVandalizeZone`, which
    /// level55's `Main` calls) is not ported (issue #960).
    /// @orig 0x00394f30 Object_IsVandalisable (unknown)
    [[nodiscard]] static bool smashable(const world_objects::ObjectType& type);
    /// The smash target object `handle` at `at` of `type` makes: its anim set (`+0x87`) and whether a held breaking
    /// object strikes it (word `+0x5e` bit 4), the two fields `WarriorVandalStealGoal_TryVandalise` reads.
    /// @coney-only the AI's view of a level object type
    [[nodiscard]] static ai::SmashTarget smashTargetOf(double handle, anim::Vec3 at,
                                                       const world_objects::ObjectType& type);

    /// Every AI human of `brains` whose pick-up reached its clip's event since the last call takes the object
    /// (LevelPickups::takeByAi()); one that goes into his hand is his held object. Player brains are left to the play
    /// mode, which takes their pick-ups itself.
    void collectTakes(ai::Brains& brains);

  private:
    const world_objects::WorldFlags& m_flags;
    LevelPickups& m_pickups;
    world_objects::SpawnRecords& m_records;
    const world_objects::ObjectTypes& m_types;
    const world_objects::LevelObjects* m_objects = nullptr;
};

} // namespace coney
