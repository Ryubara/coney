// SPDX-License-Identifier: GPL-3.0-or-later
// The statistics screen (docs/research/game-stats.md): the time's words, the listed and shown missions, the
// summary's and a mission's lines with their colours, the background's colour cycle and back closing it. Synthetic
// level records, unlockables and strings only.
// Each new case here failed first on the unit's stubbed source (coney-tools original fails-first).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"
#include "gui/game_stats.h"
#include "gui/global_strings.h"
#include "support/font_fixtures.h"
#include "warriors/level_table.h"
#include "warriors/profile_record.h"
#include "warriors/unlock_records.h"

using Catch::Approx;
using coney::LevelRecord;
using coney::LevelTable;
using coney::SavedProgress;
using coney::UnlockRecord;
using coney::UnlockRecords;
namespace gui = coney::gui;
namespace ids = coney::gui::game_stats_strings;

namespace {

// Strings named after their ids, and the time's words.
gui::GlobalStrings strings() {
    gui::GlobalStrings out;
    for (std::uint32_t id = 0x60; id <= 0x75; ++id) {
        out.set(id, "s" + std::to_string(id));
    }
    out.set(ids::kMinute, "minute");
    out.set(ids::kMinute + 1, "minutes");
    out.set(ids::kMinute + 2, "hour");
    out.set(ids::kMinute + 3, "hours");
    out.set(ids::kMinute + 4, "day");
    out.set(ids::kMinute + 5, "days");
    return out;
}

// A level record: index, number, mission byte, "<n>:<title>", the level's name, subway colour and picture word.
LevelRecord level(std::size_t index, int number, int mission, std::string title, std::string name, int subway,
                  std::uint32_t picture) {
    LevelRecord record;
    record.id = static_cast<double>(index);
    record.number = number;
    record.secondName = std::move(title);
    record.fourthName = std::move(name);
    record.values.at(LevelRecord::kMissionValue) = mission;
    record.values.at(LevelRecord::kSubwayValue) = subway;
    record.values.at(LevelRecord::kPictureValue) = picture;
    return record;
}

// The level's (level, 0, 0) record.
UnlockRecord complete(std::uint8_t number) {
    return UnlockRecord{.level = number, .group = 0, .item = 0, .type = 0, .extra = 0, .data = 0};
}

} // namespace

TEST_CASE("the play time: minutes, hours and minutes, days only when asked, joined by commas, singular for 1",
          "[game_stats]") {
    // Expected: game-stats.md#time-format, confirmed (code)
    const gui::GlobalStrings words = strings();
    CHECK(gui::formatPlayTime(&words, 0, false) == "0 minutes");
    CHECK(gui::formatPlayTime(&words, 61, false) == "1 minute");
    CHECK(gui::formatPlayTime(&words, 3600 + 120, false) == "1 hour, 2 minutes");
    // The minutes show after the hours even when 0.
    CHECK(gui::formatPlayTime(&words, 3600 + 59, false) == "1 hour, 0 minutes");
    CHECK(gui::formatPlayTime(&words, (26 * 3600) + 60, false) == "26 hours, 1 minute");
    CHECK(gui::formatPlayTime(&words, (26 * 3600) + 60, true) == "1 day, 2 hours, 1 minute");
    // With days but under a day: hours and minutes.
    CHECK(gui::formatPlayTime(&words, 7200, true) == "2 hours, 0 minutes");
}

TEST_CASE("the area colours of LevelRecord_GetAreaColour", "[game_stats]") {
    // Expected: game-stats.md#list, confirmed (code)
    CHECK(gui::areaColour(1) == coney::graphics::Rgba{132, 78, 37, 255});
    CHECK(gui::areaColour(2) == coney::graphics::Rgba{62, 98, 41, 255});
    CHECK(gui::areaColour(3) == coney::graphics::Rgba{42, 71, 141, 255});
    CHECK(gui::areaColour(4) == coney::graphics::Rgba{74, 49, 29, 255});
}

