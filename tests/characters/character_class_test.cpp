// SPDX-License-Identifier: GPL-3.0-or-later
// From a character type to the model it is drawn as (docs/research/characters.md#type-to-model): Human_Init's class
// switch, the player's plain-alias rule and the `_a` models of levels 60-64. The model names come from a stand-in for
// the recorded `CfgChar` calls written here.
#include "characters/character_class.h"

#include <map>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "human/player.h"

using coney::characters::characterClassOf;
using coney::characters::modelNameFor;
using coney::characters::modelRecordType;

namespace {

// The model-name argument of a few types' CfgChar calls, as the reference lists them.
std::optional<std::string> modelOf(int type) {
    static const std::map<int, std::string> kModels{{1, "warr_cl"},  {2, "warr_cl_gen"}, {3, "warr_cl_ghost"},
                                                    {30, "warr_re"}, {32, "warr_re_cv"}, {33, "warr_sn"}};
    const auto found = kModels.find(type);
    return found == kModels.end() ? std::nullopt : std::optional<std::string>(found->second);
}

} // namespace

TEST_CASE("Human_Init's switch maps a type to its class, as a plain alias or a variant", "[character_class]") {
    // Expected: characters.md#type-to-model, confirmed (code)
    CHECK(characterClassOf(2).id == 1);
    CHECK(!characterClassOf(2).variant);
    CHECK(characterClassOf(4).id == 1);
    CHECK(characterClassOf(4).variant);
    CHECK(characterClassOf(32).id == 30);
    CHECK(characterClassOf(32).variant);
    CHECK(characterClassOf(31).id == 30);
    CHECK(characterClassOf(40).id == 38);
    CHECK(characterClassOf(222).id == 221);
    // A type not in the switch is its own class.
    CHECK(characterClassOf(33).id == 33);
    CHECK(characterClassOf(352).id == 352);
    CHECK(!characterClassOf(352).variant);
}

TEST_CASE("a player made as a plain alias is drawn as the class's model; anyone else as the type's own",
          "[character_class]") {
    // Expected: characters.md#type-to-model, confirmed (code)
    CHECK(modelRecordType(2, 1) == 1);
    CHECK(modelRecordType(2, 0) == 2);
    CHECK(modelRecordType(32, 1) == 32);
    CHECK(modelRecordType(3, 1) == 3);
    CHECK(modelNameFor(2, 1, 5, modelOf) == std::optional<std::string>("warr_cl"));
    CHECK(modelNameFor(2, 0, 5, modelOf) == std::optional<std::string>("warr_cl_gen"));
    CHECK(modelNameFor(32, 1, 99, modelOf) == std::optional<std::string>("warr_re_cv"));
    CHECK(modelNameFor(33, 1, 3, modelOf) == std::optional<std::string>("warr_sn"));
}

TEST_CASE("levels 60 to 64 draw the `_a` model, and a type with no CfgChar call has none", "[character_class]") {
    // Expected: characters.md#type-to-model, confirmed (code) at 0x00218008 (Human_Init reads +0x114 in levels 60-64)
    CHECK(modelNameFor(1, 1, 60, modelOf) == std::optional<std::string>("warr_cl_a"));
    CHECK(modelNameFor(1, 1, 64, modelOf) == std::optional<std::string>("warr_cl_a"));
    // Human_Init's own test, not the Armies test of the HUD and the sound (60-69): 65 draws the plain model.
    CHECK(modelNameFor(1, 1, 65, modelOf) == std::optional<std::string>("warr_cl"));
    CHECK(modelNameFor(1, 1, 59, modelOf) == std::optional<std::string>("warr_cl"));
    CHECK(modelNameFor(1, 1, 70, modelOf) == std::optional<std::string>("warr_cl"));
    CHECK(!modelNameFor(500, 1, 2, modelOf).has_value());
    CHECK(!modelNameFor(1, 1, 2, {}).has_value());
}

