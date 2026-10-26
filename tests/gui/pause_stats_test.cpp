// SPDX-License-Identifier: GPL-3.0-or-later
// The pause menu's Stats screen (docs/research/pause.md#stats-screen): who has one, the bars' values by difficulty,
// the layout and the input, with synthetic strings and stats.
#include "gui/pause_menu/pause_stats.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"
#include "gui/bar.h"
#include "gui/base_widget.h"
#include "gui/global_strings.h"
#include "support/font_fixtures.h"
#include "support/recording_device.h"

using Catch::Approx;
using coney::graphics::OverlayCamera;
using coney::gui::GlobalStrings;
using coney::gui::GuiFrame;
using coney::gui::MenuCommand;
using coney::gui::PauseStats;
using coney::gui::WarriorStats;

namespace {

// Every string as "#<hex id>".
GlobalStrings syntheticStrings() {
    GlobalStrings strings;
    for (std::uint32_t id = 0; id < 0x200; ++id) {
        strings.set(id, std::format("#{:x}", id));
    }
    return strings;
}

// A Rembrandt (type 30) with made-up numbers.
WarriorStats rembrandt() {
    return WarriorStats{.characterType = 30,
                        .damageScale = 150,
                        .powerMax = 400,
                        .staminaMax = 200,
                        .rageMax = 78,
                        .powerClassByte40 = 35};
}

} // namespace

TEST_CASE("pause stats: the nine Warriors' types have a screen, with their pictures and text; others none",
          "[pause_stats]") {
    // Expected: pause.md#stats-screen, confirmed (code)
    REQUIRE(PauseStats::portraitOf(9).has_value());
    const coney::gui::StatsPortrait swan = PauseStats::portraitOf(9).value_or(coney::gui::StatsPortrait{});
    CHECK(swan.kind == 0);
    CHECK(swan.nameRecord == 0x31);
    CHECK(swan.faceRecord == 0x197);
    CHECK(swan.description == 0xf2);
    REQUIRE(PauseStats::portraitOf(189).has_value());
    const coney::gui::StatsPortrait cleon = PauseStats::portraitOf(189).value_or(coney::gui::StatsPortrait{});
    CHECK(cleon.kind == 8);
    CHECK(cleon.description == 0xeb);
    CHECK(PauseStats::portraitOf(32).value_or(coney::gui::StatsPortrait{.kind = -1}).kind == 4);
    // Ash (38), Lynx (45), a variant not in the lists (31), a civilian.
    CHECK_FALSE(PauseStats::portraitOf(38));
    CHECK_FALSE(PauseStats::portraitOf(45));
    CHECK_FALSE(PauseStats::portraitOf(31));
    CHECK_FALSE(PauseStats::portraitOf(100));
}

TEST_CASE("pause stats: the bars from the difficulty's row, Strength unclamped and the others at most 1",
          "[pause_stats]") {
    // Expected: pause.md#stats-screen, confirmed (code)
    // Difficulty 1: t0 75, t1 192, t2 260, t3 600, t4 25, t5 160.
    const auto normal = PauseStats::barValues(rembrandt(), 1);
    CHECK(normal[0] == Approx((150.0F - 75.0F) / (192.0F - 75.0F)));
    // (power 400 + stamina 200 − 260 + 25) / (600 + 160 − 260 + 25).
    CHECK(normal[1] == Approx(365.0F / 525.0F));
    CHECK(normal[2] == Approx(0.35F));
    CHECK(normal[3] == Approx(78.0F / 150.0F));
    // Difficulty 0: t2 460, t3 715; a strong Warrior goes past 1 on Strength.
    WarriorStats strong = rembrandt();
    strong.damageScale = 300;
    strong.rageMax = 400;
    strong.powerClassByte40 = 250;
    const auto easy = PauseStats::barValues(strong, 0);
    CHECK(easy[0] == Approx((300.0F - 110.0F) / (228.0F - 110.0F)));
    CHECK(easy[0] > 1.0F);
    CHECK(easy[1] == Approx((600.0F - 460.0F + 25.0F) / (715.0F + 160.0F - 460.0F + 25.0F)));
    CHECK(easy[2] == 1.0F);
    CHECK(easy[3] == 1.0F);
    // Above 2 reads row 2.
    CHECK(PauseStats::barValues(rembrandt(), 3) == PauseStats::barValues(rembrandt(), 2));
}

