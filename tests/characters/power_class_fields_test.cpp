// SPDX-License-Identifier: GPL-3.0-or-later
// The configuration fields of #285 the AI and combat read: the power class's ground-strike cap (`+0x38`), weapon draw
// (`+0x41`) and flash when hurt (`+0x42`), and the character type's range table, on synthetic calls written here: no
// disc.
#include <cstddef>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/character_types.h"
#include "combat/power_class.h"

using Catch::Approx;
using coney::script::Value;

namespace {

// A `CfgPowerClass` call of 31 arguments for class 50, every one 0 but `index`, which is `value`.
std::vector<Value> powerCallWith(std::size_t index, double value) {
    std::vector<Value> call(31, Value(0.0));
    call[0] = Value(50.0);
    call[index] = Value(value);
    return call;
}

// The power class a call with argument `index` at `value` gives over the player's.
coney::combat::PowerClass powerWith(std::size_t index, double value) {
    return coney::characters::parseCfgPowerClass(powerCallWith(index, value), coney::combat::kPlayerPowerClass);
}

// A `CfgChar` call of 17 arguments for type 107 whose 14th argument is the table `ranges` (1-based, as a script's).
std::vector<Value> cfgCharWithRanges(const std::vector<double>& ranges) {
    std::vector<Value> call(17, Value(0.0));
    call[0] = Value(107.0);
    auto table = std::make_shared<coney::script::Table>();
    for (std::size_t i = 0; i < ranges.size(); ++i) {
        REQUIRE(table->set(Value(static_cast<double>(i + 1)), Value(ranges[i])));
    }
    call[13] = Value(table);
    return call;
}

} // namespace

TEST_CASE("CfgPowerClass argument 20 is the ground-strike cap +0x38, kept 0-127", "[characters]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    CHECK(powerWith(20, 3).groundStrikeCap == 3);
    CHECK(powerWith(20, -5).groundStrikeCap == 0);
    CHECK(powerWith(20, 300).groundStrikeCap == 127);
}

TEST_CASE("CfgPowerClass arguments 29 and 30 are the weapon draw +0x41 and the flash when hurt +0x42", "[characters]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    CHECK(powerWith(29, 1).drawsWeapon);
    CHECK_FALSE(powerWith(29, 0).drawsWeapon);
    CHECK(powerWith(29, 7).drawsWeapon); // clamped to 1
    CHECK_FALSE(powerWith(29, -1).drawsWeapon);
    CHECK(powerWith(30, 1).flashesWhenHurt);
    CHECK_FALSE(powerWith(30, 0).flashesWhenHurt);
    // Neither switch is the other's.
    CHECK_FALSE(powerWith(29, 1).flashesWhenHurt);
    CHECK_FALSE(powerWith(30, 1).drawsWeapon);
}

TEST_CASE("the player's class 64 caps ground strikes at 3 and neither draws nor flashes", "[characters]") {
    // Expected: characters.md#power-class-bytes, confirmed (code)
    CHECK(coney::combat::kPlayerPowerClass.groundStrikeCap == 3);
    CHECK_FALSE(coney::combat::kPlayerPowerClass.drawsWeapon);
    CHECK_FALSE(coney::combat::kPlayerPowerClass.flashesWhenHurt);
    CHECK(coney::combat::kCivilianPowerClass.groundStrikeCap == 4);
}

TEST_CASE("a CfgChar call's 14th argument is the range table, divided by 100", "[characters]") {
    // Expected: characters.md#classes, confirmed (code)
    const auto parsed = coney::characters::parseCfgChar(cfgCharWithRanges({250.0, 0.0, 175.0}));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->ranges.size() == 3);
    CHECK(parsed->ranges[0] == Approx(2.5F));
    CHECK(parsed->ranges[1] == Approx(0.0F));
    CHECK(parsed->ranges[2] == Approx(1.75F));
    // A call without the table has none.
    const auto bare = coney::characters::parseCfgChar(std::vector<Value>{Value(107.0)});
    REQUIRE(bare.has_value());
    CHECK(bare->ranges.empty());
}
