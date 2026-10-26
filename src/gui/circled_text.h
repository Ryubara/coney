// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/colour_table.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// Where a CircledText sits and how big its parts are (`CircledText_Setup`'s arguments as far as Coney uses them).
struct CircledTextSetup {
    float x = 0.5F;             ///< GUI x of the rectangle's left edge.
    float y = 0.5F;             ///< GUI y of the rectangle's centre.
    float width = 0.6F;         ///< The rectangle's width (the back sprite's).
    float height = 0.06F;       ///< The rectangle's height before the reveal.
    float circleWidth = 0.05F;  ///< `circleW` (`+0x5f0`): a circle's width.
    float circleHeight = 0.05F; ///< `circleH` (`+0x5f4`): a circle's height.
    float circleOffset = 0.0F;  ///< `+0x240`: the first circle's x from the left edge.
    float textOffset = 0.01F;   ///< `+0x30`: the main text's x after the last circle.
    float textSize = 1.7F;      ///< The texts' font size.
};

/// A text after up to two **circled characters** on a back sprite (our name `CircledText`, vtable `0x0053b390`): the
/// mission-select and statistics screens' level headers.
///
/// - **Set-up** (`CircledText_Setup`): the back sprite size 1.0, depth 8,400, grey (128, 128, 128); the circles depth
///   8,500, red (160, 16, 16), `circleW` × `circleH`; reveal 1.
/// - **Text** (setText(), setCircled()): the main text; one character of the string per circle, as many circles as
///   characters (at most kCircles: the object has room for two).
/// - **Layout** (`CircledText_Update`): the back sprite over the rectangle, its height × the reveal; the circles left
///   to right from the left edge + circleOffset, each step `circleW` / 3 (the divisor `0x0050ec64`; 2 with device flag
///   `0x02`, which Coney does not model); the main text after the last circle; nothing while the reveal is 0.
/// - **Drawing** (`CircledText_Render`): the back sprite, the text (focused colour `+0x134` when focused, else
///   `+0x130`), each circle and its character.
///
/// Coney's choices: the back sprite is drawn whole, its height × the reveal (the original also cuts its texture
/// rectangle by the reveal); the characters are centred on the circles (the original offsets them by `circleH` × 0.1,
/// `0x0050ec68`, from a text origin Coney's centred glyphs do not share); the characters are drawn in their own colour
/// (the original recolours them to `+0x134` unless their colour is the colour table's entry 0, not traced further).
///
/// Research: docs/research/gui.md#fn-circledtext
/// @orig 0x001d0aa8 CircledText_Construct (unknown)
class CircledText : public Widget {
  public:
    /// The most circles the object holds (`+0x250` + `0x100` × i, i < 2).
    static constexpr std::size_t kCircles = 2;
    /// The back sprite's and the circles' depths.
    static constexpr float kBackDepth = 8400.0F;
    static constexpr float kCircleDepth = 8500.0F;
    /// The back sprite's and the circles' colours from the set-up.
    static constexpr graphics::Rgba kBackColour{128, 128, 128, 255};
    static constexpr graphics::Rgba kCircleColour{160, 16, 16, 255};
    /// The step divisor between circles (`0x0050ec64`).
    static constexpr float kStepDivisor = 3.0F;
    /// The font slot of the texts (the menus' text font).
    static constexpr int kFontSlot = 4;

    CircledText();

    /// The back sprite: `rect` of `batch` (not owned; null draws none).
    void setBack(graphics::SpriteBatch* batch, std::size_t rect);
    /// The circles: `rect` of `batch` (not owned; null draws none).
    void setCircleSprite(graphics::SpriteBatch* batch, std::size_t rect);
    /// Places and sizes the parts, once.
    /// @orig 0x001d0d18 CircledText_Setup (unknown)
    void setup(const CircledTextSetup& setup);
    /// The main text.
    /// @orig 0x001d0fe8 CircledText_SetText (unknown)
    void setText(std::string_view text);
    /// One circle per character of `characters` (the first kCircles).
    /// @orig 0x001d1008 CircledText_SetCircled (unknown)
    void setCircled(std::string_view characters);
    /// The circles' colour.
    /// @orig 0x001d1098 CircledText_SetCircleColour (unknown)
    void setCircleColour(graphics::Rgba colour) { m_circleColour = colour; }
    /// The characters' colour.
    /// @orig 0x001d1108 CircledText_SetCharColour (unknown)
    void setCharColour(graphics::Rgba colour);
    /// The back sprite's colour.
    /// @orig 0x001d1178 CircledText_SetBackColour (unknown)
    void setBackColour(graphics::Rgba colour) { m_backColour = colour; }
    /// The reveal, clamped to 0-1: the share of the height shown.
    /// @orig 0x001d11c8 CircledText_SetReveal (unknown)
    void setReveal(float reveal);
    /// Whether the text takes the focused colour.
    void setFocused(bool focused) { m_focused = focused; }
    /// Moves the rectangle's centre to GUI y `y` (the list places its entries each frame).
    void moveTo(float y) { m_setup.y = y; }

    /// The rectangle's height × the reveal.
    /// @orig 0x001d11f8 CircledText_GetRect (unknown)
    [[nodiscard]] float height() const { return m_setup.height * m_reveal; }
    /// How many circles are used.
    [[nodiscard]] std::size_t circles() const { return m_used; }
    /// The main text and the characters, for checks.
    [[nodiscard]] const TextWidget& text() const { return m_text; }
    [[nodiscard]] const TextWidget& character(std::size_t i) const { return m_chars.at(i); }
    /// The circles' colour.
    [[nodiscard]] graphics::Rgba circleColour() const { return m_circleColour; }

    /// The texts' frame.
    void update(const GuiFrame& frame) override;
    /// Lays the parts out and draws them.
    /// @orig 0x001d1250 CircledText_Update (unknown)
    /// @orig 0x001d16e8 CircledText_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

  private:
    CircledTextSetup m_setup;
    mutable BaseWidget m_back{nullptr, 0};
    mutable std::array<BaseWidget, kCircles> m_circles{{{nullptr, 0}, {nullptr, 0}}};
    mutable std::array<TextWidget, kCircles> m_chars;
    mutable TextWidget m_text;
    graphics::Rgba m_backColour = kBackColour;
    graphics::Rgba m_circleColour = kCircleColour;
    std::size_t m_used = 0;
    float m_reveal = 1.0F;
    bool m_focused = false;
};

} // namespace coney::gui
