// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/colour_table.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// Where a CircledText sits and how big its parts are (`CircledText_Setup`'s arguments as far as Coney uses them).
struct CircledTextSetup {
    float x = 0.5F;             ///< GUI x of the rectangle's left edge (the original keeps its centre, `+0x20`).
    float y = 0.5F;             ///< GUI y of the rectangle's centre (`+0x28`).
    float width = 0.6F;         ///< The rectangle's width, in GUI widths (`+0x30`).
    float height = 0.06F;       ///< The rectangle's height before the reveal (`+0x38`).
    float circleWidth = 0.05F;  ///< `circleW` (`+0x5f0`): a circle's width, and the base of the circles' step.
    float circleHeight = 0.05F; ///< `circleH` (`+0x5f4`): a circle's height.
    float circleOffset = 0.0F;  ///< `+0x240`: where the circles and the text start, from the left edge.
    float textSize = 1.7F;      ///< The texts' font size (the metrics the caller passes).
};

/// A text after up to two **circled characters** on a back sprite (our name `CircledText`, vtable `0x0053b390`): the
/// mission-select and statistics screens' level headers.
///
/// - **Set-up** (`CircledText_Setup`, once): the back sprite depth 8,400, grey (128, 128, 128); the circles depth
///   8,500, red (160, 16, 16), `circleW` × `circleH`; the text and the characters in the normal colour `0x005fd320`,
///   the characters without a shadow; reveal 1.
/// - **Text** (setText(), setCircled()): the main text; one character of the string per circle.
/// - **Layout** (update(), `CircledText_Update`), with r the reveal: the back sprite over the rectangle, H × r high,
///   its top edge kept; from x0 = the left edge + circleOffset, circle i centred at x0 + (2i + 1) × `circleW` / 3
///   (the divisor `0x0050ec64`: each circle adds the step before and after itself), `circleH` × r high with its top
///   edge kept, `circleH` × 0.1 (`0x0050ec68`) below the row; its character centred on it at the row's y; the text at
///   the last circle's centre + `circleW` / 2, or at x0 without circles.
/// - **Drawing** (render(), `CircledText_Render`): the back sprite, the text (focused colour `0x005fd318` when
///   focused, else the normal one), each circle and its character, which takes the focused colour unless it is black.
///
/// Stand-ins: the back sprite and the circles are drawn whole at their reduced height (the original also cuts their
/// texture rectangles by the reveal), and the texts are not revealed (Coney's text widget has no reveal); more than
/// kCircles characters are dropped (the original has room for two and writes past them).
///
/// Research: docs/research/gui.md#fn-circledtext
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
    /// The step divisor between circles (`0x0050ec64`: 3; 2 with device flag `0x02`, which Coney does not model).
    static constexpr float kStepDivisor = 3.0F;
    /// How far below the row a circle sits, in circle heights (`0x0050ec68` = `circleH` × 0.1).
    static constexpr float kCircleDrop = 0.1F;
    /// The font slot of the texts (the menus' text font).
    static constexpr int kFontSlot = 4;

    /// The parts, blank until setup(), and no circles in use.
    /// @orig 0x001d0aa8 CircledText_Construct (unknown)
    CircledText();

    /// The back sprite: `rect` of `batch` (not owned; null draws none).
    /// @coney-only sprite binding: the original's set-up takes the back sprite's word and batch
    void setBack(graphics::SpriteBatch* batch, std::size_t rect);
    /// The circles: `rect` of `batch` (not owned; null draws none).
    /// @coney-only sprite binding: the original's set-up takes the circles' sprite word and batch
    void setCircleSprite(graphics::SpriteBatch* batch, std::size_t rect);
    /// Places and sizes the parts; only the first call counts, as in the original.
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
    /// The characters' colour (until a render recolours a character that is not black).
    /// @orig 0x001d1108 CircledText_SetCharColour (unknown)
    void setCharColour(graphics::Rgba colour);
    /// The back sprite's colour.
    /// @orig 0x001d1178 CircledText_SetBackColour (unknown)
    void setBackColour(graphics::Rgba colour) { m_backColour = colour; }
    /// The reveal, clamped to 0-1 (anything not at least 0, NaN included, is 0): the share of the height shown.
    /// @orig 0x001d11c8 CircledText_SetReveal (unknown)
    void setReveal(float reveal);
    /// Whether the text takes the focused colour (the original asks its slot `+0xa0`).
    void setFocused(bool focused) { m_focused = focused; }
    /// Moves the rectangle's centre to GUI y `y` (the list places its entries each frame).
    void moveTo(float y) { m_setup.y = y; }

    /// The rectangle's height × the reveal.
    /// @orig 0x001d11f8 CircledText_GetRect (unknown)
    [[nodiscard]] float height() const { return m_setup.height * m_reveal; }
    /// How many circles are used.
    [[nodiscard]] std::size_t circles() const { return m_used; }
    /// The main text, for checks.
    [[nodiscard]] const TextWidget& text() const { return m_text; }
    /// Character `i`, for checks.
    [[nodiscard]] const TextWidget& character(std::size_t i) const { return m_chars.at(i); }
    /// Circle `i` (placed by the last update()), for checks.
    [[nodiscard]] const BaseWidget& circle(std::size_t i) const { return m_circles.at(i); }
    /// The back sprite (placed by the last update()), for checks.
    [[nodiscard]] const BaseWidget& back() const { return m_back; }
    /// The circles' colour.
    [[nodiscard]] graphics::Rgba circleColour() const { return m_circleColour; }

    /// Places every part for the reveal (the layout above). Nothing while hidden.
    /// @orig 0x001d1250 CircledText_Update (unknown)
    void update(const GuiFrame& frame) override;
    /// Draws the parts. Nothing while hidden or with nothing revealed.
    /// @orig 0x001d16e8 CircledText_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

  private:
    CircledTextSetup m_setup;
    bool m_setUp = false; // the original's "set up" flag (`+0x0c`): later set-ups are ignored
    BaseWidget m_back{nullptr, 0};
    std::array<BaseWidget, kCircles> m_circles{{{nullptr, 0}, {nullptr, 0}}};
    // The characters and the text take their drawing colour in render(), as the original's render sets them.
    mutable std::array<TextWidget, kCircles> m_chars;
    mutable TextWidget m_text;
    graphics::Rgba m_backColour = kBackColour;
    graphics::Rgba m_circleColour = kCircleColour;
    std::size_t m_used = 0;
    float m_reveal = 1.0F;
    bool m_focused = false;
};

} // namespace coney::gui
