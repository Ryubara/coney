// SPDX-License-Identifier: GPL-3.0-or-later
// The Rumble menu's screens, built from synthetic chunks run through the real bindings, and the set-up each confirm
// writes (docs/research/frontend.md#rumble-setup, docs/research/frontend.md#rumble-data). Input comes from input
// scripts, as a pad: the d-pad, cross and triangle, and the left stick partly pushed. A synthetic font lays out the
// text.
#include "gui/rumble_mode_gui/rumble_menu.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/colour_table.h"
#include "support/rumble_fixtures.h"
#include "support/rumble_menu_harness.h"
#include "warriors/game_state.h"

using coney::RumbleSetup;
using coney::gui::RumbleMenu;
using coney::gui::RumbleMenuResult;
using coney::gui::RumbleScreen;

namespace {

// The default 1 ON 1 set-up read at run time on a fresh boot, the 23 values in order: the synthetic gangs carry the
// disc's ids and rosters, so the stand-ins give the same types.
constexpr std::array<std::uint16_t, RumbleSetup::kValues> kDefaultValues{
    3, 12, 1, 4, 2, 91, 94, 91, 92, 93, 94, 91, 92, 93, 225, 226, 224, 225, 226, 227, 228, 225, 226};

// The menu over the synthetic chunks, played from an input script.
using Harness = coney::test::RumbleMenuHarness;

} // namespace

TEST_CASE("the Rumble screens list what the chunks add and the fresh profile unlocks", "[rumble_menu]") {
    Harness h("");
    h.to(5);
    CHECK(h.chunksRun == std::vector<std::string>{"rumble_data.lua"});
    CHECK(h.menu->screen() == RumbleScreen::GameMode);
    // Of the four modes only 12 and 14 are unlocked; the list shows the chunk's titles and the selected description.
    REQUIRE(h.menu->entries().size() == 2);
    CHECK(h.menu->entries().at(0) == "DUEL");
    CHECK(h.menu->entries().at(1) == "SQUAD");
    CHECK(h.menu->entryText(0).text().find("about DUEL") != std::string::npos);
    CHECK(h.data.modes.at(1).playerOptions == std::array<bool, 3>{true, true, true});
    CHECK(!h.textBatch.sprites().empty());
}

TEST_CASE("accepting each Rumble screen's first entry writes the default set-up, screen by screen", "[rumble_menu]") {
    // Cross on Game Mode, Game Type, side 1, side 2 and the arena.
    Harness h("10 tap cross\n20 tap cross\n30 tap cross\n40 tap cross\n50 tap cross\n");

    // Game Mode: the mode's id, the gang size, the label and the options; the Game Type entries from the HUD strings.
    h.to(15);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    CHECK(h.setup().values.at(RumbleSetup::kGameType) == 12);
    CHECK(h.setup().values.at(RumbleSetup::kGangSize) == 1);
    CHECK(h.setup().modeLabel == ":DUEL");
    CHECK(h.setup().playerOptions == std::array<bool, 3>{true, false, true});
    CHECK(!h.setup().presetGangs);
    REQUIRE(h.menu->grid().items() == 2);
    CHECK(h.menu->grid().item(0).text == "ALONE");
    CHECK(h.menu->grid().item(1).text == "AGAINST");
    CHECK(h.menu->takeCues() == std::vector<int>{coney::gui::kRumbleConfirmCue});

    // Game Type: one player.
    h.to(25);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleOnePlayer);
    // Two gangs unlocked; side 1 starts on the first, side 2 on the second; the names stay empty until both lock.
    REQUIRE(h.data.gangs.size() == 2);
    REQUIRE(h.menu->gangs().gang(0) != nullptr);
    CHECK(h.menu->gangs().gang(0)->name == "RED SIDE");
    CHECK(h.menu->gangs().gang(1)->name == "BLUE SIDE");
    CHECK(h.setup().gangNames[0].empty());

    // Side 1 locks and side 2 chooses; side 2's lock writes the gangs.
    h.to(35);
    CHECK(h.menu->gangs().locked(0));
    CHECK(h.menu->gangs().activeSide() == 1);
    h.to(45);
    CHECK(h.menu->screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup().values == kDefaultValues);
    CHECK(h.setup().gangNames[0] == "RED SIDE");
    CHECK(h.setup().gangNames[1] == "BLUE SIDE");
    // One arena is unlocked and allows mode 12; its label is its level record's title.
    REQUIRE(h.menu->entries().size() == 1);
    CHECK(h.menu->entries().at(0) == "ARENA 102");

    // Choose Area: the confirm marks the arena, and the next update launches it with its level number.
    CHECK(h.to(60) == RumbleMenuResult::Started);
    // Released on frame 51, launched on frame 52 (the harness has moved past it).
    CHECK(h.frame == 53);
    CHECK(h.setup().levelNumber == 102);
}