TEST_CASE("a level's picture batch follows GameStats_PickThumbBatch", "[game_stats]") {
    // Expected: game-stats.md#block, confirmed (code)
    CHECK(gui::thumbBatchOf(95) == gui::ThumbBatch::Special);
    CHECK(gui::thumbBatchOf(60) == gui::ThumbBatch::Special);
    CHECK(gui::thumbBatchOf(101) == gui::ThumbBatch::Special);
    CHECK(gui::thumbBatchOf(34) == gui::ThumbBatch::B);
    CHECK(gui::thumbBatchOf(99) == gui::ThumbBatch::A);
}

TEST_CASE("the list: levels with a mission byte, not 61-65 or 102-137, sorted, shown when complete or 95 and 101",
          "[game_stats]") {
    // Expected: game-stats.md#list, confirmed (code)
    LevelTable table;
    REQUIRE(table.set(level(0, 100, 0, "", "front end", 0, 0)));              // no mission byte: not listed
    REQUIRE(table.set(level(1, 99, 1, "1:New", "Coney", 1, 0x16000b)));       // complete
    REQUIRE(table.set(level(2, 80, 2, "2:Next", "Street", 3, 0x160002)));     // not complete: not shown
    REQUIRE(table.set(level(3, 34, 4, "0:Dark", "Park", 2, 0x170004)));       // complete, number 0
    REQUIRE(table.set(level(4, 95, 24, "", "hub", 0, 0x16000b)));             // always shown
    REQUIRE(table.set(level(5, 60, 25, "", "stage", 0, 0x16000b)));           // shown when complete
    REQUIRE(table.set(level(6, 61, 25, "", "stage 2", 0, 0)));                // 61-65: not listed
    REQUIRE(table.set(level(7, 101, 26, "3:Rumble", "arena", 1, 0x21b0000))); // always shown, never circled
    REQUIRE(table.set(level(8, 102, 26, "", "arena 2", 1, 0)));               // 102-137: not listed

    UnlockRecords unlocks;
    unlocks.setCount(4);
    REQUIRE(unlocks.set(0, complete(99)));
    REQUIRE(unlocks.set(1, complete(80)));
    REQUIRE(unlocks.set(2, complete(34)));
    REQUIRE(unlocks.set(3, UnlockRecord{.level = 34, .group = 0, .item = 9, .type = 4, .extra = 0, .data = 0}));
    SavedProgress progress;
    progress.setLocked(0, false);
    progress.setLocked(2, false);
    progress.missionBests.at(0).total = 60;
    progress.missionBests.at(1).total = 600;
    progress.missionBests.at(2).total = 7; // level 80, not shown: not counted
    progress.missionBests.at(4).total = 3000;
    progress.missionBests.at(4).bestScore = 5000;

    const gui::GlobalStrings text = strings();
    const gui::GameStatsData data = gui::gatherGameStats(table, unlocks, progress, &text);
    REQUIRE(data.levels.size() == 4);
    CHECK(data.levels.at(0).number == 99);
    CHECK(data.levels.at(1).number == 34);
    CHECK(data.levels.at(2).number == 95);
    CHECK(data.levels.at(3).number == 101);
    CHECK(data.missionsListed == 3); // six listed, less the three specials
    CHECK(data.missionsDone == 2);
    CHECK(data.totalSeconds == 60 + 600 + 3000); // slot 0 and the shown entries only

    const gui::GameStatsLevel& first = data.levels.at(0);
    CHECK(first.name == "Coney");
    CHECK(first.circled == "1");
    CHECK(first.circleColour.r == 132); // area 1
    CHECK(first.pictureWord == 0x16000b);
    CHECK(data.levels.at(1).circleColour == coney::graphics::Rgba{106, 65, 131, 255}); // reads as 0: purple
    const gui::GameStatsLevel& hub = data.levels.at(2);
    CHECK(hub.name == text.get(ids::kHub));
    CHECK(hub.circled.empty()); // the specials set only their text
    CHECK(hub.pictureWord == 0x180001);
    const gui::GameStatsLevel& rumble = data.levels.at(3);
    CHECK(rumble.name == text.get(ids::kRumbleArenas));
    CHECK(rumble.circled.empty()); // its title's "3" is not circled
    CHECK(rumble.pictureWord == 0x180003);

    // A mission's block: the score below its goal, the characters, the time, two line breaks to make five lines.
    CHECK(gui::gameStatsSubItemText(data.levels.at(1), &text) ==
          "<SIZE 1.2><COLOR B2B2B2FF>s109<COLOR 806400FF> 5000<COLOR 505050FF> (s110 <COLOR 861A1AFF>9000"
          "<COLOR 505050FF>)<CR><COLOR B2B2B2FF>s111<COLOR 806400FF> 0/1<CR>"
          "<COLOR B2B2B2FF>s102 <COLOR 806400FF>50 minutes<CR><CR>");
    // The hub has no score line, only the size: the time and four line breaks.
    CHECK(gui::gameStatsSubItemText(hub, &text) ==
          "<SIZE 1.2><COLOR B2B2B2FF>s102 <COLOR 806400FF>0 minutes<CR><CR><CR><CR>");

    // The summary: title, then percentage, missions, group items, records and time, each "label<COLOR c> value".
    CHECK(gui::gameStatsSummary(data, &text) ==
          "<BIGFONT><COLOR B2B2B2FF><SIZE 1.6>s96<CR></BIGFONT><SIZE 1.2>"
          "<COLOR 505050FF>s97<COLOR 806400FF> 0%<CR>"
          "<COLOR 505050FF>s99<COLOR 806400FF> 2/3<CR>"
          "<COLOR 505050FF>s100<COLOR 861A1AFF> 0/0<CR>" // no group items: complete
          "<COLOR 505050FF>s98<COLOR 806400FF> 0/1<CR>"
          "<COLOR 505050FF>s102 <COLOR 806400FF>1 hour, 1 minute");
}

