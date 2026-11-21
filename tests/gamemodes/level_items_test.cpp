// SPDX-License-Identifier: GPL-3.0-or-later
// The AI's view of a level's objects for the wreck command: which object types may be smashed (`Object_IsVandalisable`,
// docs/research/ai-scripting.md, the riot's smash; docs/research/crew-wreck.md). Synthetic
// types only.
#include "gamemodes/level_items.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gamemodes/level_pickups.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"
#include "world_objects/object_types.h"
#include "world_objects/spawn_records.h"

using coney::LevelItems;
using coney::anim::Vec3;
using coney::world_objects::ObjectType;
namespace wo = coney::world_objects;

namespace {

// A type of `className` with `animSet`, `kind` and `value`.
ObjectType typeOf(const char* className, int animSet, int kind = 0, int value = 0) {
    ObjectType type;
    type.className = className;
    type.animSet = animSet;
    type.objectKind = kind;
    type.value = value;
    return type;
}

} // namespace

TEST_CASE("the wreck command may smash carried objects and the classes strikes break, never doors or explosives",
          "[gamemodes][wreck]") {
    // Carried (anim set 4) and thrown (5) objects worth 1 or more, whatever their class.
    CHECK(LevelItems::smashable(typeOf("simple_object", 4, 0, 1)));
    CHECK(LevelItems::smashable(typeOf("simple_object", 5, 0, 1)));
    // The classes a strike breaks (Coney's stand-in for the type record's +0x40 & 0x30), worth 1 or more.
    CHECK(LevelItems::smashable(typeOf("overhead_weapon", 0, 0, 1)));
    CHECK(LevelItems::smashable(typeOf("dyn_cashreg", 0, 0, 1)));
    CHECK(LevelItems::smashable(typeOf("dyn_masks", 0, 0, 1)));
    // The kinds 30, 40 and 42, never.
    CHECK_FALSE(LevelItems::smashable(typeOf("simple_object", 4, 30, 1)));
    CHECK_FALSE(LevelItems::smashable(typeOf("simple_object", 4, 40, 1)));
    CHECK_FALSE(LevelItems::smashable(typeOf("simple_object", 4, 42, 1)));
    // Anything else: a plain object nobody carries.
    CHECK_FALSE(LevelItems::smashable(typeOf("simple_object", 0, 0, 1)));
}

TEST_CASE("a type worth under 1 may be smashed only when it is of class dyn_masks", "[gamemodes][wreck]") {
    // Object_IsVandalisable 0x00394f30: byte +0x5a under 1 refuses every class but dyn_masks.
    CHECK(LevelItems::smashable(typeOf("dyn_masks", 0, 0, 0)));
    CHECK_FALSE(LevelItems::smashable(typeOf("simple_object", 4, 0, 0)));
    CHECK_FALSE(LevelItems::smashable(typeOf("simple_object", 5, 0, 0)));
    CHECK_FALSE(LevelItems::smashable(typeOf("overhead_weapon", 0, 0, 0)));
    CHECK_FALSE(LevelItems::smashable(typeOf("dyn_cashreg", 0, 0, 0)));
}

// Expected: docs/research/objects.md#object-types, confirmed (code) (`0x002bf118`, the type's anim set and word bit 4)
TEST_CASE("a smash target carries its type's anim set and whether a held breaking object strikes it",
          "[gamemodes][wreck]") {
    // TryVandalise (0x002bf118) reads the type's anim set (+0x87) and its word +0x5e bit 4.
    ObjectType type = typeOf("simple_object", 4, 0, 1);
    type.bodyWord = 4;
    const coney::ai::SmashTarget target = LevelItems::smashTargetOf(7.0, {1.0F, 2.0F, 0.0F}, type);
    CHECK(target.handle == 7.0);
    CHECK(target.position.y == 2.0F);
    CHECK(target.animSet == 4);
    CHECK(target.heldBreaks);
    type.bodyWord = 0;
    CHECK_FALSE(LevelItems::smashTargetOf(7.0, {1.0F, 2.0F, 0.0F}, type).heldBreaks);
}