TEST_CASE("the Game Type entries write 3 for one player, 2 for co-op and 1 for versus", "[rumble_menu]") {
    // Expected: frontend.md#rumble-setup, confirmed (code)
    // SQUAD (the stick most of the way down), then co-op or versus (right along the row) with a second pad: the
    // confirm shows the message, and player 2's START goes on.
    for (const auto& [moves, players] :
         {std::pair{std::string(""), coney::gui::kRumbleOnePlayer},
          std::pair{std::string("26 tap right\n"), coney::gui::kRumbleCoop},
          std::pair{std::string("26 tap right\n32 tap right\n"), coney::gui::kRumbleVersus}}) {
        INFO(moves);
        Harness h("1 p2 connect\n10 stick left 0 -70\n12 stick left 0 0\n20 tap cross\n" + moves +
                  "40 tap cross\n45 p2 tap start\n50 tap cross\n");
        h.to(30);
        CHECK(h.setup().values.at(RumbleSetup::kGameType) == 14);
        CHECK(h.setup().values.at(RumbleSetup::kGangSize) == 5);
        REQUIRE(h.menu->grid().items() == 3);
        CHECK(h.menu->grid().item(1).text == "TOGETHER");
        h.to(55);
        CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
        CHECK(h.setup().values.at(RumbleSetup::kGameMode) == players);
    }
}

TEST_CASE("co-op shows the second player's message until a second pad presses START", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    Harness h("10 stick left 0 -60\n12 stick left 0 0\n20 tap cross\n30 tap right\n40 tap cross\n"
              "50 tap cross\n120 p2 connect\n125 p2 tap start\n");
    h.to(45);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    CHECK(h.menu->message().text() == "SECOND PAD");
    // Player 1's second confirm stays; the message blinks on, it does not go.
    h.to(55);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    h.to(110);
    CHECK(h.menu->message().text() == "SECOND PAD");
    h.to(130);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleCoop);
}

TEST_CASE("WAR PARTY's copy writes mode 14, five a side, its label, all three options and no presets",
          "[rumble_menu]") {
    Harness h("10 tap down\n20 tap cross\n");
    // A mode with presets chosen before must not leave its flag behind.
    h.state.rumble.presetGangs = true;
    h.to(25);
    CHECK(h.setup().values.at(RumbleSetup::kGameType) == 14);
    CHECK(h.setup().values.at(RumbleSetup::kGangSize) == 5);
    CHECK(h.setup().modeLabel == ":SQUAD");
    CHECK(h.setup().playerOptions == std::array<bool, 3>{true, true, true});
    CHECK(!h.setup().presetGangs);
}