TEST_CASE("the screen's background cycles red, green, blue, each step moving on the first frame past 5 s; back closes "
          "it, accept does not",
          "[game_stats]") {
    // Expected: game-stats.md#background, confirmed (code)
    using Screen = gui::GameStatsScreen;
    CHECK(Screen::backgroundColour(0, 0.0F) == coney::graphics::Rgba{150, 50, 50, 255});
    const coney::graphics::Rgba half = Screen::backgroundColour(0, 0.5F);
    CHECK(half.r == 100);
    CHECK(half.g == 100);
    // The fraction is dropped: green a third of the way to blue is 150 x 2/3 + 50 x 1/3 = 116.7, cut to 116.
    CHECK(Screen::backgroundColour(1, 1.0F / 3.0F).g == 116);
    CHECK(Screen::backgroundColour(2, 1.0F).r == 150); // blue back to red

    // The background is the converted GUI width 1.15 by 1.1.
    Screen screen;
    gui::GameStatsData data;
    data.levels.resize(3);
    screen.open(data, 0);
    CHECK(screen.background().setupValues().width.value_or(0.0F) ==
          Approx(coney::graphics::OverlayCamera::guiWidthToOverlay(1.15F)));
    CHECK(screen.background().setupValues().height == Approx(1.1F));
    // At 5,000 ms the first step is still blending; the first frame past it starts green afresh.
    screen.update(gui::GuiFrame{5000, nullptr});
    CHECK(screen.colourStep() == 0);
    CHECK(screen.background().colour().g == 150);
    screen.update(gui::GuiFrame{5016, nullptr});
    CHECK(screen.colourStep() == 1);
    CHECK(screen.background().colour() == coney::graphics::Rgba{50, 150, 50, 255});
    screen.update(gui::GuiFrame{5016 + 2500, nullptr});
    CHECK(screen.background().colour().b == 100);
    CHECK(screen.isOpen());
    CHECK(screen.list().count() == 3);
    screen.handle(gui::MenuCommand::Down, 1000);
    CHECK(screen.list().selected() == 1);
    screen.handle(gui::MenuCommand::Accept, 2000);
    CHECK_FALSE(screen.closed());
    screen.handle(gui::MenuCommand::Back, 3000);
    CHECK(screen.closed());
}

