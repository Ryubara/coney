// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/text_layout.h"

#include <cstddef>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/sprite_batch.h"
#include "support/font_fixtures.h"

using Catch::Approx;
using coney::graphics::Font;
using coney::graphics::fontMetrics;
using coney::graphics::FontMetrics;
using coney::graphics::OverlayCamera;
using coney::graphics::Rgba;
using coney::gui::FontLookup;
using coney::gui::kBigFontSlot;
using coney::gui::kTextFontSlot;
using coney::gui::layoutText;
using coney::gui::TextAlignment;
using coney::gui::TextLayout;
using coney::gui::TextStyle;

namespace {

// The default style with the text starting in font slot `slot`.
TextStyle inSlot(int slot) {
    TextStyle style;
    style.fontSlot = slot;
    return style;
}

// The test font in the big_font slot (6) and, to tell them apart, slot 2; nothing elsewhere.
struct Fonts {
    Font font = coney::test::testFont();
    FontLookup lookup = [this](int slot) -> const Font* { return slot == kBigFontSlot || slot == 2 ? &font : nullptr; };
};

// The GUI y of a sprite's centre.
float guiY(const coney::graphics::Sprite& sprite) { return 0.5F - sprite.position.y; }

// The GUI x of a sprite's centre.
float guiX(const coney::graphics::Sprite& sprite) {
    return sprite.position.x / OverlayCamera::guiWidthToOverlay(1.0F) + 0.5F;
}

} // namespace

TEST_CASE("plain text is one line of sprites from the box's left edge", "[text_layout]") {
    const Fonts fonts;
    TextStyle style;
    style.x = 0.1F;
    style.y = 0.2F;
    const TextLayout layout = layoutText("ab", style, fonts.lookup);
    REQUIRE(layout.sprites.size() == 2);
    CHECK(layout.lines == 1);
    const FontMetrics m = fontMetrics(1.0F);
    CHECK(layout.width == Approx(2 * (m.width / 2 + m.spacing)));
    CHECK(guiX(layout.sprites[0].sprite) == Approx(0.1F + (m.width / 2 + m.spacing) / 2));
    CHECK(guiY(layout.sprites[0].sprite) == Approx(0.2F));
    CHECK(layout.sprites[0].fontSlot == kTextFontSlot);
}

TEST_CASE("<CR> moves down by h plus the line gap, and <CR3 f> adds f", "[text_layout]") {
    const Fonts fonts;
    const FontMetrics m = fontMetrics(1.0F);
    const TextLayout layout = layoutText("a<CR>b<CR3 0.1>c", TextStyle{}, fonts.lookup);
    REQUIRE(layout.sprites.size() == 3);
    CHECK(layout.lines == 3);
    CHECK(guiY(layout.sprites[1].sprite) == Approx(m.height + m.lineGap));
    CHECK(guiY(layout.sprites[2].sprite) == Approx(2 * (m.height + m.lineGap) + 0.1F));
    CHECK(guiX(layout.sprites[1].sprite) == Approx(guiX(layout.sprites[0].sprite)));
}

TEST_CASE("<CENTER> and <RIGHT> place each line in the box grown to the widest line", "[text_layout]") {
    const Fonts fonts;
    const FontMetrics m = fontMetrics(1.0F);
    const float narrow = m.width / 2 + m.spacing; // one 8 x 16 glyph
    TextStyle style;
    style.x = 0.2F;
    const TextLayout layout = layoutText("aaaa<CR><CENTER>a<CR><RIGHT>a", style, fonts.lookup);
    REQUIRE(layout.sprites.size() == 6);
    CHECK(layout.width == Approx(4 * narrow));
    CHECK(guiX(layout.sprites[4].sprite) == Approx(0.2F + 1.5F * narrow + narrow / 2));
    CHECK(guiX(layout.sprites[5].sprite) == Approx(0.2F + 3 * narrow + narrow / 2));

    // A wider box than the text: centring is on the box.
    style.boxWidth = 1.0F;
    style.alignment = TextAlignment::Centre;
    const TextLayout boxed = layoutText("a", style, fonts.lookup);
    CHECK(guiX(boxed.sprites[0].sprite) == Approx(0.2F + 0.5F));
}