TEST_CASE("a mode with presets fills both sides and skips the gang screen", "[rumble_menu]") {
    // Expected: frontend.md#rumble-setup and frontend.md#rumble-data, confirmed (code)
    // RACE is the third entry once unlocked (ENDLESS stays locked); the menu starts again to list it.
    Harness h("10 tap down\n16 tap down\n24 tap cross\n34 tap cross\n");
    h.state.unlockables.unlock(coney::UnlockKind::RumbleMode, 24);
    h.state.unlockables.unlock(coney::UnlockKind::RumbleArena, 103);
    h.menu->start(0);
    h.to(29);
    CHECK(h.setup().values.at(RumbleSetup::kGameType) == 24);
    CHECK(h.setup().presetGangs);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Types) == 458);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Types + 8) == 459);
    h.to(39);
    CHECK(h.menu->screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Pak) == 255);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Pak) == 255);
    // Only arena 103 lists mode 24.
    REQUIRE(h.menu->entries().size() == 1);
    CHECK(h.menu->entries().at(0) == "ARENA 103");
}

TEST_CASE("left and right rotate the active side's roster to choose the warchief, except in co-op", "[rumble_menu]") {
    // Expected: frontend.md#rumble-data, confirmed (code)
    // One player: side 1 rotates left once; side 2's right (the stick partly pushed) does nothing with one human, as
    // in the original (0x001ef7c0); then both lock.
    Harness h("10 tap cross\n20 tap cross\n30 tap left\n40 tap cross\n50 stick left 60 0\n52 stick left 0 0\n"
              "70 tap cross\n");
    h.to(35);
    REQUIRE(h.menu->gangs().roster(0) != nullptr);
    CHECK(h.menu->gangs().roster(0)->front() == 94);
    CHECK(h.menu->gangs().roster(0)->back() == 91);
    // Side 2's copy of the same gang is untouched.
    CHECK(h.data.gangs.at(0).rosters[1].front() == 91);
    h.to(75);
    CHECK(h.menu->screen() == RumbleScreen::ChooseArea);
    CHECK(h.setup().values.at(RumbleSetup::kGang1Types) == 94);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Types) == 225);
    CHECK(h.setup().values.at(RumbleSetup::kGang2Types + 1) == 226);

    // Co-op (player 2's START goes on): left does nothing.
    Harness coop("1 p2 connect\n10 tap down\n20 tap cross\n30 tap right\n40 tap cross\n50 p2 tap start\n60 tap left\n");
    coop.to(65);
    CHECK(coop.setup().values.at(RumbleSetup::kGameMode) == coney::gui::kRumbleCoop);
    REQUIRE(coop.menu->gangs().roster(0) != nullptr);
    CHECK(coop.menu->gangs().roster(0)->front() == 91);
}

TEST_CASE("up and down move the active side's gang, and back unlocks before it leaves the screen", "[rumble_menu]") {
    Harness h("10 tap cross\n20 tap cross\n30 tap down\n40 tap cross\n50 tap triangle\n60 tap triangle\n");
    h.to(35);
    CHECK(h.menu->gangs().gang(0)->name == "BLUE SIDE");
    h.to(45);
    CHECK(h.menu->gangs().activeSide() == 1);
    // Back unlocks side 1 and makes it active again; a second back returns to the Game Type screen.
    h.to(55);
    CHECK(!h.menu->gangs().locked(0));
    CHECK(h.menu->gangs().activeSide() == 0);
    h.to(65);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
}

TEST_CASE("back returns to the Rumble screen before, and cancels from the first", "[rumble_menu]") {
    Harness h("10 stick left 0 -70\n12 stick left 0 0\n20 tap cross\n30 tap cross\n40 tap triangle\n50 tap circle\n"
              "60 tap triangle\n");
    h.to(35);
    CHECK(h.menu->screen() == RumbleScreen::ChooseGangs);
    h.to(45);
    CHECK(h.menu->screen() == RumbleScreen::GameType);
    h.to(55);
    CHECK(h.menu->screen() == RumbleScreen::GameMode);
    // The mode chosen before is still selected.
    CHECK(h.menu->selectedEntry() == 1);
    (void)h.menu->takeCues();
    CHECK(h.to(65) == RumbleMenuResult::Cancelled);
    CHECK(h.menu->takeCues() == std::vector<int>{coney::gui::kRumbleBackCue});
}

