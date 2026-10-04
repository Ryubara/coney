// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/font.h"

#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "graphics/overlay_camera.h"
#include "support/font_fixtures.h"

using Catch::Approx;
using coney::graphics::Font;
using coney::graphics::fontMetrics;
using coney::graphics::FontMetrics;
using coney::graphics::kFontCentred;
using coney::graphics::kFontProportional;
using coney::graphics::kFontRightAligned;
using coney::graphics::OverlayCamera;
using coney::graphics::Rgba;
using coney::graphics::Sprite;
using coney::test::testFont;
using coney::test::testFontSheet;

TEST_CASE("font metrics follow Font_Size", "[font]") {
    const FontMetrics metrics = fontMetrics(1.5F);
    CHECK(metrics.width == Approx(0.05));
    CHECK(metrics.height == Approx(0.05 * 640.0 / 448.0 * 0.75 * 1.3333));
    CHECK(metrics.spacing == Approx(0.003));
    CHECK(metrics.lineGap == Approx(-metrics.height / 7.0));
}

TEST_CASE("a sheet that is not a font is refused", "[font]") {
    coney::graphics::SpriteSheet sheet = testFontSheet();
    sheet.page.firstGlyph = -1;
    auto font = Font::fromSheet(sheet);
    REQUIRE(!font.has_value());
    CHECK(font.error().code == coney::ErrorCode::Invalid);
}

TEST_CASE("glyph widths and advances follow Font_Measure", "[font]") {
    const Font font = testFont();
    const FontMetrics m = fontMetrics(1.0F);
    // Fixed width: every glyph is w wide.
    CHECK(font.glyphWidth('a', m, false) == Approx(m.width));
    CHECK(font.advance('W', m, false) == Approx(m.width + m.spacing));
    // Proportional: an 8 x 16 glyph is half as wide as w, a 16 x 16 one w.
    CHECK(font.glyphWidth('a', m, true) == Approx(m.width / 2));
    CHECK(font.glyphWidth('W', m, true) == Approx(m.width));
    // A space is a gap of 0.56 glyphs, halved when proportional, and has no width of its own.
    CHECK(font.advance(' ', m, false) == Approx((m.width + m.spacing) * 0.56));
    CHECK(font.advance(' ', m, true) == Approx((m.width + m.spacing) * 0.28));
    CHECK(font.advance(0xac, m, true) == Approx((m.width + m.spacing) * 0.28));
    CHECK(font.glyphWidth(' ', m, true) == 0.0F);
    CHECK(font.measure("aW a", m, kFontProportional) ==
          Approx(m.width / 2 + m.width + 2 * m.spacing + (m.width + m.spacing) * 0.28 + m.width / 2 + m.spacing));
}

TEST_CASE("a character past the sheet's last rectangle takes no room", "[font]") {
    coney::graphics::SpriteSheet sheet = testFontSheet();
    sheet.page.rects.resize(100);
    const Font font = Font::fromSheet(sheet).value();
    CHECK(!font.glyph(0xa0).has_value());
    CHECK(font.advance(0xa0, fontMetrics(1.0F), true) == 0.0F);
}

TEST_CASE("Font_Draw makes one sprite per glyph, centred on the pen and the baseline", "[font]") {
    const Font font = testFont();
    const FontMetrics m = fontMetrics(1.0F);
    std::vector<Sprite> sprites;
    font.draw(sprites, "a b", 0.25F, 0.5F, m, kFontProportional, Rgba{10, 20, 30, 255});
    REQUIRE(sprites.size() == 2); // the space draws nothing
    const float first = 0.25F + (m.width / 2 + m.spacing) / 2;
    CHECK(sprites[0].position.x == Approx(OverlayCamera::guiToOverlay(first, 0.5F).x));
    CHECK(sprites[0].position.y == Approx(OverlayCamera::guiToOverlay(first, 0.5F).y));
    CHECK(sprites[0].width == Approx(OverlayCamera::guiWidthToOverlay(m.width / 2)));
    CHECK(sprites[0].height == Approx(m.height));
    CHECK(sprites[0].uv == font.glyph('a'));
    CHECK(sprites[0].colour == Rgba{10, 20, 30, 255});
    CHECK(sprites[1].uv == font.glyph('b'));
}

TEST_CASE("Font_Draw aligns by the measured width and puts each shadow first", "[font]") {
    const Font font = testFont();
    const FontMetrics m = fontMetrics(1.0F);
    const float width = font.measure("ab", m, 0);
    std::vector<Sprite> left;
    std::vector<Sprite> centred;
    std::vector<Sprite> right;
    font.draw(left, "ab", 0.5F, 0.5F, m, 0, coney::graphics::kWhite);
    font.draw(centred, "ab", 0.5F, 0.5F, m, kFontCentred, coney::graphics::kWhite);
    font.draw(right, "ab", 0.5F, 0.5F, m, kFontRightAligned, coney::graphics::kWhite);
    const float scale = OverlayCamera::guiWidthToOverlay(1.0F);
    CHECK(left[0].position.x - centred[0].position.x == Approx(width / 2 * scale));
    CHECK(left[0].position.x - right[0].position.x == Approx((width + m.width / 2) * scale));

    std::vector<Sprite> shadowed;
    font.draw(shadowed, "a", 0.5F, 0.5F, m, 0, Rgba{255, 255, 255, 128}, 255);
    REQUIRE(shadowed.size() == 2);
    CHECK(shadowed[0].colour == Rgba{0, 0, 0, 128});
    CHECK(shadowed[0].position.x - shadowed[1].position.x ==
          Approx(OverlayCamera::guiWidthToOverlay(coney::graphics::kShadowOffsetX)));
    CHECK(shadowed[1].position.y - shadowed[0].position.y == Approx(coney::graphics::kShadowOffsetY));
}
