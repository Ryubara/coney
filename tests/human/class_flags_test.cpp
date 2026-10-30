// SPDX-License-Identifier: GPL-3.0-or-later
// The power class's switches copied into the human flags (`Human_ApplyClassFlags`, `0x00222c98`), on power classes
// written here: no disc.
#include "human/class_flags.h"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "combat/power_class.h"
#include "human/human_flags.h"

namespace flag = coney::human::flag;
using coney::combat::PowerClass;
using coney::human::applied;
using coney::human::classFlagChanges;

namespace {

// Every bit the switches decide.
constexpr std::uint64_t kSwitchBits = flag::kUngroundable | flag::kHardToStun | flag::kNoEscape |
                                      flag::kBlockFromReact | flag::kIncreasedReact | flag::kIncreasedReactOff |
                                      flag::kComboStrength | flag::kReducedReact;

} // namespace

TEST_CASE("a power class's switches at 1 set their flags and clear the paired ones", "[human][class_flags]") {
    // Expected: human-init.md#class-switches, confirmed (code)
    PowerClass power;
    power.capsReaction = 1;
    power.hardToStun = 1;
    power.noEscape = 1;
    power.blockFromReact = 1;
    power.reactBoost = 1;
    power.comboReact = 1;
    const std::uint64_t flags = applied(flag::kIncreasedReactOff | flag::kReducedReact, classFlagChanges(power));
    CHECK(flags == (flag::kUngroundable | flag::kHardToStun | flag::kNoEscape | flag::kBlockFromReact |
                    flag::kIncreasedReact | flag::kComboStrength));
}

TEST_CASE("a power class's paired switches at -1 set the other flag of each pair", "[human][class_flags]") {
    // Expected: human-init.md#class-switches, confirmed (code)
    PowerClass power;
    power.reactBoost = -1;
    power.comboReact = -1;
    const std::uint64_t flags = applied(flag::kIncreasedReact | flag::kComboStrength, classFlagChanges(power));
    CHECK(flags == (flag::kIncreasedReactOff | flag::kReducedReact));
}

TEST_CASE("a power class's switches at 0, or any value but 1 and -1, clear their flags and leave the rest",
          "[human][class_flags]") {
    // Expected: human-init.md#class-switches, confirmed (code)
    PowerClass power;
    power.capsReaction = 2;
    power.hardToStun = -1;
    power.reactBoost = 5;
    const std::uint64_t others = flag::kGod | flag::kRevivable | flag::kRageAllowed;
    CHECK(applied(others | kSwitchBits, classFlagChanges(power)) == others);
    // No switch both sets and clears a bit.
    PowerClass paired;
    paired.reactBoost = 1;
    paired.comboReact = -1;
    const coney::human::FlagChanges changes = classFlagChanges(paired);
    CHECK((changes.set & changes.cleared) == 0);
    CHECK((changes.set | changes.cleared) == kSwitchBits);
}
