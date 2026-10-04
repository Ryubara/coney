// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace coney::gui {

/// A markup tag, numbered by its index in the original's table of 66 tags (`0x0050d718`). Indices 23-31 are HUD icons
/// whose names are not researched yet; Coney cannot recognise them until they are.
///
/// Research: docs/research/gui.md#markup
enum class MarkupTag : std::uint8_t {
    Color = 0,       ///< `<COLOR rrggbbaa>`
    Size = 1,        ///< `<SIZE f>`
    Pulse = 2,       ///< `<PULSE ms>`
    Sound = 3,       ///< `<SOUND name>`
    Freeze = 4,      ///< `<FREEZE ms>`
    DisplayTime = 5, ///< `<DISPLAYTIME ms>`
    Bold = 6,
    BigFont = 7,
    MoneyFont = 8,
    BgFont = 9,
    MoneyPlus = 10,
    MoneyMinus = 11,
    Center = 12,
    CCenter = 13,
    Right = 14,
    RRight = 15,
    Left = 16,
    Cr = 17,
    Cr2 = 18,
    Cr3 = 19, ///< `<CR3 f>`
    Crm = 20,
    AutoIndent = 21,
    Fist = 22,
    BgCircle = 32,
    ButtonSquare = 33,         ///< `<S>`
    ButtonCircle = 34,         ///< `<O>`
    ButtonTriangle = 35,       ///< `<T>`
    ButtonSquareTriangle = 36, ///< `<ST>`
    ButtonCross = 37,          ///< `<X>`
    ButtonStart = 38,
    ButtonSelect = 39,
    ButtonR1 = 40,
    ButtonR2 = 41,
    ButtonR3 = 42,
    ButtonL1 = 43,
    ButtonL2 = 44,
    ButtonL3 = 45,
    DpadUp = 46,
    DpadDown = 47,
    DpadLeft = 48,
    DpadRight = 49,
    LeftStick = 50,  ///< `<LAS>`: no character
    RightStick = 51, ///< `<RAS>`: no character
    StickDown = 52,  ///< `<SDD>`: animated
    StickLeft = 53,  ///< `<SDL>`
    StickRight = 54, ///< `<SDR>`
    StickUp = 55,    ///< `<SDU>`
    EndColor = 56,
    EndSize = 57,
    EndPulse = 58,
    EndBold = 59,
    EndBigFont = 60,
    EndMoneyFont = 61,
    EndBgFont = 62,
    EndCenter = 63,
    EndRight = 64,
    EndLeft = 65,
};

/// The tag named `name` (the text after `<` up to a space or `>`, such as `COLOR` or `/SIZE`), or nothing for a name
/// the table does not hold or Coney does not know yet. Names compare exactly, letter case included.
[[nodiscard]] std::optional<MarkupTag> findMarkupTag(std::string_view name);

/// The character a glyph tag stands for (a button, an icon, `=` for `<MONEYPLUS>`, `<` for `<MONEYMINUS>`), drawn
/// with the current font; nothing for a tag that is not a glyph or whose character is not known.
[[nodiscard]] std::optional<std::uint8_t> markupCharacter(MarkupTag tag);

/// One piece of marked-up text.
struct MarkupToken {
    /// What the piece is.
    enum class Kind : std::uint8_t {
        Text,       ///< Characters to draw: `text`.
        Tag,        ///< A known tag: `tag` and its `argument`.
        UnknownTag, ///< A tag Coney does not know: its name in `text`; skipped.
    };
    Kind kind = Kind::Text;
    std::string_view text; ///< The characters (Text) or the tag's name (UnknownTag).
    MarkupTag tag = MarkupTag::Color;
    std::string_view argument; ///< What follows the name inside the brackets, without the space (`B2B2B2FF`).
};

/// Splits marked-up text into runs of characters and tags, as the original's layout does: at each `<`, the text up to
/// the next `>` is a tag. A `<` with no `>` after it is drawn as text (Coney's choice). The views point into `text`.
[[nodiscard]] std::vector<MarkupToken> parseMarkup(std::string_view text);

} // namespace coney::gui
