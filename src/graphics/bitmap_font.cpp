// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/bitmap_font.h"

#include <cstddef>

#include "core/assert.h"

namespace coney::graphics {

namespace {

// The glyphs of 0x20 to 0x7e, each seven rows of five pixels (`#` lit), rows separated by spaces. Drawn for Coney.
constexpr std::array<std::string_view, 95> kGlyphs{
    "..... ..... ..... ..... ..... ..... .....", // space
    "..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#..", // !
    ".#.#. .#.#. ..... ..... ..... ..... .....", // "
    ".#.#. .#.#. ##### .#.#. ##### .#.#. .#.#.", // #
    "..#.. .#### #.#.. .###. ..#.# ####. ..#..", // $
    "##... ##..# ...#. ..#.. .#... #..## ...##", // %
    ".##.. #..#. #.#.. .#... #.#.# #..#. .##.#", // &
    "..#.. ..#.. ..... ..... ..... ..... .....", // '
    "...#. ..#.. .#... .#... .#... ..#.. ...#.", // (
    ".#... ..#.. ...#. ...#. ...#. ..#.. .#...", // )
    "..... ..#.. #.#.# .###. #.#.# ..#.. .....", // *
    "..... ..#.. ..#.. ##### ..#.. ..#.. .....", // +
    "..... ..... ..... ..... .##.. ..#.. .#...", // ,
    "..... ..... ..... ##### ..... ..... .....", // -
    "..... ..... ..... ..... ..... .##.. .##..", // .
    "..... ....# ...#. ..#.. .#... #.... .....", // /
    ".###. #...# #..## #.#.# ##..# #...# .###.", // 0
    "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.", // 1
    ".###. #...# ....# ...#. ..#.. .#... #####", // 2
    "##### ...#. ..#.. ...#. ....# #...# .###.", // 3
    "...#. ..##. .#.#. #..#. ##### ...#. ...#.", // 4
    "##### #.... ####. ....# ....# #...# .###.", // 5
    "..##. .#... #.... ####. #...# #...# .###.", // 6
    "##### ....# ...#. ..#.. .#... .#... .#...", // 7
    ".###. #...# #...# .###. #...# #...# .###.", // 8
    ".###. #...# #...# .#### ....# ...#. .##..", // 9
    "..... .##.. .##.. ..... .##.. .##.. .....", // :
    "..... .##.. .##.. ..... .##.. ..#.. .#...", // ;
    "...#. ..#.. .#... #.... .#... ..#.. ...#.", // <
    "..... ..... ##### ..... ##### ..... .....", // =
    ".#... ..#.. ...#. ....# ...#. ..#.. .#...", // >
    ".###. #...# ....# ...#. ..#.. ..... ..#..", // ?
    ".###. #...# ....# .##.# #.#.# #.#.# .###.", // @
    ".###. #...# #...# ##### #...# #...# #...#", // A
    "####. #...# #...# ####. #...# #...# ####.", // B
    ".###. #...# #.... #.... #.... #...# .###.", // C
    "###.. #..#. #...# #...# #...# #..#. ###..", // D
    "##### #.... #.... ####. #.... #.... #####", // E
    "##### #.... #.... ####. #.... #.... #....", // F
    ".###. #...# #.... #.### #...# #...# .####", // G
    "#...# #...# #...# ##### #...# #...# #...#", // H
    ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###.", // I
    "..### ...#. ...#. ...#. ...#. #..#. .##..", // J
    "#...# #..#. #.#.. ##... #.#.. #..#. #...#", // K
    "#.... #.... #.... #.... #.... #.... #####", // L
    "#...# ##.## #.#.# #.#.# #...# #...# #...#", // M
    "#...# #...# ##..# #.#.# #..## #...# #...#", // N
    ".###. #...# #...# #...# #...# #...# .###.", // O
    "####. #...# #...# ####. #.... #.... #....", // P
    ".###. #...# #...# #...# #.#.# #..#. .##.#", // Q
    "####. #...# #...# ####. #.#.. #..#. #...#", // R
    ".#### #.... #.... .###. ....# ....# ####.", // S
    "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..", // T
    "#...# #...# #...# #...# #...# #...# .###.", // U
    "#...# #...# #...# #...# #...# .#.#. ..#..", // V
    "#...# #...# #...# #.#.# #.#.# #.#.# .#.#.", // W
    "#...# #...# .#.#. ..#.. .#.#. #...# #...#", // X
    "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..", // Y
    "##### ....# ...#. ..#.. .#... #.... #####", // Z
    ".###. .#... .#... .#... .#... .#... .###.", // [
    "..... #.... .#... ..#.. ...#. ....# .....", // backslash
    ".###. ...#. ...#. ...#. ...#. ...#. .###.", // ]
    "..#.. .#.#. #...# ..... ..... ..... .....", // ^
    "..... ..... ..... ..... ..... ..... #####", // _
    ".#... ..#.. ..... ..... ..... ..... .....", // `
    "..... ..... .###. ....# .#### #...# .####", // a
    "#.... #.... #.##. ##..# #...# #...# ####.", // b
    "..... ..... .###. #.... #.... #...# .###.", // c
    "....# ....# .##.# #..## #...# #...# .####", // d
    "..... ..... .###. #...# ##### #.... .###.", // e
    "..##. .#..# .#... ###.. .#... .#... .#...", // f
    "..... .#### #...# #...# .#### ....# .###.", // g
    "#.... #.... #.##. ##..# #...# #...# #...#", // h
    "..#.. ..... .##.. ..#.. ..#.. ..#.. .###.", // i
    "...#. ..... ..##. ...#. ...#. #..#. .##..", // j
    "#.... #.... #..#. #.#.. ##... #.#.. #..#.", // k
    ".##.. ..#.. ..#.. ..#.. ..#.. ..#.. .###.", // l
    "..... ..... ##.#. #.#.# #.#.# #...# #...#", // m
    "..... ..... #.##. ##..# #...# #...# #...#", // n
    "..... ..... .###. #...# #...# #...# .###.", // o
    "..... ..... ####. #...# ####. #.... #....", // p
    "..... ..... .##.# #..## .#### ....# ....#", // q
    "..... ..... #.##. ##..# #.... #.... #....", // r
    "..... ..... .###. #.... .###. ....# ####.", // s
    ".#... .#... ###.. .#... .#... .#..# ..##.", // t
    "..... ..... #...# #...# #...# #..## .##.#", // u
    "..... ..... #...# #...# #...# .#.#. ..#..", // v
    "..... ..... #...# #...# #.#.# #.#.# .#.#.", // w
    "..... ..... #...# .#.#. ..#.. .#.#. #...#", // x
    "..... ..... #...# #...# .#### ....# .###.", // y
    "..... ..... ##### ...#. ..#.. .#... #####", // z
    "...#. ..#.. ..#.. .#... ..#.. ..#.. ...#.", // {
    "..#.. ..#.. ..#.. ..#.. ..#.. ..#.. ..#..", // |
    ".#... ..#.. ..#.. ...#. ..#.. ..#.. .#...", // }
    "..... ..... .#... #.#.# ...#. ..... .....", // ~
};

// The first character in kGlyphs.
constexpr char kFirst = ' ';

// One glyph's art turned into row bits; the art must be 7 rows of 5 (checked at compile time below).
constexpr std::array<std::uint8_t, BitmapFont::kGlyphHeight> parseGlyph(std::string_view art) {
    std::array<std::uint8_t, BitmapFont::kGlyphHeight> rows{};
    std::size_t row = 0;
    int column = 0;
    for (const char c : art) {
        if (c == ' ') {
            ++row;
            column = 0;
            continue;
        }
        if (c == '#') {
            rows.at(row) = static_cast<std::uint8_t>(rows.at(row) | (1U << (BitmapFont::kGlyphWidth - 1 - column)));
        }
        ++column;
    }
    return rows;
}

// Whether every glyph's art has the right shape: 7 rows of 5 characters, only `#` and `.`.
constexpr bool glyphsWellFormed() {
    for (const std::string_view art : kGlyphs) {
        if (art.size() != BitmapFont::kGlyphHeight * (BitmapFont::kGlyphWidth + 1) - 1) {
            return false;
        }
        for (std::size_t i = 0; i < art.size(); ++i) {
            const bool separator = i % (BitmapFont::kGlyphWidth + 1) == BitmapFont::kGlyphWidth;
            if (separator ? art[i] != ' ' : art[i] != '#' && art[i] != '.') {
                return false;
            }
        }
    }
    return true;
}
static_assert(glyphsWellFormed(), "every glyph is 7 rows of 5 pixels");

} // namespace

std::array<std::uint8_t, BitmapFont::kGlyphHeight> BitmapFont::glyph(char character) {
    const auto index = static_cast<std::size_t>(static_cast<unsigned char>(character));
    const std::size_t first = static_cast<unsigned char>(kFirst);
    if (index < first || index >= first + kGlyphs.size()) {
        return parseGlyph(kGlyphs.at('?' - kFirst));
    }
    return parseGlyph(kGlyphs.at(index - first));
}

float BitmapFont::draw(std::vector<LogicalQuad>& out, std::string_view text, float x, float y, float scale,
                       Rgba colour) {
    CONEY_ASSERT(scale > 0.0F);
    for (const char character : text) {
        const auto rows = glyph(character);
        for (int row = 0; row < kGlyphHeight; ++row) {
            // One quad per run of lit pixels in the row.
            int column = 0;
            while (column < kGlyphWidth) {
                const auto lit = [&](int c) {
                    return (rows.at(static_cast<std::size_t>(row)) >> (kGlyphWidth - 1 - c)) & 1U;
                };
                if (lit(column) == 0) {
                    ++column;
                    continue;
                }
                const int start = column;
                while (column < kGlyphWidth && lit(column) != 0) {
                    ++column;
                }
                LogicalQuad quad;
                quad.x = x + static_cast<float>(start) * scale;
                quad.y = y + static_cast<float>(row) * scale;
                quad.width = static_cast<float>(column - start) * scale;
                quad.height = scale;
                quad.colour = colour;
                out.push_back(quad);
            }
        }
        x += static_cast<float>(kCellWidth) * scale;
    }
    return x;
}

} // namespace coney::graphics
