// SPDX-License-Identifier: GPL-3.0-or-later
// The mission select screen (docs/research/frontend.md#fn-missionselecthud): the level records' area names, labels
// and map points, which missions it lists, how it groups them into areas and sorts both, and the screen's input: the
// area menu, the list, the checkpoint letters, the start and when the choice comes, back, the cues and the marker's
// pulse. Synthetic level records and unlockables only. Each new case here failed first on the unit's stubbed source
// (coney-tools orig fails-first).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/name_hash.h"
#include "gui/menu_input.h"
#include "gui/mission_select.h"
#include "warriors/level_table.h"
#include "warriors/profile_record.h"
#include "warriors/unlock_records.h"

using Catch::Approx;
using coney::LevelRecord;
using coney::LevelTable;
using coney::SavedProgress;
using coney::UnlockRecord;
using coney::UnlockRecords;
using Command = coney::gui::MenuCommand;
namespace gui = coney::gui;

namespace {

// A level record: index, number, mission byte, "<label>:<area>", the menu name, sections, subway colour, logo word.
LevelRecord level(std::size_t index, int number, int mission, std::string second, std::string name, int sections,
                  int subway = 1, std::uint32_t logo = 0x19000b) {
    LevelRecord record;
    record.id = static_cast<double>(index);
    record.number = number;
    record.secondName = std::move(second);
    record.fourthName = std::move(name);
    record.values.at(LevelRecord::kSectionsValue) = sections;
    record.values.at(LevelRecord::kMissionValue) = mission;
    record.values.at(LevelRecord::kSubwayValue) = subway;
    record.values.at(LevelRecord::kLogoValue) = logo;
    return record;
}

// Unlockables in which each of `levels` has its (level, 0, 0) record, every one unlocked.
struct Completion {
    UnlockRecords unlocks;
    SavedProgress progress;
};
Completion complete(const std::vector<std::uint8_t>& levels) {
    Completion out;
    out.unlocks.setCount(levels.size());
    for (std::size_t i = 0; i < levels.size(); ++i) {
        REQUIRE(out.unlocks.set(
            i, UnlockRecord{.level = levels[i], .group = 0, .item = 0, .type = 0, .extra = 0, .data = 0}));
        out.progress.setLocked(i, false);
    }
    return out;
}

// A table of five complete missions in three areas: Coney (two, mission bytes 1 and 2, labels "2" and "1"), Soho
// (mission byte 6) and Bronx (bytes 11 and 13, one label "x" that reads as 0), plus records the filter drops.
LevelTable table() {
    LevelTable out;
    REQUIRE(out.set(level(0, 100, 0, "", "front end", 0)));
    REQUIRE(out.set(level(1, 99, 1, "2:Coney", "New Blood", 3, 1, 0x19000b)));
    REQUIRE(out.set(level(2, 80, 2, "1:Coney", "Real Live Bunch", 4, 1, 0x190001)));
    REQUIRE(out.set(level(3, 3, 6, "6:Soho", "Writer's Block", 5, 3, 0x190002)));
    REQUIRE(out.set(level(4, 31, 11, "x:Bronx", "Boys In Blue", 6, 2, 0x190003)));
    REQUIRE(out.set(level(5, 9, 13, "13:Bronx", "All-City", 3, 2, 0x190004)));
    REQUIRE(out.set(level(6, 95, 24, "", "hub", 12)));             // the hub: never listed
    REQUIRE(out.set(level(7, 60, 25, "1:Stage", "Stage 1", 3)));   // Armies of the Night: never listed
    REQUIRE(out.set(level(8, 101, 26, "1:Arena", "Junkyard", 0))); // past 99: never listed
    REQUIRE(out.set(level(9, 84, 18, "18:Coney", "Come Out", 3))); // not complete
    return out;
}

// The levels of table() that are complete.
Completion tableCompletion() { return complete({99, 80, 3, 31, 9, 95, 60, 101}); }

// A screen opened at time 0 over table()'s areas, its cues recorded.
struct Screen {
    gui::MissionSelectScreen screen;
    std::vector<int> cues;
    std::uint64_t now = 0;

    Screen() {
        const Completion done = tableCompletion();
        screen.setSoundSink([this](int cue) { cues.push_back(cue); });
        screen.open(gui::buildMissionSelectAreas(table(), done.unlocks, done.progress, 0), now);
        screen.update(gui::GuiFrame{now, nullptr});
    }
    // One command, 200 ms after the last, then a frame.
    void press(Command command) {
        now += 200;
        screen.handle(command, now);
        screen.update(gui::GuiFrame{now, nullptr});
    }
    // A frame at `ms`.
    void at(std::uint64_t ms) {
        now = ms;
        screen.update(gui::GuiFrame{now, nullptr});
    }
};

} // namespace

