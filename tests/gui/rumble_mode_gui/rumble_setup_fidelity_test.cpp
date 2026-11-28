// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble set-up menus against the original's code where the orig sweep found them apart (#661): the arena
// filter, the roster's rotation in display order, the Game Type screen waiting for player 2's START, the gang screen's
// input rules, the presets of either side and the intro's names. Input comes from input scripts, as two pads.
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gui/colour_table.h"
#include "gui/rumble_mode_gui/rumble_data.h"
#include "gui/rumble_mode_gui/rumble_intro.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "support/rumble_fixtures.h"
#include "support/rumble_menu_harness.h"
#include "warriors/game_state.h"
#include "warriors/level_table.h"

using coney::RumbleSetup;
using coney::gui::RumbleMenu;
using coney::gui::RumbleScreen;
using coney::test::RumbleMenuHarness;
using Roster = coney::gui::RumbleGangEntry::Roster;

namespace {

// A game state whose level table holds `levels` (each its own record, indices from 0) with every one unlocked as an
// arena, and whose chosen mode is 12.
coney::GameState arenaState(std::span<const int> levels) {
    coney::GameState state;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        coney::LevelRecord record;
        record.id = static_cast<double>(i);
        record.name = "level" + std::to_string(levels[i]);
        record.number = levels[i];
        state.levels.set(record);
        state.unlockables.unlock(coney::UnlockKind::RumbleArena, levels[i]);
    }
    state.rumble.values.at(RumbleSetup::kGameType) = 12;
    return state;
}

// Whether `CfgRumbleArena(level, 9, modes)` lists the arena in `state`.
bool lists(const coney::GameState& state, int level, std::vector<std::int64_t> modes) {
    coney::gui::RumbleData data;
    modes.resize(16, 0);
    return coney::gui::addRumbleArena(data, state, level, 9, modes);
}

// Two created gangs (id -1, always listed) whose types 1-9 and 11-19 the stand-in table leaves alone, so each
// member's place after a rotation shows.
std::vector<std::byte> distinctGangChunk() {
    return coney::test::rumbleGangChunk({
        coney::test::TestGang{-1, "ONES", {1, 2, 3, 4, 5, 6, 7, 8, 9}},
        coney::test::TestGang{-1, "TENS", {11, 12, 13, 14, 15, 16, 17, 18, 19}},
    });
}

// Three created gangs (always listed) in list order.
std::vector<std::byte> threeGangChunk() {
    return coney::test::rumbleGangChunk({
        coney::test::TestGang{-1, "ONES", {1, 2, 3, 4, 5, 6, 7, 8, 9}},
        coney::test::TestGang{-1, "TENS", {11, 12, 13, 14, 15, 16, 17, 18, 19}},
        coney::test::TestGang{-1, "TWENTIES", {21, 22, 23, 24, 25, 26, 27, 28, 29}},
    });
}

// The front end's cue for a refused move (the end of a list), frontend.md#rumble-screens.
constexpr int kRefusedCue = 0xe;

} // namespace

TEST_CASE("an arena is listed only when its mode list starts with a mode and holds the chosen one", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    const std::array levels{102};
    const coney::GameState state = arenaState(levels);
    CHECK(lists(state, 102, {12}));
    CHECK(lists(state, 102, {14, 12}));
    // A list that starts with 0 is empty: it lists the arena for no mode.
    CHECK_FALSE(lists(state, 102, {0}));
    CHECK_FALSE(lists(state, 102, {0, 12}));
    // Only a 0 ends the list, and only its first 16 values are read.
    CHECK(lists(state, 102, {2, -1, 12}));
    std::vector<std::int64_t> seventeen(16, 3);
    seventeen.push_back(12);
    coney::gui::RumbleData data;
    CHECK_FALSE(coney::gui::addRumbleArena(data, state, 102, 9, seventeen));
}

TEST_CASE("an arena outside levels 101 to 199 is never listed", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    const std::array levels{100, 101, 199, 200};
    const coney::GameState state = arenaState(levels);
    CHECK_FALSE(lists(state, 100, {12}));
    CHECK(lists(state, 101, {12}));
    CHECK(lists(state, 199, {12}));
    CHECK_FALSE(lists(state, 200, {12}));
}

TEST_CASE("left rotates side 1's roster back in display order, the warchief in the centre", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    // One player: side 1 rotates left twice and right once more, then locks.
    RumbleMenuHarness h("10 tap cross\n20 tap cross\n30 tap left\n36 tap left\n50 tap right\n"
                        "60 tap cross\n");
    h.files["rumble_gang.lua"] = distinctGangChunk();
    h.to(40);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    REQUIRE(h.menu->gangs().roster(0) != nullptr);
    // Left is the back rotation for side 1: the slots (members 8 6 4 2 0 1 3 5 7, left to right) each take the
    // member to their right, so member 1 becomes the warchief, then member 3.
    CHECK(*h.menu->gangs().roster(0) == Roster{4, 6, 2, 8, 1, 9, 3, 7, 5});
    h.to(55);
    CHECK(*h.menu->gangs().roster(0) == Roster{2, 4, 1, 6, 3, 8, 5, 9, 7});
    h.to(65);
    CHECK(h.menu->gangs().activeSide() == 1);
}

