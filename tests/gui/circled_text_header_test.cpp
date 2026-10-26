// SPDX-License-Identifier: GPL-3.0-or-later
// The pause header (docs/research/circled-text.md#pause-header): how a title splits into circles, the colours and the
// layout per video mode; and the smaller CircledText of the statistics screens
// (docs/research/circled-text.md#circled-text): its layout, reveal and colours. Over a synthetic font and sheet.
// Each new case here failed first on the unit's stubbed source (coney-tools original fails-first).
#include "gui/circled_text_header.h"

#include <cstddef>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "graphics/sprite_batch.h"
#include "gui/circled_text.h"
#include "gui/colour_table.h"
#include "support/font_fixtures.h"

using Catch::Approx;
using coney::graphics::Rgba;
using coney::graphics::SpriteBatch;
using coney::gui::CircledHeaderFactors;
using coney::gui::CircledText;
using coney::gui::CircledTextHeader;
using coney::gui::CircledTextHeaderSetup;
using coney::gui::CircledTextSetup;
using coney::gui::GuiCanvas;
using coney::gui::GuiFrame;

namespace {

// The synthetic font in every slot, and a batch over a 256-rectangle sheet standing for `part_page0`.
struct HeaderCanvas {
    coney::graphics::Font font = coney::test::testFont();
    SpriteBatch text{font.sheet(), 4096, 9000.0F};
    SpriteBatch sprites{coney::test::testFontSheet(), 16, CircledTextHeader::kCircleDepth};
    GuiCanvas canvas{
        .fonts = [this](int /*slot*/) { return &font; },
        .textBatch = [this](int /*slot*/) { return &text; },
    };
};

// y0 of the layout at row `y`: Y − h / 2 + the title's line height + oy − 0.196.
float y0At(float y) {
    const coney::graphics::FontMetrics title = coney::graphics::fontMetrics(CircledTextHeader::kTitleSize);
    return y - (0.007F / 2.0F) + (title.height + title.lineGap) - 0.01F - 0.196F;
}

} // namespace

TEST_CASE("pause header: the characters before ':' are circled, the ':' adds the checkpoint's letter",
          "[circled_text_header]") {
    // Expected: circled-text.md#header-title, confirmed (code)
    CircledTextHeader header;
    header.setTitle("1:Coney", 2, "New Blood");
    REQUIRE(header.circles() == 2);
    CHECK(header.letter(0).text() == "1");
    CHECK(header.letter(1).text() == "B");
    CHECK(header.title().text() == "Coney");
    CHECK(header.subtitle().text() == "New Blood");

    // Without a ':' every character is a circle's and the title is empty; a leading ':' adds no circle; more than
    // three circles are dropped.
    header.setTitle("Rumble", 1, "");
    CHECK(header.circles() == 3);
    CHECK(header.title().text().empty());
    header.setTitle(":Title", 3, "");
    CHECK(header.circles() == 0);
    CHECK(header.title().text() == "Title");
    header.setTitle("123:X", 1, "");
    CHECK(header.circles() == 3);
    CHECK(header.letter(2).text() == "3");

    // Every later ':' adds another checkpoint circle and is left out of the title.
    header.setTitle("1:Co:ney", 2, "");
    REQUIRE(header.circles() == 3);
    CHECK(header.letter(2).text() == "B");
    CHECK(header.title().text() == "Coney");
    header.setTitle("::X", 3, "");
    REQUIRE(header.circles() == 1);
    CHECK(header.letter(0).text() == "C");
    CHECK(header.title().text() == "X");
}

TEST_CASE("pause header: the checkpoint's letter is grey when set, and black from the next update",
          "[circled_text_header]") {
    // Expected: circled-text.md#header-title, confirmed (code)
    CircledTextHeader header;
    header.setTitle("1:Coney", 2, "New Blood");
    CHECK(header.letter(1).style().colour == coney::gui::kMenuGrey);
    header.update(GuiFrame{0, nullptr});
    CHECK(header.letter(0).style().colour == coney::graphics::kBlack);
    CHECK(header.letter(1).style().colour == coney::graphics::kBlack);
}