TEST_CASE("the mission select's level record texts: the area after the ':', the label before it, read as a number",
          "[mission_select]") {
    // Expected: frontend.md#warriors-functions, confirmed (code) at 0x0041f3e8, 0x0041f428 and 0x00433a68
    const LevelRecord coney = level(1, 99, 1, "1:Coney", "New Blood", 3);
    CHECK(gui::missionAreaName(coney) == "Coney");
    CHECK(gui::missionLabel(coney) == "1");
    CHECK_FALSE(gui::missionAreaName(level(1, 99, 1, "Coney", "", 3)).has_value());
    CHECK(gui::missionAreaName(level(1, 99, 1, "1:", "", 3)) == "");
    // At most three characters; none before a leading ':'.
    CHECK(gui::missionLabel(level(1, 99, 1, "1234:A", "", 3)) == "123");
    CHECK(gui::missionLabel(level(1, 99, 1, ":A", "", 3)).empty());
    CHECK(gui::missionLabel(level(1, 99, 1, "12", "", 3)) == "12");
    CHECK(gui::labelNumber("12") == 12);
    CHECK(gui::labelNumber(" 7") == 7);
    CHECK(gui::labelNumber("-3") == -3);
    CHECK(gui::labelNumber("x") == 0);
    CHECK(gui::labelNumber("") == 0);
}

TEST_CASE("the mission select's map points, logo points and halo colours by video mode", "[mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) at 0x0041f540, 0x0041f5f8 and 0x0041f340 (the tables
    // at 0x005148e0 and 0x00514c78)
    CHECK(gui::mapColumn(false, false, false) == 0);
    CHECK(gui::mapColumn(false, true, false) == 1);
    CHECK(gui::mapColumn(false, true, true) == 2);
    CHECK(gui::mapColumn(true, false, true) == 3);
    CHECK(gui::mapColumn(true, true, false) == 4);
    const LevelRecord first = level(1, 99, 1, "1:Coney", "", 3);
    CHECK(gui::missionMapPoint(first, 0).x == Approx(0.36F));
    CHECK(gui::missionMapPoint(first, 0).y == Approx(0.947F).margin(0.001));
    CHECK(gui::missionMapPoint(first, 3).y == Approx(0.848F).margin(0.001));
    const LevelRecord eleventh = level(4, 31, 11, "11:Bronx", "", 6);
    CHECK(gui::missionMapPoint(eleventh, 0).x == Approx(0.184F).margin(0.001));
    CHECK(gui::missionMapPoint(eleventh, 0).y == Approx(0.134F).margin(0.001));
    CHECK(gui::missionLogoPoint(first, 0).x == Approx(0.36F));
    CHECK(gui::missionLogoPoint(first, 0).y == Approx(0.744F).margin(0.001));
    // No mission byte: Coney's (0, 0).
    CHECK(gui::missionMapPoint(level(0, 100, 0, "", "", 0), 0) == gui::MapPoint{});
    CHECK(gui::areaHaloColour(1) == coney::graphics::Rgba{186, 139, 53, 255});
    CHECK(gui::areaHaloColour(2) == coney::graphics::Rgba{48, 137, 81, 255});
    CHECK(gui::areaHaloColour(3) == coney::graphics::Rgba{64, 95, 142, 255});
    CHECK(gui::areaHaloColour(4) == coney::graphics::Rgba{76, 53, 44, 255});
}

TEST_CASE("the mission select lists complete levels 2-99 but the hub and the Armies of the Night", "[mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) at 0x001bc100
    const Completion done = complete({99, 2, 95, 60, 65, 66, 1, 100});
    const auto listed = [&done](int number) {
        return gui::isMissionSelectListed(level(1, number, 1, "1:A", "", 3), done.unlocks, done.progress);
    };
    CHECK(listed(99));
    CHECK(listed(2));
    CHECK(listed(66));
    CHECK_FALSE(listed(95));
    CHECK_FALSE(listed(60));
    CHECK_FALSE(listed(65));
    CHECK_FALSE(listed(1));
    CHECK_FALSE(listed(100));
    CHECK_FALSE(listed(80)); // not complete
}

