// SPDX-License-Identifier: GPL-3.0-or-later
// The character types read from recorded `CfgChar` calls, on synthetic calls written here: no disc.
#include "characters/character_types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::characters::CharacterType;
using coney::characters::CharacterTypes;
using coney::script::Table;
using coney::script::Value;

namespace {

// A list table of `values` at keys 1, 2, ..., as RecordedCalls keeps a table argument.
Value listOf(const std::vector<double>& values) {
    auto table = std::make_shared<Table>();
    for (std::size_t i = 0; i < values.size(); ++i) {
        REQUIRE(table->set(Value(static_cast<double>(i + 1)), Value(values[i])));
    }
    return Value(table);
}

// A `CfgChar` call's 17 arguments: type `type`, health `health`, model `model`, a damage table of `damage` scaled by
// `scale`, an attack table of `attacks`, category `category`, power class `power`, speed class 2, behaviour 3,
// Warrior index 7.
std::vector<Value> cfgChar(double type, double health, const std::string& model, const std::vector<double>& damage = {},
                           double scale = 1.0, const std::vector<double>& attacks = {}, double category = 14.0,
                           double power = 7.0) {
    return {Value(type),    Value(3.0),      Value(category), Value(2.0),   Value(power),  Value(health),
            listOf(damage), listOf(attacks), Value(scale),    Value(model), Value("none"), Value(7.0),
            Value(0.0),     listOf({}),      Value("none"),   Value(0.0),   Value("none")};
}

} // namespace

TEST_CASE("a CfgChar call gives its type's model, health, classes and tables", "[characters]") {
    const std::vector<Value> call = cfgChar(32, 1800, "warr_re_cv", {15.0, 23.5, 0.0}, 2.0, {10, 300, -4});
    const std::optional<CharacterType> parsed = coney::characters::parseCfgChar(call);
    REQUIRE(parsed.has_value());
    const CharacterType type = parsed.value_or(CharacterType{});
    CHECK(type.type == 32);
    CHECK(type.model == "warr_re_cv");
    CHECK(type.health == 1800);
    CHECK(type.behaviour == 3);
    CHECK(type.speedClass == 2);
    CHECK(type.warrior == 7);
    CHECK(type.category == 14);
    CHECK(type.powerClass == 7);
    // The damage times the scale, truncated; the attacks clamped to bytes.
    CHECK(type.damage == std::vector<std::int16_t>{30, 47, 0});
    CHECK(type.attacks == std::vector<std::uint8_t>{10, 255, 0});
    // A call whose type is not a number defines nothing.
    CHECK_FALSE(coney::characters::parseCfgChar(std::vector<Value>{Value("x")}).has_value());
    CHECK_FALSE(coney::characters::parseCfgChar(std::vector<Value>{}).has_value());
}

TEST_CASE("the types are found by number, the last call of a type winning", "[characters]") {
    coney::script::RecordedCalls recorded;
    recorded.add("CfgChar", cfgChar(40, 1800, "warr_ty_cv"));
    recorded.add("CfgChar", cfgChar(30, 1800, "warr_re"));
    recorded.add("CfgChar", cfgChar(32, 1800, "warr_re_cv"));
    recorded.add("CfgChar", cfgChar(40, 1500, "warr_ty_cv"));
    recorded.add("CfgObj", std::vector<Value>{Value(1.0)});
    const CharacterTypes types = CharacterTypes::fromRecorded(recorded);
    REQUIRE(types.all().size() == 3);
    CHECK(types.all()[0].type == 30);
    CHECK(types.all()[2].type == 40);
    REQUIRE(types.find(40) != nullptr);
    CHECK(types.find(40)->health == 1500);
    CHECK(types.find(33) == nullptr);
    CHECK(CharacterTypes::fromRecorded(coney::script::RecordedCalls{}).empty());
}

TEST_CASE("a player's model follows the class rule and the Armies levels", "[characters]") {
    coney::script::RecordedCalls recorded;
    recorded.add("CfgChar", cfgChar(30, 1800, "warr_re"));
    recorded.add("CfgChar", cfgChar(31, 1800, "warr_re_gen"));
    recorded.add("CfgChar", cfgChar(32, 1800, "warr_re_cv"));
    const CharacterTypes types = CharacterTypes::fromRecorded(recorded);
    // 31 is a plain alias of class 30: a player of it is drawn as 30's model, an AI human as its own.
    CHECK(types.modelFor(31, 1, 99) == "warr_re");
    CHECK(types.modelFor(31, 0, 99) == "warr_re_gen");
    // 32 is a variant: a player keeps its own; in levels 60-64 the `_a` model.
    CHECK(types.modelFor(32, 1, 99) == "warr_re_cv");
    CHECK(types.modelFor(32, 1, 61) == "warr_re_cv_a");
    CHECK_FALSE(types.modelFor(33, 1, 99).has_value());
}