TEST_CASE("pause stats: set up for a Warrior, placed from the base point, drawn with the labels and description",
          "[pause_stats]") {
    // Expected: pause.md#stats-screen, confirmed (code)
    const GlobalStrings strings = syntheticStrings();
    PauseStats stats;
    CHECK_FALSE(stats.setup(std::nullopt, 1, &strings, false));
    WarriorStats ash = rembrandt();
    ash.characterType = 38;
    CHECK_FALSE(stats.setup(ash, 1, &strings, false));
    CHECK(stats.lines().empty());

    REQUIRE(stats.setup(rembrandt(), 1, &strings, false));
    stats.update(GuiFrame{0, nullptr});
    CHECK(stats.lines() == std::vector<std::string>{"#f3", "#f4", "#f5", "#f6", "#f0"});
    CHECK(stats.name().setupValues().x == Approx(0.42F));
    CHECK(stats.name().setupValues().y == Approx(0.3F));
    CHECK(stats.name().setupValues().colour == PauseStats::kPortraitTint);
    CHECK(stats.shadow(0).setupValues().x == Approx(0.4225F));
    CHECK(stats.shadow(1).setupValues().y == Approx(0.298F));
    // Setup gives the shadows alpha 128, but each draw sets every part's alpha to the menu's, so they are opaque.
    CHECK(stats.shadow(0).setupValues().colour == coney::graphics::Rgba{0, 0, 0, 255});
    CHECK(stats.face().setupValues().x == Approx(0.33F));
    CHECK(stats.face().setupValues().y == Approx(0.401F));
    CHECK(stats.bar(0).setupValues().x == Approx(0.64F));
    CHECK(stats.bar(3).setupValues().y == Approx(0.54F));
    CHECK(stats.bar(2).fill() == Approx(0.35F));
    // The labels: their anchor (0.38, 0.42 + 0.05 i) plus the bars' label offset (0.04, -0.035).
    CHECK(stats.label(0).style().x == Approx(0.42F));
    CHECK(stats.label(0).style().y == Approx(0.385F));
    CHECK(stats.label(1).style().y == Approx(0.435F));
    CHECK(stats.label(3).style().x == Approx(0.42F));
    CHECK(stats.label(3).style().y == Approx(0.535F));
    CHECK(stats.label(0).style().alignment == coney::gui::TextAlignment::Left);
    CHECK(stats.description().style().x == Approx(0.26F));
    CHECK(stats.description().style().y == Approx(0.6F));
    CHECK(stats.description().style().wrapWidth == Approx(0.55F));
    CHECK(stats.firstLine() == 0);
    CHECK(stats.lastLine() == 5);

    // The menu's alpha on every part, and setup puts it back to full.
    stats.setFade(0.5F);
    stats.update(GuiFrame{0, nullptr});
    CHECK(stats.shadow(1).setupValues().colour.a == 127);
    CHECK(stats.name().setupValues().colour.a == 127);
    REQUIRE(stats.setup(rembrandt(), 1, &strings, false));
    stats.update(GuiFrame{0, nullptr});
    CHECK(stats.shadow(1).setupValues().colour.a == 255);

    // 16:9 moves only the base point's y, to 0.45.
    REQUIRE(stats.setup(rembrandt(), 1, &strings, true));
    stats.update(GuiFrame{0, nullptr});
    CHECK(stats.bar(0).setupValues().y == Approx(0.34F));
    CHECK(stats.name().setupValues().x == Approx(0.42F));
}

TEST_CASE("pause stats: an unavailable screen is not placed", "[pause_stats]") {
    // Expected: pause.md#stats-screen, confirmed (code)
    PauseStats stats;
    WarriorStats ash = rembrandt();
    ash.characterType = 38;
    REQUIRE_FALSE(stats.setup(ash, 1, nullptr, false));
    stats.update(GuiFrame{0, nullptr});
    CHECK(stats.name().setupValues().x == Approx(coney::gui::BaseWidgetSetup{}.x));
    CHECK(stats.bar(0).setupValues().x == Approx(coney::gui::BarSetup{}.x));
}

