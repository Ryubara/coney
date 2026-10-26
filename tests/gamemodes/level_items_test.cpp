// SPDX-License-Identifier: GPL-3.0-or-later
// The AI's view of a level's objects for the wreck command: which object types may be smashed (`Object_IsVandalisable`,
// docs/research/ai-scripting.md, the riot's smash; docs/research/crew-wreck.md). Synthetic
// types only.
#include "gamemodes/level_items.h"

#include <catch2/catch_test_macros.hpp>

using coney::LevelItems;
using coney::world_objects::ObjectType;

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
