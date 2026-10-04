// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "graphics/font.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"

namespace coney::gui {

/// The font slot of `big_font`: text code names fonts by the resource manager's instance slot, and slot 6 is the
/// `big_font` instance made at start-up (docs/research/gui.md#resource-instances).
inline constexpr int kBigFontSlot = 6;
/// The font slot a text starts in: slot 2, one of the start-up `part_page0` instances (`part_page0` is a font with
/// first glyph 94 whose characters 0x91-0xa0 are the button pictures of the tag table). Coney's choice: the page does
/// not say which slot a text widget starts in; `big_font` has no button pictures there, so it cannot be the default
/// of text that uses button tags outside `<BIGFONT>`.
inline constexpr int kTextFontSlot = 2;
/// The sprite sheet the start-up instance in kTextFontSlot is made over.
inline constexpr std::string_view kTextFontSheet = "part_page0";
/// The sprite sheet of kBigFontSlot.
inline constexpr std::string_view kBigFontSheet = "big_font";

/// How the lines of a text sit in its box.
enum class TextAlignment : std::uint8_t {
    Left,
    Centre,
    Right,
};

/// Where and how a text is laid out: what a text widget gives the layout.
struct TextStyle {
    float x = 0.0F;        ///< GUI x of the box's left edge.
    float y = 0.0F;        ///< GUI y of the first line's centre (the glyphs' centre line, as Font_Draw takes it).
    float boxWidth = 0.0F; ///< The box's width; grown to the widest line.
    float scale = 1.0F;    ///< The font scale (graphics::fontMetrics()); `<SIZE f>` multiplies it.
    graphics::Rgba colour = graphics::kWhite;
    float fade = 1.0F;            ///< Multiplies every alpha (the widget's fade), 0 to 1.
    std::uint8_t shadowAlpha = 0; ///< A drop shadow under each glyph when not 0.
    bool proportional = true;     ///< Proportional glyph widths (Font flag 0x04).
    TextAlignment alignment = TextAlignment::Left;
    int fontSlot = kTextFontSlot; ///< The font until `<BIGFONT>` changes it.
    std::uint32_t timeMs = 0;     ///< Game time since the text was first shown: `<PULSE>`, `<DISPLAYTIME>`.
};

/// One sprite of a laid-out text and the font slot whose batch draws it.
struct TextSprite {
    int fontSlot = kTextFontSlot;
    graphics::Sprite sprite;
};

/// A laid-out text: its sprites in drawing order, its size and what its tags asked for beyond drawing.
struct TextLayout {
    std::vector<TextSprite> sprites;
    float width = 0.0F;  ///< The box's width after growing to the widest line, GUI units.
    float height = 0.0F; ///< From the first line's top to the last line's bottom, GUI units.
    std::size_t lines = 0;
    bool expired = false;            ///< `<DISPLAYTIME>` has passed: nothing is drawn.
    std::vector<std::string> sounds; ///< `<SOUND name>` names, in order, for whoever plays them on the first frame.
    std::uint32_t freezeMs = 0;      ///< The largest `<FREEZE ms>`; Coney does not freeze the game timer yet.
    std::size_t skippedTags = 0;     ///< Tags with no effect in Coney (unknown names, not-yet-understood tags).
};

/// The font in a slot, or null when that slot holds none (its characters are then skipped).
using FontLookup = std::function<const graphics::Font*(int slot)>;

/// Lays out marked-up `text` (docs/research/gui.md#markup) in `style`: splits it at tags, measures each line's runs
/// with their font, size and colour, grows the box to the widest line, places each line by its alignment and draws
/// every run with Font_Draw's rules (graphics::Font::draw()). Lines break only at `<CR>`, `<CR2>`, `<CR3 f>` and
/// `<CRM>`, moving down by h + lineGap of the size in effect (plus f for `<CR3>`): the page documents no automatic
/// wrapping.
///
/// Coney's choices where the page is silent: a closing tag restores the value before its opening tag (a stack); a
/// line takes the alignment in effect at its first character; `<CCENTER>` and `<RRIGHT>` act as `<CENTER>` and
/// `<RIGHT>`; `<CR2>` and `<CRM>` always break (Coney is single-player); `<PULSE ms>` scales the colour by
/// 1 + 0.5 × sin(2π t / ms); `<BOLD>`, `<MONEYFONT>`, `<BGFONT>`, `<AUTOINDENT>` and the animated stick glyphs have no
/// effect yet and are counted in skippedTags.
///
/// Research: docs/research/gui.md#text
/// @orig 0x001b9600 TextWidget_Layout (unknown)
[[nodiscard]] TextLayout layoutText(std::string_view text, const TextStyle& style, const FontLookup& fonts);

/// Adds the sprites of `layout` to the batch of each sprite's font slot; a slot with no batch (null) is skipped.
/// Returns how many sprites a full batch dropped.
std::size_t addTextSprites(const TextLayout& layout, const std::function<graphics::SpriteBatch*(int slot)>& batches);

} // namespace coney::gui