TEST_CASE("pause stats: the description wraps at its leading AUTOINDENT width, else 0.55", "[pause_stats]") {
    // Expected: pause.md#stats-description, confirmed (code)
    GlobalStrings strings = syntheticStrings();
    PauseStats stats;
    strings.set(0xf0, "<AUTOINDENT 0.4><COLOR B2B2B2FF>Some words");
    REQUIRE(stats.setup(rembrandt(), 1, &strings, false));
    CHECK(stats.description().style().wrapWidth == Approx(0.4F));
    // Only at the very start: a later tag, or none, leaves 0.55.
    strings.set(0xf0, "Some <AUTOINDENT 0.4>words");
    REQUIRE(stats.setup(rembrandt(), 1, &strings, false));
    CHECK(stats.description().style().wrapWidth == Approx(0.55F));
}

TEST_CASE("pause stats: each bar draws the rage meter's left cap, body, fill and right cap", "[pause_stats]") {
    // Expected: hud.md#the-rage-meter, confirmed (code)
    // A part_page0 stand-in: 58 rectangles over a 256 x 256 texture; 56 and 57 are 8 texels wide, so each cap is
    // 8 / 256 = 0.03125 overlay units wide (a cap is as wide as its rectangle's u span).
    coney::graphics::SpriteSheet sheet;
    sheet.page.rects.assign(58, coney::graphics::UvRect{0.0F, 0.0F, 0.25F, 0.0625F});
    sheet.page.rects.at(56) = coney::graphics::UvRect{0.0F, 0.0F, 8.0F / 256.0F, 16.0F / 256.0F};
    sheet.page.rects.at(57) = coney::graphics::UvRect{0.5F, 0.0F, 0.5F + (8.0F / 256.0F), 16.0F / 256.0F};
    sheet.texture = std::make_shared<coney::test::FakeTexture>(256, 256);
    coney::graphics::SpriteBatch bars(sheet, 64, 8500.0F);
    PauseStats stats;
    stats.setBatches(nullptr, nullptr, &bars);
    REQUIRE(stats.setup(rembrandt(), 1, nullptr, false));
    stats.update(GuiFrame{0, nullptr});
    stats.render({});
    // Four bars of four sprites, each in the meter's order.
    REQUIRE(bars.sprites().size() == 16);
    const auto& leftCap = bars.sprites()[0];
    const auto& body = bars.sprites()[1];
    const auto& fill = bars.sprites()[2];
    const auto& rightCap = bars.sprites()[3];
    constexpr float kCap = 8.0F / 256.0F;
    CHECK(leftCap.uv.u1 == Approx(kCap));
    CHECK(leftCap.width == Approx(kCap));
    CHECK(leftCap.colour == PauseStats::kBarBack);
    // The body over the inner width (0.22 less the left cap), inset one texel left and right only.
    CHECK(body.width == Approx(0.22F - kCap));
    CHECK(body.colour == PauseStats::kBarBack);
    CHECK(body.uv.u0 == Approx(1.0F / 256.0F));
    CHECK(body.uv.u1 == Approx(0.25F - (1.0F / 256.0F)));
    CHECK(body.uv.v0 == Approx(0.0F));
    CHECK(body.uv.v1 == Approx(0.0625F));
    CHECK(fill.width == Approx((0.22F - kCap) * stats.bar(0).fill()));
    CHECK(fill.colour == PauseStats::kBarFill);
    CHECK(rightCap.uv.u0 == Approx(0.5F));
    CHECK(rightCap.width == Approx(kCap));
    // From the bar's left edge P (0.64, 0.39): the left cap over P to P + c, the body centred on P + W / 2, the fill
    // from P + c / 2, the right cap from P + W - c / 2. Widths are overlay units, GUI widths x 640 / 448.
    const auto gui = [](float overlayWidth) { return overlayWidth * 448.0F / 640.0F; };
    const auto at = [](float x) { return OverlayCamera::guiToOverlay(x, 0.39F); };
    const float filled = (0.22F - kCap) * stats.bar(0).fill();
    CHECK(leftCap.position.x == Approx(at(0.64F + (gui(kCap) / 2.0F)).x));
    CHECK(leftCap.position.y == Approx(at(0.0F).y));
    CHECK(body.position.x == Approx(at(0.64F + (gui(0.22F) / 2.0F)).x));
    CHECK(fill.position.x == Approx(at(0.64F + (gui(kCap) / 2.0F) + (gui(filled) / 2.0F)).x));
    CHECK(rightCap.position.x == Approx(at(0.64F + gui(0.22F)).x));
}

