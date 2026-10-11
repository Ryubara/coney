// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <vector>

#include "ai/item_services.h"
#include "gamemodes/level_pickups.h"
#include "world_objects/flags.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

// The level's loose objects and store fronts as the AI fetches them (ai::ItemServices): the store-front flags, a
// zone's owned objects, where an object is and who holds it, an AI human's pick-up and, after the characters' step,
// the takes their pick-ups reached. Research: docs/research/ai-code.md#t2-warrior-steal, docs/research/ai.md#get-item,
// docs/research/crimes.md#owned-items

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

    /// Every AI human of `brains` whose pick-up reached its clip's event since the last call takes the object
    /// (LevelPickups::takeByAi()); one that goes into his hand is his held object. Player brains are left to the play
    /// mode, which takes their pick-ups itself.
    void collectTakes(ai::Brains& brains);

  private:
    const world_objects::WorldFlags& m_flags;
    LevelPickups& m_pickups;
    world_objects::SpawnRecords& m_records;
    const world_objects::ObjectTypes& m_types;
};

} // namespace coney
