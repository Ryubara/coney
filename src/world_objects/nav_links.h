// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "animation/anim_math.h"

namespace coney::world {
class PathMap;
}

namespace coney::world_objects {

/// Navigation link kinds (a D record's low 16 bits) the doors and panes read and write
/// (docs/research/objects.md#nav-links).
namespace link_kind {
inline constexpr std::uint16_t kChoke = 0x4;      ///< The choke point: a window link's link before it is retagged.
inline constexpr std::uint16_t kDoor = 0x10;      ///< Walked or jumped; `ConvertJumpToDoor` makes these from kChoke.
inline constexpr std::uint16_t kBreakable = 0x40; ///< Charged through: a breakable door's or a window's link.
} // namespace link_kind

/// How far `NavLink_FindNearest` looks for a link, metres.
inline constexpr float kNearestLinkReach = 5.0F;

/// A link found near a point, and the link back the other way when there is one.
struct FoundLink {
    std::uint32_t link = 0;
    std::optional<std::uint32_t> back;
};

/// The doors' and panes' changes to the level's navigation links: retagging a door number's links, their avoid bit
/// (the route planner adds 1,600 to an avoided link's cost, so a closed door makes a detour preferred, not mandatory)
/// and the path polygon flag 8 that an open door puts on its doorway's hole. A null map changes nothing and finds
/// nothing.
///
/// **Coney's choice** where the page is open: a link's distance from a point is the distance to the middle of the
/// segment between its two nodes.
///
/// Research: docs/research/objects.md#nav-links, docs/research/objects.md#door-numbers
class NavLinks {
  public:
    /// Links over `map` (which may be null), which must outlive this.
    explicit NavLinks(world::PathMap* map);

    /// Retags every link carrying door `number` with `kind`.
    /// @orig 0x00250c00 NavLinks_SetKindByNumber (unknown)
    void setKindByNumber(std::uint16_t number, std::uint16_t kind);
    /// Opens door `number`'s links: clears their avoid bit and sets flag 8 on its doorway's hole (holeOf()).
    /// @orig 0x00250c50 NavLinks_OpenByNumber (unknown)
    void openByNumber(std::uint16_t number);
    /// Closes door `number`'s links: sets their avoid bit and clears flag 8 on its doorway's hole.
    /// @orig 0x00250d00 NavLinks_CloseByNumber (unknown)
    void closeByNumber(std::uint16_t number);

    /// The link nearest `at` whose kind shares a bit with `kinds`, within `reach`, and its reverse; nothing when none
    /// is that near.
    /// @orig 0x00250960 NavLink_FindNearest (unknown)
    [[nodiscard]] std::optional<FoundLink> findNearest(anim::Vec3 at, std::uint16_t kinds,
                                                       float reach = kNearestLinkReach) const;

    /// Sets or clears one link's avoid bit.
    void setAvoid(std::uint32_t link, bool avoid);
    /// Retags one link.
    void setKind(std::uint32_t link, std::uint16_t kind);
    /// Sets or clears one polygon's flag 8 (world::kPathPolygonExcluded).
    void setPolygonExcluded(std::uint32_t polygon, bool excluded);
    /// Door `number`'s doorway hole: from its last link (in link order), the node it leads to and that node's last
    /// door or breakable link, the hole at the middle of those two nodes (world::PathMap::holeAt()); nothing when the
    /// number has no link, the node no such link, or no hole is there.
    /// @orig 0x002508b8 NavLink_DoorPolygon (unknown)
    [[nodiscard]] std::optional<std::uint32_t> holeOf(std::uint16_t number) const;
    /// The polygon that takes in `at` in plan; nothing when none does.
    [[nodiscard]] std::optional<std::uint32_t> polygonAt(anim::Vec3 at) const;

    /// `DisableDoorLink(pos)`: the nearest kDoor link within 5 m and its reverse get the avoid bit, as a closed door's.
    /// **Coney's reading** of "the same by position": disable closes, enable opens.
    void disableNear(anim::Vec3 at);
    /// `EnableDoorLink(pos)`: the nearest kDoor link within 5 m and its reverse lose the avoid bit, as an open door's.
    void enableNear(anim::Vec3 at);
    /// `ConvertJumpToDoor(pos)`: the nearest kChoke link within 5 m and its reverse become kDoor links.
    void convertJumpToDoor(anim::Vec3 at);

  private:
    // Sets the avoid bit of every link of `number` to the opposite of `open`, and its hole's flag 8 to `open`.
    void setNumberOpen(std::uint16_t number, bool open);
    // Changes the link nearest `at` of `kinds`, and its reverse, with `change`.
    template <typename Change> void changeNearest(anim::Vec3 at, std::uint16_t kinds, Change change);

    world::PathMap* m_map;
    std::vector<std::uint32_t> m_fromNode; // each link's starting node
};

} // namespace coney::world_objects