TEST_CASE("pause stats: up and down move the description's six-line window one line, within the text",
          "[pause_stats]") {
    // Expected: pause.md#stats-description, confirmed (code)
    coney::gui::GlobalStrings strings = syntheticStrings();
    const coney::graphics::Font font = coney::test::testFont();
    coney::graphics::SpriteBatch text(font.sheet(), 4096, 9000.0F);
    const coney::gui::GuiCanvas canvas{.fonts = [&font](int /*slot*/) { return &font; },
                                       .textBatch = [&text](int /*slot*/) { return &text; }};
    // Rembrandt's description, sixteen words wrapped two to a line into eight lines: its `<AUTOINDENT>` width fits
    // " L0 L1" (the first line starts with the space after the tag's first word) but no three words.
    coney::gui::TextStyle measured;
    measured.fontSlot = PauseStats::kTextFontSlot;
    const float twoWords = coney::gui::layoutText(" L0 L1", measured, canvas.fonts).width;
    strings.set(0xf0,
                std::format("<AUTOINDENT {:.6f}>L0 L1 L2 L3 L4 L5 L6 L7 L8 L9 LA LB LC LD LE LF", twoWords * 1.05F));
    PauseStats stats;
    std::vector<int> cues;
    stats.setSoundSink([&cues](int cue) { cues.push_back(cue); });
    REQUIRE(stats.setup(rembrandt(), 1, &strings, false));
    stats.update(GuiFrame{0, nullptr});
    stats.render(canvas);
    // Lines 0-5 drawn, each "Lx Ly ", and the break that ends line 5 (an empty line after it).
    coney::gui::TextStyle oneLine = stats.description().style();
    oneLine.wrapWidth = 0.0F;
    const std::size_t lineSprites = coney::gui::layoutText("L0 L1 ", oneLine, canvas.fonts).sprites.size();
    REQUIRE(lineSprites > 0);
    const coney::gui::TextLayout page = stats.descriptionLayout(canvas);
    CHECK(page.sprites.size() == 6 * lineSprites);
    CHECK(page.lines == 7);
    // Up at the top refuses; down moves twice, until line 7 is the bottom line, then refuses.
    CHECK_FALSE(stats.handle(MenuCommand::Up));
    CHECK_FALSE(stats.handle(MenuCommand::Down));
    CHECK_FALSE(stats.handle(MenuCommand::Down));
    CHECK_FALSE(stats.handle(MenuCommand::Down));
    CHECK(stats.firstLine() == 2);
    CHECK(stats.lastLine() == 7);
    CHECK_FALSE(stats.handle(MenuCommand::Up));
    CHECK(stats.firstLine() == 1);
    CHECK(cues == std::vector<int>{0xe, 4, 4, 0xe, 4});
    // A short text never moves.
    strings.set(0xf0, "One line");
    REQUIRE(stats.setup(rembrandt(), 1, &strings, false));
    stats.measure(canvas);
    cues.clear();
    CHECK_FALSE(stats.handle(MenuCommand::Down));
    CHECK_FALSE(stats.handle(MenuCommand::Up));
    CHECK(cues == std::vector<int>{0xe, 0xe});
}

TEST_CASE("pause stats: directions refuse, back closes", "[pause_stats]") {
    // Expected: pause.md#stats-screen, confirmed (code)
    PauseStats stats;
    std::vector<int> cues;
    stats.setSoundSink([&cues](int cue) { cues.push_back(cue); });
    REQUIRE(stats.setup(rembrandt(), 1, nullptr, false));
    CHECK_FALSE(stats.handle(MenuCommand::Up));
    CHECK_FALSE(stats.handle(MenuCommand::Down));
    CHECK_FALSE(stats.handle(MenuCommand::Left));
    CHECK_FALSE(stats.handle(MenuCommand::Accept));
    CHECK(stats.handle(MenuCommand::Back));
    CHECK(cues == std::vector<int>{0xe, 0xe, 0xe, 0xf});
}