TEST_CASE("the list: five entries at most centred on (0.185, 0.45), headers 0.35 x 0.09, each block under its "
          "header, the focused one's block opening 550 ms after it took the focus",
          "[game_stats]") {
    // Expected: game-stats.md#placement, confirmed (code)
    using Screen = gui::GameStatsScreen;
    const gui::GlobalStrings text = strings();
    Screen screen;
    screen.setStrings(&text);
    std::vector<int> cues;
    screen.setSoundSink([&cues](int cue) { cues.push_back(cue); });
    gui::GameStatsData data;
    data.levels.resize(7);
    for (std::size_t i = 0; i < data.levels.size(); ++i) {
        data.levels.at(i).number = static_cast<int>(i) + 2;
        data.levels.at(i).circled = std::to_string(i + 1);
        data.levels.at(i).circleColour = gui::areaColour(1);
    }
    screen.open(data, 0);
    CHECK(screen.list().visibleCount() == 5);
    screen.update(gui::GuiFrame{16, nullptr});
    // The first header's and block's parts are placed when they are drawn.
    screen.render(gui::GuiCanvas{});

    // The first header centred on the list's place: 0.35 wide about x 0.185, so its text starts at the left edge 0.01.
    const gui::CircledText& first = screen.header(0);
    CHECK(first.visible());
    CHECK(first.height() == Approx(0.09F));
    CHECK(first.circleColour() == coney::graphics::Rgba{132, 78, 37, 255});
    CHECK(first.text().style().y == Approx(0.45F));
    CHECK(first.circles() == 1);

    // Its block under it, overlapping the header by two screen lines (2 / 448): the picture centred on the list's x,
    // as high as the block, the text 0.02 right of the 0.35 column, its first line half a size-1.0 line down.
    const float overlap = 2.0F / 448.0F;
    const gui::GameStatsSubItem& block = screen.subItem(0);
    CHECK(block.reveal() == 1.0F);
    const float blockHeight = block.fullHeight();
    CHECK(blockHeight > 0.0F);
    const float blockTop = 0.45F + 0.045F - overlap;
    CHECK(block.picture().setupValues().x == Approx(0.185F));
    CHECK(block.picture().setupValues().y == Approx(blockTop + (blockHeight / 2.0F)));
    CHECK(block.picture().setupValues().height == Approx(blockHeight));
    CHECK(block.text().style().x == Approx(0.185F - 0.175F + 0.35F + 0.02F));
    CHECK(block.text().style().y == Approx(blockTop + (coney::graphics::fontMetrics(1.0F).height / 2.0F)));

    // The second header centred one header and the shown block (less the overlap) further down.
    CHECK(screen.header(1).text().style().y == Approx(0.45F + 0.09F + blockHeight - overlap));
    // The second entry's block is closed.
    CHECK(screen.subItem(1).reveal() == 0.0F);
    CHECK_FALSE(screen.subItem(1).drawn());

    // Down: the first block closes a step a frame; the second opens only from 550 ms after the move, with cue 0xd.
    screen.handle(gui::MenuCommand::Down, 1000);
    std::uint64_t now = 1000;
    for (int frame = 0; frame < 4; ++frame) {
        now += 16;
        screen.update(gui::GuiFrame{now, nullptr});
    }
    CHECK(screen.subItem(0).reveal() == 0.0F);
    CHECK(screen.subItem(1).reveal() == 0.0F);
    cues.clear();
    screen.update(gui::GuiFrame{1000 + 550, nullptr});
    CHECK(screen.subItem(1).reveal() == Approx(0.25F));
    CHECK(cues == std::vector<int>{Screen::kRevealCue});
    for (int frame = 0; frame < 3; ++frame) {
        screen.update(gui::GuiFrame{1600 + static_cast<std::uint64_t>(frame), nullptr});
    }
    CHECK(screen.subItem(1).reveal() == 1.0F);
    CHECK(cues.size() == 1);
}