namespace {

// A `CfgPowerClass` call for class `id`: power maximum `power`, hurt below `hurt`, the struggle divisor 0 and every
// other field 0.5.
std::vector<Value> cfgPowerClass(double id, double power, double hurt) {
    std::vector<Value> call(20, Value(0.5));
    call[0] = Value(id);
    call[1] = Value(power);
    call[9] = Value(hurt);
    call[17] = Value(0.0);
    return call;
}

// A `CfgWarriorClass` call for Warrior class `id` with damage scale `percent` (byte +0x06) and every other byte 1.
std::vector<Value> cfgWarriorClass(double id, double percent) {
    std::vector<Value> call(14, Value(1.0));
    call[0] = Value(id);
    call[2] = Value(percent);
    return call;
}

} // namespace

TEST_CASE("a CfgPowerClass call fills the fields it gives over a base", "[characters]") {
    const coney::combat::PowerClass power =
        coney::characters::parseCfgPowerClass(cfgPowerClass(64, 380, 0.25), coney::combat::kPlayerPowerClass);
    CHECK(power.powerMax == 380);
    CHECK(power.hurtFraction == 0.25F);
    CHECK(power.groundMs == 0);        // 0.5 truncated
    CHECK(power.struggleDivisor == 1); // never 0: the struggle divides by it
    // A short call keeps the base's fields past its end.
    const coney::combat::PowerClass partial = coney::characters::parseCfgPowerClass(
        std::vector<Value>{Value(2.0), Value(150.0)}, coney::combat::kPlayerPowerClass);
    CHECK(partial.powerMax == 150);
    CHECK(partial.stunMs == coney::combat::kPlayerPowerClass.stunMs);
    // Argument 22, the throw at walls, clamped 0-1.
    std::vector<Value> walls = cfgPowerClass(64, 380, 0.25);
    walls.resize(22, Value(0.0));
    walls[21] = Value(5.0);
    CHECK(coney::characters::parseCfgPowerClass(walls, coney::combat::kPlayerPowerClass).throwsAtWalls);
    walls[21] = Value(0.0);
    CHECK_FALSE(coney::characters::parseCfgPowerClass(walls, coney::combat::kPlayerPowerClass).throwsAtWalls);
}

TEST_CASE("a player takes his class's damage, his Warrior's power class and its damage scale", "[characters]") {
    coney::script::RecordedCalls recorded;
    recorded.add("CfgChar", cfgChar(30, 1800, "warr_re", {15.0, 26.0}, 1.0, {}, 14.0, 40.0));
    recorded.add("CfgChar", cfgChar(31, 1800, "warr_re_gen", {99.0}, 1.0, {}, 14.0, 40.0));
    recorded.add("CfgChar", cfgChar(417, 600, "civ", {10.0}, 1.0, {}, 3.0, 2.0));
    recorded.add("CfgPowerClass", cfgPowerClass(64, 400, 0.3));
    recorded.add("CfgPowerClass", cfgPowerClass(2, 200, 0.35));
    recorded.add("CfgWarriorClass", cfgWarriorClass(6, 150));
    recorded.add("CfgWarriorClass", cfgWarriorClass(6, 115)); // the last call wins
    recorded.add("CfgWarriorClass", cfgWarriorClass(9, 135));
    const CharacterTypes types = CharacterTypes::fromRecorded(recorded);

    // Type 31, a plain alias of class 30: class 30's damage, Rembrandt's power class 64 and Warrior class 6 at 115 %.
    const std::optional<coney::characters::PlayerTraits> rembrandt = types.playerTraitsOf(31);
    REQUIRE(rembrandt.has_value());
    const coney::characters::PlayerTraits traits = rembrandt.value_or(coney::characters::PlayerTraits{});
    CHECK(traits.classType == 30);
    CHECK(traits.damage == std::vector<std::int16_t>{15, 26});
    CHECK(traits.powerClassId == 64);
    CHECK(traits.powerClass.value_or(coney::combat::PowerClass{}).powerMax == 400);
    CHECK(traits.warriorClass == 6);
    CHECK(traits.damagePercent == 115);

    // A civilian as a player: his own class 2, Warrior class 9's scale.
    const coney::characters::PlayerTraits civilian =
        types.playerTraitsOf(417).value_or(coney::characters::PlayerTraits{});
    CHECK(civilian.powerClassId == 2);
    CHECK(civilian.powerClass.value_or(coney::combat::PowerClass{}).hurtFraction == 0.35F);
    CHECK(civilian.warriorClass == 9);
    CHECK(civilian.damagePercent == 135);

    // Without CfgPowerClass and CfgWarriorClass calls a player has his class id but no record, and unscaled damage.
    coney::script::RecordedCalls bare;
    bare.add("CfgChar", cfgChar(33, 1800, "warr_sn", {12.0}));
    const coney::characters::PlayerTraits snow =
        CharacterTypes::fromRecorded(bare).playerTraitsOf(33).value_or(coney::characters::PlayerTraits{});
    CHECK(snow.powerClassId == 65);
    CHECK_FALSE(snow.powerClass.has_value());
    CHECK(snow.damagePercent == 0);
    CHECK_FALSE(types.playerTraitsOf(5).has_value());
}