TEST_CASE("the Rumble defaults run the chunks and take each screen's first entry", "[rumble_menu]") {
    Harness h("");
    const coney::gui::RumbleMenuServices services{
        .state = &h.state,
        .data = &h.data,
        .strings = &h.strings,
        .runChunk = [&h](std::string_view chunk) { h.scripts.runFile(chunk); },
    };
    const std::optional<RumbleSetup> setup = coney::gui::rumbleMenuDefaults(services, 117);
    REQUIRE(setup.has_value());
    CHECK(setup.value_or(RumbleSetup{}).values == kDefaultValues);
    CHECK(setup.value_or(RumbleSetup{}).gangNames[1] == "BLUE SIDE");
    CHECK(setup.value_or(RumbleSetup{}).levelNumber == 117);

    // Without the chunks there is nothing to choose.
    h.files.clear();
    CHECK(!coney::gui::rumbleMenuDefaults(services, 102).has_value());
}

TEST_CASE("the Rumble background cycles red, green and blue over 5 s legs, and the layout follows the video flags",
          "[rumble_menu]") {
    using coney::gui::rumbleBackgroundColour;
    CHECK(rumbleBackgroundColour(0) == coney::graphics::Rgba{150, 50, 50, 255});
    CHECK(rumbleBackgroundColour(2500) == coney::graphics::Rgba{100, 100, 50, 255});
    CHECK(rumbleBackgroundColour(5000) == coney::graphics::Rgba{50, 150, 50, 255});
    CHECK(rumbleBackgroundColour(10000) == coney::graphics::Rgba{50, 50, 150, 255});
    CHECK(rumbleBackgroundColour(15000) == coney::graphics::Rgba{150, 50, 50, 255});

    const coney::gui::RumbleLayout normal = coney::gui::RumbleLayout::forFlags(false, false, false);
    CHECK(normal.titleY == 0.08F);
    CHECK(normal.gridY == 0.84F);
    CHECK(normal.usageY == 0.91F);
    CHECK(coney::gui::RumbleLayout::forFlags(true, false, false).titleY == 0.18F);
    CHECK(coney::gui::RumbleLayout::forFlags(false, true, false).usageY == 0.96F);
    CHECK(coney::gui::RumbleLayout::forFlags(true, true, false).gridY == 0.805F);
}

TEST_CASE("the Rumble screens' frame: centred titles and usage lines, Game Type in one centred row", "[rumble_menu]") {
    Harness h("10 tap cross\n");
    h.strings.set(RumbleMenu::kGameModeTitle, "MODE TITLE");
    h.strings.set(RumbleMenu::kUsageSelectBack, "SELECT BACK");
    h.strings.set(RumbleMenu::kUsageOneRow, "ONE ROW");
    h.menu->start(0);
    h.to(5);
    CHECK(h.menu->title().text() == "MODE TITLE");
    CHECK(h.menu->title().style().y == 0.08F);
    CHECK(h.menu->title().style().scale == RumbleMenu::kTitleScale);
    CHECK(h.menu->usage().text() == "SELECT BACK");
    CHECK(h.menu->usage().style().y == 0.91F);
    // The selected mode grey, the other dim.
    CHECK(h.menu->entryText(0).style().colour == coney::gui::kMenuGrey);
    CHECK(h.menu->entryText(1).style().colour == coney::gui::kDimGrey);
    // Game Type: both entries on one row at y 0.84, joined by the separator; left and right move.
    h.to(15);
    REQUIRE(h.menu->screen() == RumbleScreen::GameType);
    CHECK(h.menu->grid().rows() == 1);
    CHECK(h.menu->grid().item(0).separator);
    CHECK(h.menu->grid().itemPosition(0, h.canvas).second == Catch::Approx(0.84F));
    CHECK(h.menu->usage().text() == "ONE ROW");
}
