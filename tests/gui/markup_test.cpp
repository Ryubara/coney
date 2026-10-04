// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/markup.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::gui::findMarkupTag;
using coney::gui::markupCharacter;
using coney::gui::MarkupTag;
using coney::gui::MarkupToken;
using coney::gui::parseMarkup;

TEST_CASE("marked-up text splits into text runs and tags with arguments", "[markup]") {
    const std::vector<MarkupToken> tokens = parseMarkup("<COLOR B2B2B2FF>Press <X> to go<CR></COLOR>");
    REQUIRE(tokens.size() == 6);
    CHECK(tokens[0].kind == MarkupToken::Kind::Tag);
    CHECK(tokens[0].tag == MarkupTag::Color);
    CHECK(tokens[0].argument == "B2B2B2FF");
    CHECK(tokens[1].kind == MarkupToken::Kind::Text);
    CHECK(tokens[1].text == "Press ");
    CHECK(tokens[2].tag == MarkupTag::ButtonCross);
    CHECK(tokens[3].text == " to go");
    CHECK(tokens[4].tag == MarkupTag::Cr);
    CHECK(tokens[5].tag == MarkupTag::EndColor);
}

TEST_CASE("unknown tags are kept by name and a lone < is text", "[markup]") {
    const std::vector<MarkupToken> tokens = parseMarkup("a<BOBJ>b < c");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[1].kind == MarkupToken::Kind::UnknownTag);
    CHECK(tokens[1].text == "BOBJ");
    CHECK(tokens[2].text == "b < c");
}

TEST_CASE("tag names match exactly and glyph tags give their characters", "[markup]") {
    CHECK(findMarkupTag("SIZE") == MarkupTag::Size);
    CHECK(findMarkupTag("S") == MarkupTag::ButtonSquare);
    CHECK(findMarkupTag("ST") == MarkupTag::ButtonSquareTriangle);
    CHECK(findMarkupTag("/BIGFONT") == MarkupTag::EndBigFont);
    CHECK(!findMarkupTag("size").has_value());
    CHECK(markupCharacter(MarkupTag::ButtonCross) == 0x9e);
    CHECK(markupCharacter(MarkupTag::ButtonTriangle) == 0x96);
    CHECK(markupCharacter(MarkupTag::DpadRight) == 0x98);
    CHECK(markupCharacter(MarkupTag::MoneyMinus) == '<');
    CHECK(!markupCharacter(MarkupTag::LeftStick).has_value());
    CHECK(!markupCharacter(MarkupTag::Color).has_value());
}
