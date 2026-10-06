// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coney::world_objects {

/// One spawn record: what `ObjSpawn` leaves for an object to be made from later. Coney keeps the record's meaning,
/// not its 0x28-byte layout.
struct SpawnRecord {
    double handle = 0;                         ///< From the world objects' handle space (Coney's choice, below).
    std::string typeName;                      ///< The object type (`CfgObj`) the object is made as.
    std::array<float, 3> position{};           ///< `+0x08`.
    std::array<float, 4> rotation{0, 0, 0, 1}; ///< `+0x00`: the quaternion {x, y, z, w}.
    std::uint32_t tint = 0xFFFFFFFFU;          ///< `+0x14`: the tint word `0xRRGGBBAA`; white is no tint.
    std::uint32_t zone = 0;                    ///< `+0x20`'s top bits: the object zone, 0 for none.
    std::uint32_t flags = 0;                   ///< `ObjSpawn`'s flags (bits 1 and 64 go to `+0x24`).
    std::string flagName{};                    ///< `+0x18`: the flag (marker) it is linked to; empty for none.
    bool live = false;                         ///< `+0x24` bit `0x20000`: its object exists.
    bool pinned = false;                       ///< `+0x24` bit `0x10000`: never stored (a scene holds it).
    bool removed = false;                      ///< `+0x24` bit `0x40000`: gone for good, never spawned again.
    bool hidden = false;                       ///< Its object is hidden (`ObjHide`) until shown again (`ObjShow`).
    float fadeInDistance = 0.0F;               ///< `+0x138`: what `ObjShow` was given (inferred: a fade-in distance).
    std::uint32_t money = 0;                   ///< A `dyn_money` pickup's dollars (its object's `+0x124`).
};

/// The `ObjectTaskManager`'s spawn records: `ObjSpawn` adds one and returns its handle; resolving the handle (a binding
/// that takes it, such as `SceneAddObject`) makes the object when it is not live.
///
/// **Coney's choices:** a record's handle comes from the counter every world object's handle comes from, not the
/// original's record index << 16 (record 0 would be 0, Coney's `NilHandle`); the rotation is kept as given, not packed
/// into four s16 × 4,096; and before `CfgSetDatabaseSizes` makes the pool, the records are not limited. Coney has no
/// object tasks yet: "live" marks a record whose object a consumer (the front end's world scene) draws.
///
/// Research: docs/research/objects.md#spawn-records, docs/research/objects.md#spawning
class SpawnRecords {
  public:
    /// What the pool holds beyond `CfgSetDatabaseSizes`' object count.
    static constexpr std::size_t kPoolExtra = 500;

    /// `CfgSetDatabaseSizes(objects, ...)`: room for `objects` + kPoolExtra records; forgets any there were.
    void createPool(std::size_t objects);

    /// Adds `record` (its handle set by the caller) and returns it; null when the pool is full.
    /// @orig 0x00398940 ObjRecord_Add (unknown)
    SpawnRecord* add(SpawnRecord record);

    /// The record named by `handle`; null when none is.
    [[nodiscard]] SpawnRecord* find(double handle);
    [[nodiscard]] const SpawnRecord* find(double handle) const;

    /// Resolves `handle`: its record, made live when it was not (a removed record stays gone and gives null).
    /// @orig 0x00398fe0 ObjRecord_GetHandle (unknown)
    SpawnRecord* resolve(double handle);

    /// Pins or unpins the record of `handle`, so streaming never stores its object; an unknown handle is ignored.
    /// @orig 0x00398df8 ObjRecord_SetPinned (unknown)
    void setPinned(double handle, bool pinned);

    /// `ObjDestroy(handle)`: the record's object is gone for good (removed, not live, never spawned again). Returns
    /// whether the handle named a record that was not removed already.
    /// @orig 0x00396c58 Obj_Destroy (unknown)
    bool destroy(double handle);

    /// Zones the mask holds: the zone is the top 10 bits of a record's `+0x20`.
    static constexpr std::size_t kZones = 1024;
    /// `ObjEnableZone(zone, enable)`: sets or clears the zone's bit of the mask (manager `+0x20`), so streaming spawns
    /// or stores that zone's objects. A zone outside the mask is ignored (**Coney choice**: the original does not
    /// check).
    /// @orig 0x00396778 ObjZone_Enable (unknown)
    /// @orig 0x00398348 ObjZoneMask_Set (unknown)
    void setZoneEnabled(std::uint32_t zone, bool enabled);
    /// Whether the zone's objects are wanted: zone 0 is on at a level's start, every other off.
    [[nodiscard]] bool zoneEnabled(std::uint32_t zone) const { return zone < kZones && m_zones.test(zone); }

    /// Every record, oldest first.
    [[nodiscard]] const std::vector<SpawnRecord>& all() const { return m_records; }
    /// Forgets every record and puts the zone mask back to zone 0 alone: the level is unloaded.
    void clear() {
        m_records.clear();
        m_zones.reset();
        m_zones.set(0);
    }

  private:
    std::vector<SpawnRecord> m_records;
    std::bitset<kZones> m_zones{1};        // zone 0 on, 1-254 off at a level's start (0x00397a48)
    std::optional<std::size_t> m_capacity; // none until CfgSetDatabaseSizes
};

/// Whether `ObjSpawn` makes a record for `typeName`: a key pickup (`dyn_key...`) needs unlockable 13 and the power
/// cuffs (`dyn_powercuffs`) unlockable 14 outside levels numbered 100 or more. **Coney's choice:** Coney's
/// unlockables do not hold those two yet (the kind they are asked as is not traced), so both are always suppressed,
/// as a fresh profile has neither.
/// @orig 0x00396858 Obj_Spawn (unknown)
[[nodiscard]] bool spawnAllowed(std::string_view typeName);

} // namespace coney::world_objects
