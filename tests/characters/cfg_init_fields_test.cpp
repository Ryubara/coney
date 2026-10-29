// SPDX-License-Identifier: GPL-3.0-or-later
// The configuration fields `Human_Init` reads beyond the model: the class's weapon (`CfgChar`'s `+0x18c`) and the
// power class's switches (`CfgPowerClass`'s bytes `+0x3a`-`+0x3f`), on synthetic calls written here: no disc.
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "characters/character_types.h"
#include "combat/power_class.h"

using coney::script::Value;

namespace {

// A `CfgChar` call of 17 arguments for type 300, every one 0 but the type and `weapon` (argument 17).
std::vector<Value> cfgCharWithWeapon(const std::string& weapon) {
    std::vector<Value> call(17, Value(0.0));
    call[0] = Value(300.0);
    call[16] = Value(weapon);
    return call;
}

// A `CfgPowerClass` call of 31 arguments for class 12, every one 0 but the six switches (arguments 23-28).
std::vector<Value> cfgPowerClassWithSwitches(double a23, double a24, double a25, double a26, double a27, double a28) {
    std::vector<Value> call(31, Value(0.0));
    call[0] = Value(12.0);
    call[22] = Value(a23);
    call[23] = Value(a24);
    call[24] = Value(a25);
    call[25] = Value(a26);
    call[26] = Value(a27);
    call[27] = Value(a28);
    return call;
}

// The weapon `parseCfgChar` reads from a call with `weapon` as its 17th argument, or "<not parsed>" when the call is
// refused (a value no case expects, so a refused call fails the check).
std::string parsedWeapon(const std::string& weapon) {
    const auto parsed = coney::characters::parseCfgChar(cfgCharWithWeapon(weapon));
    return parsed.has_value() ? parsed->weapon : std::string("<not parsed>");
}

} // namespace

TEST_CASE("a CfgChar call's 17th argument is the class's weapon, at most 31 characters", "[characters]") {
    // Expected: human-init.md#weapon, confirmed (code)
    CHECK(parsedWeapon("dyn_baton") == "dyn_baton");
    CHECK(parsedWeapon("none") == "none");
    CHECK(parsedWeapon(std::string(40, 'w')) == std::string(31, 'w'));
}

TEST_CASE("a CfgPowerClass call's arguments 23-28 are the switches +0x3a-+0x3f, 27 and 28 crossed", "[characters]") {
    // Expected: human-init.md#class-switches, confirmed (code)
    const coney::combat::PowerClass power = coney::characters::parseCfgPowerClass(
        cfgPowerClassWithSwitches(1, 2, 3, 4, 5, 6), coney::combat::kPlayerPowerClass);
    CHECK(power.capsReaction == 1);
    CHECK(power.hardToStun == 2);
    CHECK(power.noEscape == 3);
    CHECK(power.blockFromReact == 4);
    CHECK(power.comboReact == 5); // argument 27 is +0x3f
    CHECK(power.reactBoost == 6); // argument 28 is +0x3e
    // Each is the number's low byte, signed and unclamped: -1 stays -1, 257 is 1.
    const coney::combat::PowerClass signedBytes = coney::characters::parseCfgPowerClass(
        cfgPowerClassWithSwitches(-1, 257, 0, 0, -1, 1), coney::combat::kPlayerPowerClass);
    CHECK(signedBytes.capsReaction == -1);
    CHECK(signedBytes.hardToStun == 1);
    CHECK(signedBytes.comboReact == -1);
    CHECK(signedBytes.reactBoost == 1);
}