TEST_CASE("right rotates side 1 forward, and left rotates side 2 forward", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    // Versus (player 2's START goes on), so side 2 may rotate: side 1 right once, locks; side 2 left once.
    RumbleMenuHarness h("1 p2 connect\n10 tap cross\n20 tap right\n26 tap right\n30 tap cross\n"
                        "40 p2 tap start\n50 tap right\n60 tap cross\n70 tap left\n");
    h.files["rumble_gang.lua"] = distinctGangChunk();
    h.to(45);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    REQUIRE(h.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleVersus);
    h.to(55);
    // Forward: each slot takes the member to its left, so member 2 (left of centre) becomes the warchief.
    CHECK(*h.menu->gangs().roster(0) == Roster{3, 1, 5, 2, 7, 4, 9, 6, 8});
    h.to(75);
    REQUIRE(h.menu->gangs().activeSide() == 1);
    REQUIRE(h.menu->gangs().roster(1) != nullptr);
    CHECK(*h.menu->gangs().roster(1) == Roster{13, 11, 15, 12, 17, 14, 19, 16, 18});
}

TEST_CASE("with one human, side 2's roster cannot rotate", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    RumbleMenuHarness h("10 tap cross\n20 tap cross\n30 tap cross\n40 tap left\n50 tap right\n");
    h.files["rumble_gang.lua"] = distinctGangChunk();
    // After the left alone (a left then a right would cancel out even where side 2 rotates), and after both.
    h.to(45);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    REQUIRE(h.menu->gangs().activeSide() == 1);
    REQUIRE(h.menu->gangs().roster(1) != nullptr);
    CHECK(*h.menu->gangs().roster(1) == Roster{11, 12, 13, 14, 15, 16, 17, 18, 19});
    h.to(55);
    CHECK(*h.menu->gangs().roster(1) == Roster{11, 12, 13, 14, 15, 16, 17, 18, 19});
}

TEST_CASE("in versus, back on side 2 with nothing of its own locked does nothing", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    RumbleMenuHarness h("1 p2 connect\n10 tap cross\n20 tap right\n26 tap right\n30 tap cross\n40 p2 tap start\n"
                        "50 tap cross\n60 tap triangle\n");
    h.to(55);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    REQUIRE(h.menu->gangs().locked(0));
    (void)h.menu->takeCues();
    h.to(65);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    CHECK(h.menu->gangs().locked(0));
    CHECK(h.menu->gangs().activeSide() == 1);
    CHECK(h.menu->takeCues().empty());
}

TEST_CASE("up and down stop at the ends of the gang list with the refused cue", "[rumble_menu]") {
    // Expected: frontend.md#rumble-screens, confirmed (code)
    // DUEL (one a side), one player: side 1 on RED SIDE, side 2 on BLUE SIDE, the last of the two listed.
    RumbleMenuHarness h("10 tap cross\n20 tap cross\n30 tap up\n40 tap down\n50 tap down\n");
    h.to(25);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    REQUIRE(h.setup().values.at(RumbleSetup::kGangSize) == 1);
    (void)h.menu->takeCues();
    // Up on the first gang stays, with the refused cue.
    h.to(35);
    CHECK(h.menu->gangs().gang(0)->name == "RED SIDE");
    CHECK(h.menu->takeCues() == std::vector<int>{kRefusedCue});
    // With one fighter a side, both sides may hold the same gang.
    h.to(45);
    CHECK(h.menu->gangs().gang(0)->name == "BLUE SIDE");
    CHECK(h.menu->takeCues() == std::vector<int>{coney::gui::kRumbleMoveCue});
    // Down on the last gang stays.
    h.to(55);
    CHECK(h.menu->gangs().gang(0)->name == "BLUE SIDE");
    CHECK(h.menu->takeCues() == std::vector<int>{kRefusedCue});
}

TEST_CASE("with more than one fighter a side, a side's cursor skips the other side's gang", "[rumble_menu]") {
    // Expected: frontend.md#rumble-screens, confirmed (code)
    // SQUAD (five a side), one player: side 1 on ONES, side 2 on TENS.
    RumbleMenuHarness h("10 tap down\n20 tap cross\n30 tap cross\n40 tap down\n50 tap down\n60 tap up\n");
    h.files["rumble_gang.lua"] = threeGangChunk();
    h.to(35);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    REQUIRE(h.setup().values.at(RumbleSetup::kGangSize) == 5);
    REQUIRE(h.menu->gangs().gang(0)->name == "ONES");
    REQUIRE(h.menu->gangs().gang(1)->name == "TENS");
    (void)h.menu->takeCues();
    h.to(45);
    CHECK(h.menu->gangs().gang(0)->name == "TWENTIES");
    CHECK(h.menu->takeCues() == std::vector<int>{coney::gui::kRumbleMoveCue});
    h.to(55);
    CHECK(h.menu->gangs().gang(0)->name == "TWENTIES");
    CHECK(h.menu->takeCues() == std::vector<int>{kRefusedCue});
    h.to(65);
    CHECK(h.menu->gangs().gang(0)->name == "ONES");
    CHECK(h.menu->takeCues() == std::vector<int>{coney::gui::kRumbleMoveCue});
}