namespace {

// The zone the wreck tests use.
constexpr int kZone = 51;

// A type named `name`, of class "simple_object", with `animSet`, the word +0x5e `bodyWord` and `value`.
ObjectType wreckType(const char* name, int animSet, std::uint16_t bodyWord, int value) {
    ObjectType type = typeOf("simple_object", animSet, 0, value);
    type.name = name;
    type.bodyWord = bodyWord;
    return type;
}

// The level's items over synthetic records and types, in an enabled zone kZone: a carried bag, a thrown bottle, an
// object of word bit 0x04 and one of bit 0x20, a plain one nobody carries, a carried one worth nothing and a mask worth
// nothing. The zone queries are asked of it, so a LevelItems that answers as the ItemServices defaults do (no
// target, nothing left, no view) fails every test below that expects one.
struct Wreck {
    coney::GameState state;
    coney::script::ScriptSystem scripts{[](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                                            return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
                                        },
                                        {},
                                        {}};
    wo::SpawnRecords records;
    wo::ObjectTypes types;
    wo::WorldFlags flags;
    coney::LevelPickups pickups{scripts, state, records, types};
    LevelItems items{flags, pickups, records, types};

    Wreck() {
        types.add(wreckType("bag", 4, 0, 1));
        types.add(wreckType("bottle", 5, 0, 1));
        types.add(wreckType("struck", 0, 0x04, 1));
        types.add(wreckType("bit20", 0, 0x20, 1));
        types.add(wreckType("plain", 0, 0, 1));
        types.add(wreckType("empty", 4, 0, 0));
        ObjectType mask = wreckType("mask", 4, 0, 0);
        mask.className = "dyn_masks";
        types.add(mask);
        records.setZoneEnabled(kZone, true);
    }

    // A record of `type` with `handle` at (x, 0, 0) in `zone`.
    wo::SpawnRecord* place(double handle, const char* type, float x, std::uint32_t zone = kZone) {
        wo::SpawnRecord record;
        record.handle = handle;
        record.typeName = type;
        record.position = {x, 0.0F, 0.0F};
        record.zone = zone;
        return records.add(record);
    }
};

constexpr Vec3 kOrigin{0.0F, 0.0F, 0.0F};

} // namespace

// Expected: docs/research/ai-goals.md#goal-destroy, confirmed (code) (`0x0039a2c0`, the carried or wreck-bit types)
TEST_CASE("the zone's nearest wreck target is the nearest record of a carried type or one with the wreck bits",
          "[gamemodes][wreck]") {
    // ObjZone_FindNearestObject 0x0039a2c0, mode 0x20: anim set 4 or 5, or word bits 0x24, worth something or a mask.
    Wreck w;
    w.place(1, "plain", 1.0F);   // not carried, no wreck bits: never a target
    w.place(2, "empty", 2.0F);   // carried but worth nothing
    w.place(3, "bit20", 5.0F);   // the wreck bit 0x20
    w.place(4, "bag", 9.0F);     // carried
    w.place(5, "struck", 7.0F);  // the wreck bit 0x04
    w.place(6, "bottle", 12.0F); // thrown
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 0) == std::optional<double>(3.0));
    // The nearest is measured from where he stands.
    CHECK(w.items.nearestZoneTarget(kZone, Vec3{20.0F, 0.0F, 0.0F}, 0) == std::optional<double>(6.0));
    // A mask is a target worth nothing.
    w.place(7, "mask", 0.5F);
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 0) == std::optional<double>(7.0));
}

// Expected: docs/research/ai-goals.md#goal-destroy, confirmed (code) (`0x0039a2c0`, what the search skips)
TEST_CASE("the zone's nearest wreck target skips objects in a hand, in flight, claimed or removed and other zones",
          "[gamemodes][wreck]") {
    Wreck w;
    w.place(1, "bag", 1.0F);
    w.place(2, "bag", 2.0F);
    w.place(3, "bag", 3.0F);
    w.place(4, "bag", 4.0F);
    w.place(5, "bag", 5.0F);
    w.place(6, "bag", 0.1F, 52); // another zone, nearer
    w.place(7, "bag", 6.0F);
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 0) == std::optional<double>(1.0));
    // In a hand.
    REQUIRE(w.pickups.placeInHand(1, 77.0));
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 0) == std::optional<double>(2.0));
    // In flight.
    w.pickups.setAirborne([](double object) { return object == 2.0; });
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 0) == std::optional<double>(3.0));
    // Claimed by any gang until 5 s; free again at 5 s.
    w.items.claim(3, 1, 5000);
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 4999) == std::optional<double>(4.0));
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 5000) == std::optional<double>(3.0));
    // Removed (broken and gone).
    REQUIRE(w.records.destroy(3));
    CHECK(w.items.nearestZoneTarget(kZone, kOrigin, 5000) == std::optional<double>(4.0));
    for (const double handle : {4.0, 5.0, 7.0}) {
        REQUIRE(w.records.destroy(handle));
    }
    CHECK_FALSE(w.items.nearestZoneTarget(kZone, kOrigin, 5000).has_value());
}