TEST_CASE("pause header: the circles take the colour given, the last turns red, and the characters stay black",
          "[circled_text_header]") {
    // Expected: circled-text.md#header-colours, confirmed (code)
    HeaderCanvas fixture;
    CircledTextHeader header;
    header.setBatch(&fixture.sprites);
    const Rgba gold{186, 139, 53, 255};
    header.setCircleColour(gold);
    header.setTitle("1:Coney", 1, "New Blood");
    header.setLetterColour(Rgba{255, 255, 255, 255});
    header.update(GuiFrame{0, nullptr});
    header.render(fixture.canvas);
    CHECK(header.circle(0).colour() == gold);
    CHECK(header.circle(1).colour() == coney::gui::kMenuRed);
    CHECK(header.letter(0).style().colour == coney::graphics::kBlack);
    // Two circles and rule B: rule A is never drawn.
    CHECK(fixture.sprites.sprites().size() == 3);
    // The label is hidden until shown.
    CHECK_FALSE(header.label().visible());
}

TEST_CASE("pause header: the default layout puts the title at 0.68 of y0, after circles 0.051 apart",
          "[circled_text_header]") {
    // Expected: circled-text.md#header-layout, confirmed (code)
    HeaderCanvas fixture;
    CircledTextHeader header;
    header.setup(CircledTextHeaderSetup{});
    header.setTitle("1:Coney", 2, "New Blood");
    header.render(fixture.canvas);

    const float y0 = y0At(0.22F);
    CHECK(y0 == Approx(0.102F).margin(0.001F));
    CHECK(header.rule().setupValues().x == Approx(0.535F));
    CHECK(header.rule().setupValues().y == Approx(0.135F));
    // The rule's GUI width 0.57 goes through the device's conversion (× 640 / 448) into overlay units; checked at
    // runtime: the original's rule spans 0.506 of a 640-pixel screenshot.
    CHECK(header.rule().setupValues().width.value_or(0.0F) == Approx(0.57F * 640.0F / 448.0F));
    CHECK(header.subtitle().style().x == Approx(0.257F));
    CHECK(header.subtitle().style().y == Approx((y0 + 0.075F) * 0.95F));
    CHECK(header.letter(0).style().x == Approx(0.272F));
    CHECK(header.letter(1).style().x == Approx(0.323F));
    CHECK(header.letter(0).style().y == Approx(y0 * 0.68F));
    // Each circle is centred on its character, 0.082 × 1.2 tall, at y0 × 0.78.
    const float width = header.letter(0).width(fixture.canvas);
    CHECK(header.circle(0).setupValues().x == Approx(0.272F + (width / 2.0F)));
    CHECK(header.circle(0).setupValues().y == Approx(y0 * 0.78F));
    CHECK(header.circle(0).setupValues().height == Approx(0.0984F));
    // Its width follows the rectangle's shape: the widget's update refits it after the size is set.
    CHECK_FALSE(header.circle(0).setupValues().width.has_value());
    CHECK(header.title().style().x == Approx(0.374F));
    CHECK(header.title().style().y == Approx(y0 * 0.68F));

    // Without circles the title starts at 0.255.
    header.setTitle(":Rumble", 1, "");
    header.render(fixture.canvas);
    CHECK(header.title().style().x == Approx(0.255F));
}

TEST_CASE("pause header: the video modes' factors", "[circled_text_header]") {
    // Expected: circled-text.md#header-factors, confirmed (code)
    const CircledHeaderFactors wide = CircledHeaderFactors::forMode(true, false);
    CHECK(wide.ruleY == Approx(0.6F));
    CHECK(wide.letterY == 0.0F);
    CHECK(wide.circleScale == Approx(1.2F));
    const CircledHeaderFactors low = CircledHeaderFactors::forMode(false, true);
    CHECK(low.subtitleY == Approx(1.63F));
    CHECK(low.circleScale == Approx(0.84F));
    const CircledHeaderFactors lowWide = CircledHeaderFactors::forMode(true, true);
    CHECK(lowWide.circleY == Approx(1.4F));
    CHECK(lowWide.labelY == Approx(1.1F));

    // The label sits in its box: X − W / 2 + ox − 0.005.
    HeaderCanvas fixture;
    CircledTextHeader header;
    header.setup(CircledTextHeaderSetup{.y = 0.18F, .factors = wide});
    header.setLabel("Current Objectives");
    header.setLabelVisible(true);
    header.render(fixture.canvas);
    CHECK(header.label().style().x == Approx(0.255F));
    CHECK(header.rule().setupValues().y == Approx((0.18F - 0.07F) * 0.6F));
}