TEST_CASE("<COLOR>, <SIZE> and their closing tags change and restore the run's look", "[text_layout]") {
    const Fonts fonts;
    TextStyle style;
    style.fade = 0.5F;
    const TextLayout layout =
        layoutText("a<COLOR AA2B2BFF><SIZE 2.0>b</SIZE>c</COLOR>d<COLOR nothex>e", style, fonts.lookup);
    REQUIRE(layout.sprites.size() == 5);
    CHECK(layout.sprites[0].sprite.colour == Rgba{255, 255, 255, 128}); // the fade halves alpha
    CHECK(layout.sprites[1].sprite.colour == Rgba{0xAA, 0x2B, 0x2B, 128});
    CHECK(layout.sprites[1].sprite.height == Approx(fontMetrics(2.0F).height));
    CHECK(layout.sprites[2].sprite.height == Approx(fontMetrics(1.0F).height));
    CHECK(layout.sprites[2].sprite.colour == Rgba{0xAA, 0x2B, 0x2B, 128});
    CHECK(layout.sprites[3].sprite.colour == Rgba{255, 255, 255, 128});
    CHECK(layout.skippedTags == 1); // the malformed colour
}

TEST_CASE("button tags draw their character from the current font", "[text_layout]") {
    const Fonts fonts;
    const TextLayout layout = layoutText("<X><T><BOBJ><BIGFONT>a</BIGFONT>", inSlot(2), fonts.lookup);
    REQUIRE(layout.sprites.size() == 3);
    CHECK(layout.sprites[0].sprite.uv == fonts.font.glyph(0x9e));
    CHECK(layout.sprites[1].sprite.uv == fonts.font.glyph(0x96));
    CHECK(layout.sprites[0].fontSlot == 2);
    CHECK(layout.sprites[2].fontSlot == kBigFontSlot);
    CHECK(layout.skippedTags == 1); // BOBJ
}

TEST_CASE("<DISPLAYTIME> fades the text over its last second and then hides it", "[text_layout]") {
    const Fonts fonts;
    TextStyle style;
    style.timeMs = 1000;
    CHECK(layoutText("<DISPLAYTIME 3000>a", style, fonts.lookup).sprites[0].sprite.colour.a == 255);
    style.timeMs = 2500;
    CHECK(layoutText("<DISPLAYTIME 3000>a", style, fonts.lookup).sprites[0].sprite.colour.a == 128);
    style.timeMs = 3000;
    const TextLayout gone = layoutText("<DISPLAYTIME 3000>a", style, fonts.lookup);
    CHECK(gone.expired);
    CHECK(gone.sprites.empty());
}

TEST_CASE("<SOUND> and <FREEZE> are reported, a shadow doubles the sprites", "[text_layout]") {
    const Fonts fonts;
    TextStyle style;
    style.shadowAlpha = 128;
    const TextLayout layout = layoutText("<SOUND beep>a<FREEZE 500>", style, fonts.lookup);
    REQUIRE(layout.sounds.size() == 1);
    CHECK(layout.sounds[0] == "beep");
    CHECK(layout.freezeMs == 500);
    REQUIRE(layout.sprites.size() == 2);
    CHECK(layout.sprites[0].sprite.colour == Rgba{0, 0, 0, 128});
}

TEST_CASE("addTextSprites fills each slot's batch", "[text_layout]") {
    const Fonts fonts;
    const TextLayout layout = layoutText("ab<BIGFONT>c", inSlot(2), fonts.lookup);
    coney::graphics::SpriteBatch small(fonts.font.sheet(), 1, 9000.0F);
    coney::graphics::SpriteBatch big(fonts.font.sheet(), 8, 9000.0F);
    const std::size_t dropped = coney::gui::addTextSprites(layout, [&](int slot) { return slot == 2 ? &small : &big; });
    CHECK(dropped == 1);
    CHECK(small.sprites().size() == 1);
    CHECK(big.sprites().size() == 1);
}