TEST_CASE("the layout per video mode: GameStats_Init's values for the 0x02 modes, progressive, 16:9 and the default",
          "[game_stats]") {
    // Expected: game-stats.md#values, confirmed (code) at 0x00215e78
    using Layout = gui::GameStatsLayout;
    const Layout normal = Layout::forFlags(false, false, false);
    CHECK(normal.textX == Approx(0.01F));
    CHECK(normal.summaryY == Approx(0.06F));
    CHECK(normal.usageY == Approx(0.94F));
    CHECK(normal.backgroundWidth == Approx(1.15F));
    CHECK(normal.backgroundHeight == Approx(1.1F));
    CHECK(normal.headerWidth == Approx(0.35F));
    CHECK(normal.headerHeight == Approx(0.09F));
    CHECK(normal.circleWidth == Approx(0.065F));
    CHECK(normal.circleHeight == Approx(0.11F));
    CHECK(normal.listX == Approx(0.185F));
    CHECK(normal.listY == Approx(0.45F));
    CHECK(normal.clipHeight == Approx(0.5F));
    CHECK(normal.stepDivisor == 3.0F);

    // 16:9 (flag 0x04 alone).
    const Layout wide = Layout::forFlags(false, true, false);
    CHECK(wide.textX == Approx(-0.07F));
    CHECK(wide.summaryY == Approx(0.04F));
    CHECK(wide.usageY == Approx(0.94F));
    CHECK(wide.backgroundWidth == Approx(1.45F));
    CHECK(wide.backgroundHeight == Approx(1.2F));
    CHECK(wide.headerWidth == Approx(0.43F));
    CHECK(wide.headerHeight == Approx(0.09F));
    CHECK(wide.circleWidth == Approx(0.081F));
    CHECK(wide.circleHeight == Approx(0.11F));
    CHECK(wide.listX == Approx(0.145F));
    CHECK(wide.listY == Approx(0.45F));
    CHECK(wide.clipHeight == Approx(0.5F));

    // Progressive (0x20), with or without 16:9; its circles keep their static size.
    for (const bool widescreen : {false, true}) {
        const Layout progressive = Layout::forFlags(false, widescreen, true);
        CHECK(progressive.textX == Approx(-0.05F));
        CHECK(progressive.summaryY == Approx(0.08F));
        CHECK(progressive.usageY == Approx(0.9F));
        CHECK(progressive.backgroundWidth == Approx(0.74F));
        CHECK(progressive.backgroundHeight == Approx(0.74F));
        CHECK(progressive.headerWidth == Approx(0.3F));
        CHECK(progressive.headerHeight == Approx(0.09F));
        CHECK(progressive.circleWidth == Approx(0.065F));
        CHECK(progressive.circleHeight == Approx(0.11F));
        CHECK(progressive.listX == Approx(0.1F));
        CHECK(progressive.listY == Approx(0.45F));
        CHECK(progressive.clipHeight == Approx(0.5F));
        CHECK(progressive.stepDivisor == 3.0F);
    }

    // 0x02, before any other flag, alone and with 16:9; the circles step half their width.
    const Layout flag02 = Layout::forFlags(true, false, true);
    CHECK(flag02.textX == Approx(0.02F));
    CHECK(flag02.summaryY == Approx(0.16F));
    CHECK(flag02.usageY == Approx(0.82F));
    CHECK(flag02.backgroundWidth == Approx(1.1F));
    CHECK(flag02.backgroundHeight == Approx(0.8F));
    CHECK(flag02.headerWidth == Approx(0.3F));
    CHECK(flag02.headerHeight == Approx(0.07F));
    CHECK(flag02.circleWidth == Approx(0.05F));
    CHECK(flag02.circleHeight == Approx(0.077F));
    CHECK(flag02.listX == Approx(0.17F));
    CHECK(flag02.listY == Approx(0.445F));
    CHECK(flag02.clipHeight == Approx(0.38F));
    CHECK(flag02.stepDivisor == 2.0F);
    const Layout flag02Wide = Layout::forFlags(true, true, false);
    CHECK(flag02Wide.textX == Approx(-0.15F));
    CHECK(flag02Wide.summaryY == Approx(0.14F));
    CHECK(flag02Wide.usageY == Approx(0.86F));
    CHECK(flag02Wide.backgroundWidth == Approx(1.55F));
    CHECK(flag02Wide.backgroundHeight == Approx(0.95F));
    CHECK(flag02Wide.headerWidth == Approx(0.43F));
    CHECK(flag02Wide.headerHeight == Approx(0.078F));
    CHECK(flag02Wide.circleWidth == Approx(0.051F));
    CHECK(flag02Wide.circleHeight == Approx(0.077F));
    CHECK(flag02Wide.listX == Approx(0.07F));
    CHECK(flag02Wide.listY == Approx(0.46F));
    CHECK(flag02Wide.clipHeight == Approx(0.4F));
    CHECK(flag02Wide.stepDivisor == 2.0F);
}