namespace {

// Sets `header` up in the statistics screen's shape: left edge 0.2, row 0.5, two circles.
void setUpStatsHeader(CircledText& header, HeaderCanvas& fixture) {
    header.setBack(&fixture.sprites, 2);
    header.setCircleSprite(&fixture.sprites, 2);
    header.setup(CircledTextSetup{
        .x = 0.2F, .y = 0.5F, .width = 0.6F, .height = 0.09F, .circleWidth = 0.065F, .circleHeight = 0.11F});
    header.setText("Coney");
    header.setCircled("12");
}

} // namespace

TEST_CASE("circled text: each circle adds a third of its width before and after itself, the text follows the last",
          "[circled_text]") {
    // Expected: circled-text.md#ct-layout, confirmed (code)
    HeaderCanvas fixture;
    CircledText header;
    setUpStatsHeader(header, fixture);
    header.update(GuiFrame{0, nullptr});
    const float step = 0.065F / 3.0F;
    CHECK(header.circle(0).setupValues().x == Approx(0.2F + step));
    CHECK(header.circle(1).setupValues().x == Approx(0.2F + (3.0F * step)));
    CHECK(header.character(1).style().x == Approx(0.2F + (3.0F * step)));
    CHECK(header.text().style().x == Approx(0.2F + (3.0F * step) + 0.0325F));
    // The circles sit a tenth of their height below the row, the characters and the text on it.
    CHECK(header.circle(0).setupValues().y == Approx(0.5F + 0.011F));
    CHECK(header.character(0).style().y == Approx(0.5F));
    CHECK(header.text().style().y == Approx(0.5F));
    // The circle's width is given as it is (no conversion, no refit); the back's goes through the conversion.
    CHECK(header.circle(0).setupValues().width.value_or(0.0F) == Approx(0.065F));
    CHECK(header.circle(0).setupValues().height == Approx(0.11F));
    CHECK(header.back().setupValues().width.value_or(0.0F) == Approx(0.6F * 640.0F / 448.0F));
    CHECK(header.back().setupValues().y == Approx(0.5F));

    // Without circles the text starts at the left edge + the circle offset.
    CircledText plain;
    plain.setup(CircledTextSetup{.x = 0.3F, .circleOffset = 0.02F});
    plain.update(GuiFrame{0, nullptr});
    CHECK(plain.text().style().x == Approx(0.32F));
}

TEST_CASE("circled text: the reveal shrinks the back and the circles from the bottom, their top edges kept",
          "[circled_text]") {
    // Expected: circled-text.md#ct-layout, confirmed (code)
    HeaderCanvas fixture;
    CircledText header;
    setUpStatsHeader(header, fixture);
    header.setReveal(0.5F);
    header.update(GuiFrame{0, nullptr});
    CHECK(header.height() == Approx(0.045F));
    CHECK(header.back().setupValues().height == Approx(0.045F));
    CHECK(header.back().setupValues().y == Approx(0.5F - 0.0225F));
    CHECK(header.circle(0).setupValues().height == Approx(0.055F));
    CHECK(header.circle(0).setupValues().y == Approx(0.5F - 0.0275F + 0.011F));

    // The reveal is clamped to 0-1; anything not at least 0 is 0.
    header.setReveal(2.0F);
    CHECK(header.height() == Approx(0.09F));
    header.setReveal(-1.0F);
    CHECK(header.height() == 0.0F);
    header.setReveal(std::numeric_limits<float>::quiet_NaN());
    CHECK(header.height() == 0.0F);
}

TEST_CASE("circled text: the characters take the focused colour when drawn unless black; set-up counts once",
          "[circled_text]") {
    // Expected: circled-text.md#ct-drawing, confirmed (code)
    HeaderCanvas fixture;
    CircledText header;
    setUpStatsHeader(header, fixture);
    CHECK(header.character(0).style().colour == coney::gui::kDimGrey);
    CHECK(header.character(0).style().shadowAlpha == 0);
    header.update(GuiFrame{0, nullptr});
    header.render(fixture.canvas);
    CHECK(header.character(0).style().colour == coney::gui::kSelectedGrey);
    CHECK(header.text().style().colour == coney::gui::kDimGrey);
    header.setFocused(true);
    header.setCharColour(coney::graphics::kBlack);
    header.render(fixture.canvas);
    CHECK(header.character(0).style().colour == coney::graphics::kBlack);
    CHECK(header.text().style().colour == coney::gui::kSelectedGrey);

    // A second set-up changes nothing.
    header.setup(CircledTextSetup{.x = 0.9F});
    header.update(GuiFrame{0, nullptr});
    CHECK(header.circle(0).setupValues().x == Approx(0.2F + (0.065F / 3.0F)));
}
