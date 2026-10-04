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
}
