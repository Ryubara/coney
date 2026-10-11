// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: `big_font` and `part_page0` load as fonts, the latter with a glyph for every
// button and icon tag, and every English HUD string lays out with them. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise. It prints counts only, never text (LEGAL.md).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/language.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "graphics/font.h"
#include "gui/global_strings.h"
#include "gui/markup.h"
#include "gui/text_layout.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "scripting/config_strings.h"

TEST_CASE("the text fonts load with every glyph the tags use and lay out every HUD string", "[disc][text]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    if (!wad) {
        return;
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(table);
    coney::platform::addSpriteSheetHandlers(table);

    // The two fonts text uses (docs/research/gui.md#text): big_font (first glyph 0, 262 rectangles) for <BIGFONT>, and
    // part_page0 (first glyph 94), Coney's text font, which must have a glyph for every glyph tag.
    const auto loadFont = [&](std::string_view name) -> std::optional<coney::graphics::Font> {
        auto sheet = coney::platform::loadSpriteSheetResource(*wad, table, name, false);
        if (!sheet) {
            return std::nullopt;
        }
        auto font = coney::graphics::Font::fromSheet(std::move(*sheet));
        return font ? std::optional<coney::graphics::Font>(std::move(*font)) : std::nullopt;
    };
    const std::optional<coney::graphics::Font> big = loadFont(coney::gui::kBigFontSheet);
    const std::optional<coney::graphics::Font> textFont = loadFont(coney::gui::kTextFontSheet);
    REQUIRE(big.has_value());
    REQUIRE(textFont.has_value());
    if (!big || !textFont) {
        return;
    }
    CHECK(big->sheet().page.firstGlyph == 0);
    CHECK(big->sheet().page.rects.size() == 262);
    CHECK(textFont->sheet().page.firstGlyph == 94);
    std::size_t glyphTags = 0;
    std::size_t inText = 0;
    std::size_t inBig = 0;
    for (int index = 0; index < 66; ++index) {
        const auto character = coney::gui::markupCharacter(static_cast<coney::gui::MarkupTag>(index));
        if (character) {
            ++glyphTags;
            inText += textFont->glyph(*character).has_value() ? 1 : 0;
            inBig += big->glyph(*character).has_value() ? 1 : 0;
        }
    }
    std::printf("fonts: big_font %zu rectangles, part_page0 %zu; glyph tags with a rectangle: %zu and %zu of %zu\n",
                big->sheet().page.rects.size(), textFont->sheet().page.rects.size(), inBig, inText, glyphTags);
    CHECK(inText == glyphTags);

    // Every English HUD string, laid out in the two fonts: counts of sprites, lines and skipped tags.
    coney::gui::GlobalStrings strings;
    REQUIRE(coney::script::loadGlobalStrings(coney::script::wadScriptSource(*wad), coney::Language::English, strings)
                .has_value());
    const coney::gui::FontLookup fonts = [&big, &textFont](int slot) -> const coney::graphics::Font* {
        return slot == coney::gui::kBigFontSlot ? &*big : &*textFont;
    };
    std::size_t laidOut = 0;
    std::size_t sprites = 0;
    std::size_t lines = 0;
    std::size_t skipped = 0;
    std::size_t empty = 0;
    for (const auto& [id, text] : strings.entries(coney::gui::StringTable::Hud)) {
        const coney::gui::TextLayout layout = coney::gui::layoutText(text, coney::gui::TextStyle{}, fonts);
        ++laidOut;
        sprites += layout.sprites.size();
        lines += layout.lines;
        skipped += layout.skippedTags;
        empty += layout.sprites.empty() ? 1 : 0;
    }
    std::printf("HUD strings laid out: %zu; %zu sprites, %zu lines, %zu tags skipped, %zu draw nothing\n", laidOut,
                sprites, lines, skipped, empty);
    CHECK(laidOut == 388);
    CHECK(sprites > 0);

    // Owner items 9 and 24b (docs/research/gui.md#moneyfont, docs/research/rumble.md#intro): the swap prompts 0xc-0xe
    // start with GSTRING.ACTIONBUTTON, whose <ST> inside <MONEYFONT> is part_page0 rectangle 73 (the triangle); the
    // revive prompt 4 draws a big_font stick picture (rectangles 256-261); the rumble prompt 0x25, laid out as its
    // usage line in part_page0, draws the cross button.
    const auto drawsRect = [](const coney::gui::TextLayout& layout, const coney::graphics::Font& font, int slot,
                              std::size_t first, std::size_t last) {
        std::size_t found = 0;
        for (const coney::gui::TextSprite& sprite : layout.sprites) {
            for (std::size_t rect = first; rect <= last && rect < font.sheet().page.rects.size(); ++rect) {
                found += sprite.fontSlot == slot && sprite.sprite.uv == font.sheet().page.rect(rect) ? 1 : 0;
            }
        }
        return found;
    };
    std::size_t triangles = 0;
    for (const std::uint32_t id : {0xcU, 0xdU, 0xeU}) {
        triangles += drawsRect(coney::gui::layoutText(strings.get(id), coney::gui::TextStyle{}, fonts), *textFont,
                               coney::gui::kTextFontSlot, 73, 73);
    }
    const std::size_t sticks = drawsRect(coney::gui::layoutText(strings.get(4), coney::gui::TextStyle{}, fonts), *big,
                                         coney::gui::kBigFontSlot, 256, 261);
    const coney::gui::TextLayout rumble = coney::gui::layoutText(strings.get(0x25), coney::gui::TextStyle{}, fonts);
    std::size_t crosses = 0;
    for (const coney::gui::TextSprite& sprite : rumble.sprites) {
        crosses += sprite.sprite.uv == textFont->glyph(0x9e) ? 1 : 0;
    }
    std::printf("prompt glyphs: %zu triangles in the 3 swap prompts, %zu stick pictures in the revive prompt, %zu "
                "crosses in the rumble prompt\n",
                triangles, sticks, crosses);
    CHECK(triangles == 3);
    CHECK(sticks >= 1);
    CHECK(crosses >= 1);
}