TEST_CASE("the screen in the 0x02 mode: the summary, the background, the headers and their circles where the mode's "
          "values put them",
          "[game_stats]") {
    // Expected: game-stats.md#values, confirmed (code) at 0x00215e78 and 0x002155a8
    const gui::GlobalStrings text = strings();
    gui::GameStatsScreen screen;
    screen.setStrings(&text);
    screen.setLayout(gui::GameStatsLayout::forFlags(true, false, false));
    gui::GameStatsData data;
    data.levels.resize(3);
    for (gui::GameStatsLevel& level : data.levels) {
        level.circled = "12";
    }
    screen.open(data, 0);
    screen.update(gui::GuiFrame{33, nullptr});
    CHECK(screen.summary().style().x == Approx(0.02F));
    CHECK(screen.summary().style().y == Approx(0.16F));
    CHECK(screen.background().setupValues().height == Approx(0.8F));
    CHECK(screen.background().setupValues().width.value_or(0.0F) ==
          Approx(coney::graphics::OverlayCamera::guiWidthToOverlay(1.1F)));
    // The first header centred on (0.17, 0.445), 0.3 x 0.07: its left edge at 0.02; its circles 0.05 x 0.077, each
    // stepping half its width before and after itself.
    const gui::CircledText& first = screen.header(0);
    CHECK(first.height() == Approx(0.07F));
    CHECK(first.text().style().y == Approx(0.445F));
    CHECK(first.circle(0).setupValues().width.value_or(0.0F) == Approx(0.05F));
    CHECK(first.circle(0).setupValues().height == Approx(0.077F));
    CHECK(first.circle(0).setupValues().x == Approx(0.02F + 0.025F));
    CHECK(first.circle(1).setupValues().x == Approx(0.02F + 0.075F));
}

TEST_CASE("the list scrolls: a move past the middle row hides the top header a quarter an update while the next one "
          "shows at the bottom, and the hidden header is not drawn",
          "[game_stats]") {
    // Expected: game-stats.md#scroll, confirmed (runtime) and confirmed (code) at 0x001e1d70 and 0x001e21d8
    const gui::GlobalStrings text = strings();
    gui::GameStatsScreen screen;
    screen.setStrings(&text);
    gui::GameStatsData data;
    data.levels.resize(7);
    screen.open(data, 0);
    std::uint64_t now = 0;
    // Updates every 33 ms (the original's 30 a second) up to `until`.
    const auto runTo = [&](std::uint64_t until) {
        while (now + 33 <= until) {
            now += 33;
            screen.update(gui::GuiFrame{now, nullptr});
        }
    };
    runTo(100);
    screen.handle(gui::MenuCommand::Down, now);
    runTo(1000);
    CHECK(screen.window().first() == 0);
    // The third entry is past the middle row: the first header goes, the sixth comes.
    screen.handle(gui::MenuCommand::Down, now);
    const float shares[] = {0.75F, 0.5F, 0.25F};
    for (const float share : shares) {
        runTo(now + 33);
        CHECK(screen.header(0).height() == Approx(0.09F * share));
        CHECK(screen.header(5).height() == Approx(0.09F * (1.0F - share)));
        // The second header follows the first's shown height (its block closed long ago).
        CHECK(screen.header(1).text().style().y == Approx(0.45F + (0.09F * share)));
    }
    runTo(now + 33);
    CHECK(screen.window().first() == 1);
    CHECK_FALSE(screen.header(0).visible());
    CHECK(screen.header(1).visible());
    CHECK(screen.header(1).text().style().y == Approx(0.45F));
}