TEST_CASE("a Warriors type plays with his Warrior's class; anyone else with his own", "[character_class]") {
    // Expected: characters.md#power-classes, confirmed (code)
    using coney::characters::powerClassOf;
    using coney::characters::warriorClassOf;
    using coney::characters::warriorPowerClassOf;
    // One type of each group, in Warrior class order, then the extras and Ash.
    CHECK(warriorClassOf(11) == 0);
    CHECK(warriorClassOf(4) == 1);
    CHECK(warriorClassOf(189) == 1);
    CHECK(warriorClassOf(17) == 2);
    CHECK(warriorClassOf(188) == 3);
    CHECK(warriorClassOf(25) == 4);
    CHECK(warriorClassOf(191) == 5);
    CHECK(warriorClassOf(32) == 6);
    CHECK(warriorClassOf(39) == 6);
    CHECK(warriorClassOf(190) == 7);
    CHECK(warriorClassOf(5) == 8);
    CHECK(warriorClassOf(41) == coney::characters::kOtherWarriorClass);
    CHECK(warriorClassOf(417) == coney::characters::kOtherWarriorClass);
    CHECK(warriorPowerClassOf(11) == 58);
    CHECK(warriorPowerClassOf(32) == 64);
    CHECK(warriorPowerClassOf(10) == 66);
    CHECK(warriorPowerClassOf(41) == 7);
    // Any human of category 14, a player or not, takes it; another category keeps the type's own class.
    CHECK(powerClassOf(32, coney::characters::kWarriorsCategory, 40) == 64);
    CHECK(powerClassOf(417, 3, 2) == 2);
    CHECK(powerClassOf(41, coney::characters::kWarriorsCategory, 40) == 7);
}

TEST_CASE("Human_ApplyTypeScale sizes the listed classes, more of them in co-op, and leaves the rest",
          "[character_class]") {
    // Expected: human-init.md#size, confirmed (code)
    using coney::characters::typeScaleOf;
    CHECK(typeScaleOf(5, false) == 1.02F);
    CHECK(typeScaleOf(18, false) == 0.97F);
    CHECK(typeScaleOf(30, false) == 0.97F);
    CHECK(typeScaleOf(21, false) == 0.98F);
    CHECK(typeScaleOf(23, false) == 0.98F);
    CHECK(typeScaleOf(24, false) == 0.98F);
    CHECK(typeScaleOf(33, false) == 1.03F);
    CHECK(typeScaleOf(89, false) == 1.16F);
    CHECK(typeScaleOf(119, false) == 1.2F);
    CHECK(typeScaleOf(120, false) == 1.2F);
    CHECK(typeScaleOf(135, false) == 1.2F);
    CHECK(typeScaleOf(128, false) == 1.175F);
    CHECK(typeScaleOf(161, false) == 1.11F);
    // Co-op: the big five shrink to 1.1, 135 to 1.03; the rest as alone.
    CHECK(typeScaleOf(89, true) == 1.1F);
    CHECK(typeScaleOf(119, true) == 1.1F);
    CHECK(typeScaleOf(120, true) == 1.1F);
    CHECK(typeScaleOf(128, true) == 1.1F);
    CHECK(typeScaleOf(161, true) == 1.1F);
    CHECK(typeScaleOf(135, true) == 1.03F);
    CHECK(typeScaleOf(30, true) == 0.97F);
    // Between and around the listed ones: none.
    for (const int other :
         {0, 1, 4, 6, 17, 19, 20, 22, 25, 29, 31, 32, 34, 88, 90, 118, 121, 127, 129, 134, 136, 160, 162, 417}) {
        CHECK_FALSE(typeScaleOf(other, false).has_value());
        CHECK_FALSE(typeScaleOf(other, true).has_value());
    }
    // Rembrandt (type 32, class 30) is the 0.97 Coney's player is made at.
    CHECK(typeScaleOf(characterClassOf(coney::human::kPlayerType).id, false) == coney::human::kPlayerBodyScale);
}