TEST_CASE("the mission select's areas: grouped by the CRC-32 of the area name, by map height, missions by label",
          "[mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) at 0x001bcc48, 0x001bca70, 0x001bc930 and the
    // comparators 0x004e58f8 and 0x004e6410; confirmed (runtime) for the grouping and the order
    // (run r-20261119-mission-select)
    const Completion done = tableCompletion();
    const std::vector<gui::MissionSelectArea> areas =
        gui::buildMissionSelectAreas(table(), done.unlocks, done.progress, 0);
    REQUIRE(areas.size() == 3);
    // Coney (y 0.947), Soho (0.622), Bronx (0.134): the larger y first.
    CHECK(areas.at(0).name == "Coney");
    CHECK(areas.at(0).hash == coney::crc32(std::string_view("Coney")));
    CHECK(areas.at(1).name == "Soho");
    CHECK(areas.at(2).name == "Bronx");
    // The marker and the logo are the first mission's in index order, not in the sorted order.
    CHECK(areas.at(0).firstRecord == 1);
    CHECK(areas.at(0).logoWord == 0x19000b);
    CHECK(areas.at(0).halo == coney::graphics::Rgba{186, 139, 53, 255});
    CHECK(areas.at(2).firstRecord == 4);
    CHECK(areas.at(2).marker.y == Approx(0.134F).margin(0.001));
    // Each area's missions by their label read as a number, the smaller first: "1" before "2", "x" (0) before "13".
    REQUIRE(areas.at(0).missions.size() == 2);
    CHECK(areas.at(0).missions.at(0).record == 2);
    CHECK(areas.at(0).missions.at(0).name == "Real Live Bunch");
    CHECK(areas.at(0).missions.at(1).record == 1);
    CHECK(areas.at(0).missions.at(1).level == 99);
    CHECK(areas.at(0).missions.at(1).checkpoints == 3);
    REQUIRE(areas.at(2).missions.size() == 2);
    CHECK(areas.at(2).missions.at(0).label == "x");
    CHECK(areas.at(2).missions.at(0).circleColour == gui::MissionSelectScreen::kPurple);
    CHECK(areas.at(2).missions.at(1).label == "13");
    CHECK(areas.at(2).missions.at(1).circleColour == coney::graphics::Rgba{62, 98, 41, 255}); // area 2
}

TEST_CASE("the mission select's area menu: up shows the next area, down the previous, refused at the ends",
          "[mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) at 0x001be070 (jump table 0x00554c50), 0x001bdc48
    // and 0x001bdcd0; confirmed (runtime) for up moving on (run r-20261119-mission-select)
    Screen s;
    REQUIRE(s.screen.isOpen());
    CHECK(s.screen.menuFocused());
    CHECK(s.screen.cursor() == 0);
    CHECK(s.screen.shownArea() == 0u);
    // Down at the first area: refused.
    s.press(Command::Down);
    CHECK(s.screen.cursor() == 0);
    CHECK(s.screen.refused());
    CHECK(s.cues == std::vector<int>{0xe});
    // Up twice: the next areas, the refusal cleared; a third up at the last is refused.
    s.press(Command::Up);
    CHECK(s.screen.cursor() == 1);
    CHECK(s.screen.shownArea() == 1u);
    CHECK_FALSE(s.screen.refused());
    s.press(Command::Up);
    s.press(Command::Up);
    CHECK(s.screen.cursor() == 2);
    CHECK(s.screen.refused());
    CHECK(s.cues == std::vector<int>{0xe, 4, 4, 0xe});
    // Left and right are not taken.
    s.press(Command::Left);
    s.press(Command::Right);
    CHECK(s.screen.cursor() == 2);
    CHECK(s.cues.size() == 4);
    // Down goes back.
    s.press(Command::Down);
    CHECK(s.screen.cursor() == 1);
    // Back cancels, without a cue.
    s.press(Command::Back);
    CHECK(s.screen.cancelled());
    CHECK(s.screen.done());
    CHECK_FALSE(s.screen.chosen());
    CHECK(s.cues.size() == 5);
}

