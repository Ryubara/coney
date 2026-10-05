// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/commands.h"

#include <cstdint>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/combat_script.h"
#include "core/pad.h"

using namespace coney::combat;

namespace {

// The research's history hold at runtime.
constexpr int kHistory = 7;

// The command of every update of `script` through the street's tables.
std::vector<CommandId> commandsOf(std::string_view script, std::uint64_t frames, int history = kHistory) {
    const CommandTables tables = CommandTables::street();
    CommandMatcher matcher;
    std::vector<CommandId> out;
    for (const coney::test::PadFrame& frame : coney::test::playScript(script, frames)) {
        out.push_back(matcher.update(frame.buttons, tables, history));
    }
    return out;
}

} // namespace

TEST_CASE("square pressed alone is 0xf, then held 0x15; under a held cross it is the combination 0x22", "[combat]") {
    const auto alone = commandsOf("2 press square\n5 release square\n", 7);
    CHECK(alone[1] == command::kNone);
    CHECK(alone[2] == command::kSquarePressed);
    CHECK(alone[3] == command::kSquareHeld);
    CHECK(alone[4] == command::kSquareHeld);
    CHECK(alone[5] == command::kNone); // no released entry for square

    const auto combo = commandsOf("2 press cross\n3 press square\n", 4);
    CHECK(combo[2] == command::kCrossPressed);
    CHECK(combo[3] == command::kCrossSquare); // combination press overwrites pressed
}

TEST_CASE("a circle tap gives 0x1e on the press and 0xd on the release; held 7 samples it gives 0xe", "[combat]") {
    const auto tap = commandsOf("2 tap circle\n", 4);
    CHECK(tap[2] == command::kCirclePressed);
    CHECK(tap[3] == command::kCircleTapped);

    // Six samples down is still a tap.
    const auto six = commandsOf("2 press circle\n8 release circle\n", 9);
    CHECK(six[8] == command::kCircleTapped);

    // Held: the history hold fires on exactly the 7th sample, and the release is no longer a tap.
    const auto held = commandsOf("2 press circle\n14 release circle\n", 16);
    CHECK(held[2] == command::kCirclePressed);
    for (std::size_t frame = 3; frame < 8; ++frame) {
        CHECK(held[frame] == command::kNone);
    }
    CHECK(held[8] == command::kCircleHeld);
    CHECK(held[9] == command::kNone);
    CHECK(held[14] == command::kNone);
}

TEST_CASE("the history hold follows the tunable sample count", "[combat]") {
    const auto held = commandsOf("0 press triangle\n", 8, 5);
    CHECK(held[0] == command::kTrianglePressed);
    CHECK(held[4] == command::kTriangleHeld);
    CHECK(held[6] == command::kNone);
}

TEST_CASE("a cross tap gives 0x12 then 0x10 on the release; held, 0x10 comes on its 4th sample", "[combat]") {
    const auto tap = commandsOf("2 tap cross\n", 4);
    CHECK(tap[2] == command::kCrossPressed);
    CHECK(tap[3] == command::kCrossLongHold);

    const auto held = commandsOf("2 press cross\n10 release cross\n", 11);
    CHECK(held[2] == command::kCrossPressed);
    CHECK(held[3] == command::kCrossHeld);
    CHECK(held[4] == command::kCrossHeld);
    CHECK(held[5] == command::kCrossLongHold);
    CHECK(held[6] == command::kCrossHeld);
    CHECK(held[10] == command::kNone); // released after 8 samples: no long hold

    // Released after 3 samples still fires on the release.
    const auto three = commandsOf("2 press cross\n5 release cross\n", 6);
    CHECK(three[5] == command::kCrossLongHold);
}

TEST_CASE("L2 held then cross or square pressed gives the charge and the dive", "[combat]") {
    const auto charge = commandsOf("0 stick left 0 100\n2 press l2\n6 tap cross\n", 8);
    CHECK(charge[2] == command::kL2Held);
    CHECK(charge[5] == command::kL2Held);
    CHECK(charge[6] == command::kL2Cross);
    CHECK(charge[7] == command::kCrossLongHold); // the release's long hold, as with any cross tap

    const auto dive = commandsOf("2 press l2\n6 tap square\n", 8);
    CHECK(dive[6] == command::kL2Square);
    CHECK(dive[7] == command::kL2Held);
}

TEST_CASE("R1 held blocks, L1 + R1 makes 0x1f, and L1's release makes 8", "[combat]") {
    const auto block = commandsOf("2 press r1\n5 press l1\n8 release l1\n10 release r1\n", 11);
    CHECK(block[2] == command::kR1Pressed);
    CHECK(block[3] == command::kR1Held);
    CHECK(block[5] == command::kL1R1);
    CHECK(block[6] == command::kR1Held); // L1's held entry comes before R1's, so R1 wins inside the table
    CHECK(block[8] == command::kL1Released);
    CHECK(block[10] == command::kNone);

    // Both at once (the research's `press l1 r1`).
    const auto both = commandsOf("2 press l1 r1\n", 3);
    CHECK(both[2] == command::kL1R1);
}

TEST_CASE("select combinations need select held and the d-pad pressed", "[combat]") {
    const auto combo = commandsOf("2 press select\n4 tap up\n6 tap l3\n", 7);
    CHECK(combo[4] == command::kSelectUp);
    CHECK(combo[6] == command::kSelectL3);
    const auto plain = commandsOf("4 tap up\n", 5);
    CHECK(plain[4] == command::kDpadUp);
}

TEST_CASE("tables added by hand match in the documented order", "[combat]") {
    // A released entry beats a tapped one on the same sample, and a later held entry beats an earlier one.
    CommandTables tables;
    tables.add(Trigger::Tapped, coney::pad::kSquare, 100);
    tables.add(Trigger::Released, coney::pad::kSquare, 101);
    tables.add(Trigger::Held, coney::pad::kL1, 102);
    tables.add(Trigger::Held, coney::pad::kL1, 103);
    CommandMatcher matcher;
    CHECK(matcher.update(coney::pad::kSquare, tables, kHistory) == command::kNone);
    CHECK(matcher.update(0, tables, kHistory) == 101);
    CHECK(matcher.update(coney::pad::kL1, tables, kHistory) == 103);
    CHECK(matcher.holdCount(2) == 1);
    CHECK(matcher.pressed() == coney::pad::kL1);
    CHECK(tables.table(Trigger::Query).empty());
}