TEST_CASE("a move whose only gang left is the other side's is refused", "[rumble_menu]") {
    // Expected: frontend.md#rumble-screens, confirmed (code)
    // SQUAD, one player, two gangs: side 1's down, and side 2's up once side 1 is locked, would land on the other
    // side's gang.
    RumbleMenuHarness h("10 tap down\n20 tap cross\n30 tap cross\n40 tap down\n50 tap cross\n60 tap up\n");
    h.files["rumble_gang.lua"] = distinctGangChunk();
    h.to(35);
    REQUIRE(h.menu->screen() == RumbleScreen::ChooseGangs);
    (void)h.menu->takeCues();
    h.to(45);
    CHECK(h.menu->gangs().gang(0)->name == "ONES");
    CHECK(h.menu->takeCues() == std::vector<int>{kRefusedCue});
    h.to(55);
    REQUIRE(h.menu->gangs().activeSide() == 1);
    (void)h.menu->takeCues();
    h.to(65);
    CHECK(h.menu->gangs().gang(1)->name == "TENS");
    CHECK(h.menu->takeCues() == std::vector<int>{kRefusedCue});
}

TEST_CASE("co-op and versus go on only on player 2's START; player 1's accept shows the message again",
          "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    // SQUAD, then co-op; player 1 accepts twice, then player 2 presses START.
    RumbleMenuHarness h("1 p2 connect\n10 tap down\n20 tap cross\n30 tap right\n40 tap cross\n50 tap cross\n"
                        "60 p2 tap start\n");
    h.to(45);
    REQUIRE(h.menu->screen() == RumbleScreen::GameType);
    CHECK(h.menu->message().text() == "SECOND PAD");
    h.to(55);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    CHECK(h.menu->message().text() == "SECOND PAD");
    h.to(65);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleCoop);
}

TEST_CASE("the player 2 message is grey and blinks until the selection changes", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    // SQUAD, co-op; the cross of frame 40 is read as accept a frame later (1,367 ms); the selection moves at frame 200.
    RumbleMenuHarness h("10 tap down\n20 tap cross\n30 tap right\n40 tap cross\n200 tap left\n");
    h.to(41);
    REQUIRE(h.menu->message().text() == "SECOND PAD");
    const coney::graphics::Rgba grey = coney::gui::kMenuGrey;
    CHECK(h.menu->message().style().colour.r == grey.r);
    CHECK(h.menu->message().style().colour.g == grey.g);
    CHECK(h.menu->message().style().colour.b == grey.b);
    // It fades in over 1,500 ms (frame 85 is 1,500 ms on), then out over the next 1,500 ms.
    h.to(62);
    const std::uint8_t rising = h.menu->message().style().colour.a;
    CHECK(rising > 100);
    CHECK(rising < 155);
    h.to(107);
    const std::uint8_t falling = h.menu->message().style().colour.a;
    CHECK(falling > 100);
    CHECK(falling < 155);
    // Still there after 4 s; gone once the selection changes.
    h.to(160);
    CHECK(h.menu->message().text() == "SECOND PAD");
    h.to(205);
    CHECK(h.menu->message().text().empty());
}

TEST_CASE("a mode with a preset for either side counts as preset", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    RumbleMenuHarness h("10 tap cross\n20 tap cross\n");
    h.files["rumble_data.lua"] =
        coney::test::rumbleModeChunk({coney::test::TestMode{"HALF", 12, true, false, false, 1, 0, 459}});
    h.menu->start(0);
    h.to(15);
    CHECK(h.setup().presetGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Types) == 0);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Types + 8) == 459);
    h.to(25);
    CHECK(h.menu->screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Pak) == 255);
}

TEST_CASE("the Rumble intro shows the names up to the first empty one", "[rumble]") {
    // Expected: rumble.md#intro, confirmed (code)
    coney::gui::RumbleIntro intro;
    const std::vector<std::string> names{"FIRST", "", "THIRD"};
    intro.open("Go", names, {0, 1}, 0);
    CHECK(intro.names() == std::vector<std::string>{"FIRST"});
}

TEST_CASE("the Rumble intro with all ten name slots filled counts no names", "[rumble]") {
    // Expected: rumble.md#intro, confirmed (code)
    // The original's count is stored only when its loop finds an empty slot, which ten full slots never give.
    coney::gui::RumbleIntro intro;
    const std::vector<std::string> names{"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"};
    intro.open("Go", names, {0, 1}, 0);
    CHECK(intro.names().empty());
    // Nine names and an empty tenth slot count nine.
    const std::vector<std::string> nine{"A", "B", "C", "D", "E", "F", "G", "H", "I", ""};
    intro.open("Go", nine, {0, 1}, 0);
    CHECK(intro.names().size() == 9);
}