// Expected: docs/research/ai-goals.md#goal-destroy, confirmed (code) (`0x0039a2c0`, `0x0039a580`: a disabled zone)
TEST_CASE("a disabled or negative zone has no wreck target and nothing left to wreck", "[gamemodes][wreck]") {
    Wreck w;
    w.place(1, "bag", 1.0F, 60);
    CHECK_FALSE(w.items.nearestZoneTarget(60, kOrigin, 0).has_value());
    CHECK_FALSE(w.items.zoneHasObjects(60));
    CHECK_FALSE(w.items.nearestZoneTarget(-1, kOrigin, 0).has_value());
    CHECK_FALSE(w.items.zoneHasObjects(-1));
    w.records.setZoneEnabled(60, true);
    CHECK(w.items.nearestZoneTarget(60, kOrigin, 0) == std::optional<double>(1.0));
    CHECK(w.items.zoneHasObjects(60));
}

// Expected: docs/research/ai-goals.md#goal-destroy, confirmed (code) (`0x0039a580`, the zone check)
TEST_CASE("a zone has objects while one is whole, in no hand and not removed", "[gamemodes][wreck]") {
    // ObjZone_HasObjects 0x0039a580, mode 1.
    Wreck w;
    CHECK_FALSE(w.items.zoneHasObjects(kZone));
    w.place(1, "bag", 1.0F);
    w.place(2, "plain", 2.0F); // any type counts, wrecked or not
    CHECK(w.items.zoneHasObjects(kZone));
    // An object in a hand does not count.
    REQUIRE(w.pickups.placeInHand(1, 77.0));
    CHECK(w.items.zoneHasObjects(kZone));
    REQUIRE(w.pickups.placeInHand(2, 77.0));
    CHECK_FALSE(w.items.zoneHasObjects(kZone));
    // Back in the open it counts again; removed it does not.
    w.pickups.drop(1, Vec3{1.0F, 0.0F, 0.0F});
    CHECK(w.items.zoneHasObjects(kZone));
    REQUIRE(w.records.destroy(1));
    CHECK_FALSE(w.items.zoneHasObjects(kZone));
}

// Expected: docs/research/objects.md#object-types, inferred from the type record's anim set and word
TEST_CASE("the wreck's view of an object is its type's anim set and strike flag, and nothing once it is gone",
          "[gamemodes][wreck]") {
    Wreck w;
    w.place(1, "bag", 3.0F);
    w.place(2, "struck", 4.0F);
    w.place(3, "unknown", 5.0F);
    const std::optional<coney::ai::SmashTarget> bag = w.items.wreckView(1);
    REQUIRE(bag.has_value());
    const coney::ai::SmashTarget view = bag.value_or(coney::ai::SmashTarget{});
    CHECK(view.handle == 1.0);
    CHECK(view.animSet == 4);
    CHECK(view.position.x == 3.0F);
    CHECK_FALSE(view.heldBreaks);
    const std::optional<coney::ai::SmashTarget> struck = w.items.wreckView(2);
    REQUIRE(struck.has_value());
    CHECK(struck.value_or(coney::ai::SmashTarget{}).heldBreaks);
    // A record of no known type, an unknown handle and a removed record have no view.
    CHECK_FALSE(w.items.wreckView(3).has_value());
    CHECK_FALSE(w.items.wreckView(99).has_value());
    REQUIRE(w.records.destroy(1));
    CHECK_FALSE(w.items.wreckView(1).has_value());
}
