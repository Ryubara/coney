// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/markup.h"

#include <array>
#include <cstddef>

namespace coney::gui {

namespace {

// A tag's name in the original's table and its index.
struct TagName {
    std::string_view name;
    MarkupTag tag;
};

// The tags whose names docs/research/gui.md#markup gives, by index.
constexpr std::array<TagName, 57> kTagNames{{
    {"COLOR", MarkupTag::Color},
    {"SIZE", MarkupTag::Size},
    {"PULSE", MarkupTag::Pulse},
    {"SOUND", MarkupTag::Sound},
    {"FREEZE", MarkupTag::Freeze},
    {"DISPLAYTIME", MarkupTag::DisplayTime},
    {"BOLD", MarkupTag::Bold},
    {"BIGFONT", MarkupTag::BigFont},
    {"MONEYFONT", MarkupTag::MoneyFont},
    {"BGFONT", MarkupTag::BgFont},
    {"MONEYPLUS", MarkupTag::MoneyPlus},
    {"MONEYMINUS", MarkupTag::MoneyMinus},
    {"CENTER", MarkupTag::Center},
    {"CCENTER", MarkupTag::CCenter},
    {"RIGHT", MarkupTag::Right},
    {"RRIGHT", MarkupTag::RRight},
    {"LEFT", MarkupTag::Left},
    {"CR", MarkupTag::Cr},
    {"CR2", MarkupTag::Cr2},
    {"CR3", MarkupTag::Cr3},
    {"CRM", MarkupTag::Crm},
    {"AUTOINDENT", MarkupTag::AutoIndent},
    {"FIST", MarkupTag::Fist},
    {"BGCIRCLE", MarkupTag::BgCircle},
    {"S", MarkupTag::ButtonSquare},
    {"O", MarkupTag::ButtonCircle},
    {"T", MarkupTag::ButtonTriangle},
    {"ST", MarkupTag::ButtonSquareTriangle},
    {"X", MarkupTag::ButtonCross},
    {"START", MarkupTag::ButtonStart},
    {"SELECT", MarkupTag::ButtonSelect},
    {"R1", MarkupTag::ButtonR1},
    {"R2", MarkupTag::ButtonR2},
    {"R3", MarkupTag::ButtonR3},
    {"L1", MarkupTag::ButtonL1},
    {"L2", MarkupTag::ButtonL2},
    {"L3", MarkupTag::ButtonL3},
    {"DU", MarkupTag::DpadUp},
    {"DD", MarkupTag::DpadDown},
    {"DL", MarkupTag::DpadLeft},
    {"DR", MarkupTag::DpadRight},
    {"LAS", MarkupTag::LeftStick},
    {"RAS", MarkupTag::RightStick},
    {"SDD", MarkupTag::StickDown},
    {"SDL", MarkupTag::StickLeft},
    {"SDR", MarkupTag::StickRight},
    {"SDU", MarkupTag::StickUp},
    {"/COLOR", MarkupTag::EndColor},
    {"/SIZE", MarkupTag::EndSize},
    {"/PULSE", MarkupTag::EndPulse},
    {"/BOLD", MarkupTag::EndBold},
    {"/BIGFONT", MarkupTag::EndBigFont},
    {"/MONEYFONT", MarkupTag::EndMoneyFont},
    {"/BGFONT", MarkupTag::EndBgFont},
    {"/CENTER", MarkupTag::EndCenter},
    {"/RIGHT", MarkupTag::EndRight},
    {"/LEFT", MarkupTag::EndLeft},
}};

// A glyph tag and the character it stands for.
struct TagCharacter {
    MarkupTag tag;
    std::uint8_t character;
};

// The characters of the glyph tags, from docs/research/gui.md#markup.
constexpr std::array<TagCharacter, 21> kTagCharacters{{
    {MarkupTag::MoneyPlus, '='},
    {MarkupTag::MoneyMinus, '<'},
    {MarkupTag::Fist, ':'},
    {MarkupTag::BgCircle, 'y'},
    {MarkupTag::ButtonSquare, 0x9f},
    {MarkupTag::ButtonCircle, 0x9d},
    {MarkupTag::ButtonTriangle, 0x96},
    {MarkupTag::ButtonSquareTriangle, 'n'},
    {MarkupTag::ButtonCross, 0x9e},
    {MarkupTag::ButtonStart, 0x97},
    {MarkupTag::ButtonSelect, 0x93},
    {MarkupTag::ButtonR1, 0x9c},
    {MarkupTag::ButtonR2, 0x94},
    {MarkupTag::ButtonR3, 0x92},
    {MarkupTag::ButtonL1, 0xa0},
    {MarkupTag::ButtonL2, 0x95},
    {MarkupTag::ButtonL3, 0x91},
    {MarkupTag::DpadUp, 0x9b},
    {MarkupTag::DpadDown, 0x99},
    {MarkupTag::DpadLeft, 0x9a},
    {MarkupTag::DpadRight, 0x98},
}};

} // namespace

std::optional<MarkupTag> findMarkupTag(std::string_view name) {
    for (const TagName& entry : kTagNames) {
        if (entry.name == name) {
            return entry.tag;
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> markupCharacter(MarkupTag tag) {
    for (const auto& [glyphTag, character] : kTagCharacters) {
        if (glyphTag == tag) {
            return character;
        }
    }
    return std::nullopt;
}

std::vector<MarkupToken> parseMarkup(std::string_view text) {
    std::vector<MarkupToken> tokens;
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t open = text.find('<', position);
        const std::size_t close = open == std::string_view::npos ? std::string_view::npos : text.find('>', open);
        // No complete tag ahead: the rest is text.
        if (close == std::string_view::npos) {
            tokens.push_back(MarkupToken{MarkupToken::Kind::Text, text.substr(position), MarkupTag::Color, {}});
            break;
        }
        if (open > position) {
            tokens.push_back(
                MarkupToken{MarkupToken::Kind::Text, text.substr(position, open - position), MarkupTag::Color, {}});
        }
        // The tag: a name, then after a space its argument.
        const std::string_view inside = text.substr(open + 1, close - open - 1);
        const std::size_t space = inside.find(' ');
        const std::string_view name = inside.substr(0, space);
        const std::string_view argument =
            space == std::string_view::npos ? std::string_view() : inside.substr(space + 1);
        if (const std::optional<MarkupTag> tag = findMarkupTag(name)) {
            tokens.push_back(MarkupToken{MarkupToken::Kind::Tag, {}, *tag, argument});
        } else {
            tokens.push_back(MarkupToken{MarkupToken::Kind::UnknownTag, name, MarkupTag::Color, {}});
        }
        position = close + 1;
    }
    return tokens;
}

} // namespace coney::gui