TEST_CASE("a mission's block: five lines high, its picture's rectangle cut by the reveal and its text wiped from the "
          "top, line by line",
          "[game_stats]") {
    // Expected: game-stats.md#block, confirmed (code) at 0x00217788, 0x001e32a0, 0x001e3070 and 0x001b9600
    const gui::GlobalStrings text = strings();
    const coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch batch(font.sheet(), 4096, 9000.0F);
    const gui::GuiCanvas canvas{.fonts = [&font](int /*slot*/) { return &font; },
                                .textBatch = [&batch](int /*slot*/) { return &batch; }};
    // A level with all three counts: the score line, the three counts and the time line.
    gui::GameStatsLevel level;
    level.targets.groupItems.total = 2;
    level.targets.characters.total = 1;
    level.targets.arenas.total = 3;
    gui::GameStatsSubItem block;
    block.setup(level, &text, nullptr);
    // Its height is five of its lines (size 1.2), each a glyph's height and the (negative) line gap.
    const coney::graphics::FontMetrics line = coney::graphics::fontMetrics(1.2F);
    const coney::graphics::FontMetrics base = coney::graphics::fontMetrics(1.0F);
    const float advance = line.height + line.lineGap;
    CHECK(block.fullHeight() == Approx(5.0F * advance));
    // The heights of the text's sprites with `reveal` of the block shown.
    const auto heights = [&](float reveal) {
        batch.clear();
        block.moveTo(0.2F, 0.1F, reveal);
        block.render(canvas);
        std::vector<float> out;
        for (const coney::graphics::Sprite& sprite : batch.sprites()) {
            out.push_back(sprite.height);
        }
        return out;
    };
    // Whole: every glyph whole.
    const std::vector<float> whole = heights(1.0F);
    REQUIRE_FALSE(whole.empty());
    for (const float height : whole) {
        CHECK(height == Approx(line.height));
    }
    // The picture is as high as the shown block, its rectangle cut to the same share.
    (void)heights(0.5F);
    CHECK(block.picture().setupValues().height == Approx(2.5F * advance));
    CHECK(block.picture().setupValues().rectShare == 0.5F);
    // At 0.9 the time line (the fifth) shows (depth x 0.9 - 4 lines) / h of each glyph, depth the text's height less a
    // size-1.0 line gap; the score line shows whole.
    const float depth = (5.0F * advance) - base.lineGap;
    const float timeShare = ((depth * 0.9F) - (4.0F * advance)) / line.height;
    REQUIRE(timeShare > 0.0F);
    REQUIRE(timeShare < 1.0F);
    const std::vector<float> most = heights(0.9F);
    CHECK(most.size() == whole.size());
    std::size_t cut = 0;
    for (const float height : most) {
        if (height == Approx(line.height * timeShare)) {
            ++cut;
        } else {
            CHECK(height == Approx(line.height));
        }
    }
    CHECK(cut > 0);
    CHECK(cut < whole.size());
    // At 0.5 the last two lines are gone; at 0.1 only the top of the score line shows.
    CHECK(heights(0.5F).size() < whole.size());
    for (const float height : heights(0.1F)) {
        CHECK(height == Approx(depth * 0.1F));
    }
    CHECK(heights(0.0F).empty());
}