TEST_CASE("the mission select's list: accept focuses it, a second opens a mission, its letters step within its "
          "sections, back closes it and returns",
          "[mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) at 0x001bde18 and 0x001be070; confirmed (runtime)
    // for down stepping the letter (run r-20261119-mission-select)
    Screen s;
    s.press(Command::Accept);
    CHECK_FALSE(s.screen.menuFocused());
    CHECK(s.cues.back() == 8);
    // Down moves to the second mission (the list's own move); then accept opens it.
    s.press(Command::Down);
    CHECK(s.screen.selectedMission() == 1);
    s.press(Command::Accept);
    CHECK(s.screen.missionOpen());
    CHECK(s.screen.areas().at(0).missions.at(1).open);
    CHECK(s.cues.back() == 8);
    // Up at letter A: refused. Down twice to C (three sections), a third down refused.
    s.press(Command::Up);
    CHECK(s.cues.back() == 0xe);
    CHECK(s.screen.refused());
    s.press(Command::Down);
    s.press(Command::Down);
    CHECK(s.screen.areas().at(0).missions.at(1).letter == 2);
    CHECK(s.cues.back() == 4);
    s.press(Command::Down);
    CHECK(s.screen.areas().at(0).missions.at(1).letter == 2);
    CHECK(s.cues.back() == 0xe);
    // The open mission keeps the list's selection.
    CHECK(s.screen.selectedMission() == 1);
    // Back closes the mission, keeping the list's focus; a second back returns to the area menu.
    s.press(Command::Back);
    CHECK_FALSE(s.screen.missionOpen());
    CHECK_FALSE(s.screen.areas().at(0).missions.at(1).open);
    CHECK_FALSE(s.screen.menuFocused());
    CHECK(s.cues.back() == 0xf);
    s.press(Command::Back);
    CHECK(s.screen.menuFocused());
    CHECK(s.cues.back() == 0xf);
    CHECK_FALSE(s.screen.done());
}

TEST_CASE("the mission select's start: the choice comes on the first frame the clock is past 2300 ms, the level and "
          "the letter + 1",
          "[mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) at 0x001be198, 0x001bde18 (the start stamps no time:
    // only MissionSelect_Init writes +0x6a0, a 0, at 0x001bd394) and 0x001bdd38; confirmed (runtime) for the level and
    // checkpoint (run r-20261119-mission-select)
    Screen s;
    s.press(Command::Up); // Soho: one mission, five sections
    s.press(Command::Accept);
    s.press(Command::Accept);
    s.press(Command::Down);
    s.press(Command::Down);
    s.press(Command::Accept); // at 1200 ms
    CHECK(s.screen.starting());
    const std::size_t cues = s.cues.size();
    // Commands are not taken while the start waits.
    s.screen.handle(Command::Back, s.now + 10);
    CHECK_FALSE(s.screen.cancelled());
    s.at(2300);
    CHECK_FALSE(s.screen.chosen());
    s.at(2301);
    CHECK(s.screen.chosen());
    CHECK(s.screen.done());
    CHECK_FALSE(s.screen.starting());
    CHECK(s.screen.chosenLevel() == 3);
    CHECK(s.screen.chosenCheckpoint() == 3);
    CHECK(s.cues.size() == cues + 1);
    CHECK(s.cues.back() == 8);

    // Started with the clock already past 2300 ms, the choice comes on the same frame: there is no delay after the
    // start.
    Screen late;
    late.at(5000);
    late.press(Command::Accept);
    late.press(Command::Accept);
    late.press(Command::Accept);
    CHECK(late.screen.chosen());
    CHECK(late.screen.chosenLevel() == 80);
    CHECK(late.screen.chosenCheckpoint() == 1);
}

TEST_CASE("the mission select's marker pulses between 2 and 1 times its size each 400 ms, its halo 1.55 times it",
          "[mission_select]") {
    // Expected: frontend.md#fn-missionselecthud, confirmed (code) at 0x001be198
    Screen s;
    const float marker = s.screen.layout().marker;
    CHECK(s.screen.pulse() == Approx(2.0F));
    s.at(200);
    CHECK(s.screen.pulse() == Approx(1.5F));
    CHECK(s.screen.halo().setupValues().height == Approx(marker * 1.5F * 1.55F));
    // The way turns at 400 ms: from 1 up to 2.
    s.at(400);
    CHECK(s.screen.pulse() == Approx(1.0F));
    s.at(500);
    CHECK(s.screen.pulse() == Approx(1.25F));
    // The other markers keep the marker's size; a new area starts the pulse again.
    s.screen.handle(Command::Up, 600);
    s.at(600);
    CHECK(s.screen.pulse() == Approx(2.0F));
    CHECK(s.screen.marker(0).setupValues().height == Approx(marker));
    CHECK(s.screen.marker(1).setupValues().height == Approx(marker * 2.0F));
}
