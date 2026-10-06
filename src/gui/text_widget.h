// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "graphics/render_device.h"
#include "gui/text_layout.h"
#include "gui/widget.h"

namespace coney::gui {

/// How a text widget is set up: the original's light `TextWidget` setup (slot `+0x68`, `0x001cd060`: position,
/// metrics, colour, flags, font slot) and the markup widget's (`0x001b9090`: scale, colour, alignment, font slot).
struct TextWidgetSetup {
    float x = 0.0F;                           ///< GUI x: where the pen starts (left), the centre, or the right edge.
    float y = 0.0F;                           ///< GUI y the first line's glyphs are centred on.
    float scale = 1.0F;                       ///< The font scale (`Font_Size`), the original's "size".
    graphics::Rgba colour = graphics::kWhite; ///< The text's colour; its alpha fades it.
    TextAlignment alignment = TextAlignment::Left;
    int fontSlot = kTextFontSlot; ///< The font until a `<BIGFONT>`; kBigFontSlot for `big_font`.
    float wrapWidth = 0.0F;       ///< The multi-line widget's wrap width; 0: lines break only at `<CR>` tags.
};

/// A text widget: a marked-up text (docs/research/gui.md#markup) laid out and drawn each frame, one sprite per
/// character, through the batches of its font slots, with a drop shadow of 0x80. Its time for `<PULSE>` and
/// `<DISPLAYTIME>` counts from the first update after the text was set.
///
/// It stands for both of the original's text widgets: the light `TextWidget` (`0x001ccf88`, one line without markup,
/// font slot as given: grid items, PM titles and prompts) and the markup text widget (`0x001b8f98`, `MessageHUD`:
/// usage lines, hints, message boxes), since a text without tags lays out the same either way: the pen starts at x and
/// the glyphs are centred on y (confirmed at runtime: a text at y 0.81 has its capitals centred at 0.811).
///
/// Research: docs/research/gui.md#widget-classes, docs/research/gui.md#text
/// @orig 0x001ccf88 TextWidget::TextWidget (unknown)
/// @orig 0x001b8f98 MessageHUD::MessageHUD (unknown)
class TextWidget : public Widget {
  public:
    /// The drop shadow text widgets draw with (`+0xc4` and the markup widget's `+0x18c`): 0x80.
    static constexpr std::uint8_t kShadowAlpha = 0x80;

    TextWidget();

    /// Places, sizes and colours the text.
    /// @orig 0x001cd060 TextWidget_Setup (unknown)
    void setup(const TextWidgetSetup& setup);

    /// Replaces the text and restarts its time.
    /// @orig 0x001cd1e0 TextWidget_SetText (unknown)
    void setText(std::string_view text);
    /// The text.
    [[nodiscard]] const std::string& text() const { return m_text; }

    /// Centres the text's lines on GUI x `centreX` in a box `boxWidth` wide, the first line's centre at GUI y `y`.
    void centreOn(float centreX, float y, float boxWidth);

    /// The style the layout uses: position, scale, colour, alignment, font slot. Its time is set by update().
    [[nodiscard]] TextStyle& style() { return m_style; }
    /// The style the layout uses.
    [[nodiscard]] const TextStyle& style() const { return m_style; }

    /// Sets the widget's fade, 0 (invisible) to 1, which multiplies every alpha of the text.
    void setFade(float fade) { m_style.fade = fade; }

    /// Advances the text's time to `frame`.
    void update(const GuiFrame& frame) override;

    /// Lays the text out and adds its sprites to the canvas's batch of each font slot. Nothing while hidden.
    void render(const GuiCanvas& canvas) const override;

    /// Lays the text out with the canvas's fonts, without drawing: what render() would draw.
    [[nodiscard]] TextLayout layout(const GuiCanvas& canvas) const;

    /// The text's measured width in GUI units with the canvas's fonts (the original's `+0xb0`); 0 without fonts.
    [[nodiscard]] float width(const GuiCanvas& canvas) const { return layout(canvas).width; }

  private:
    std::string m_text;
    TextStyle m_style;
    std::optional<std::uint64_t> m_shownSinceMs; // game time of the first update after setText()
    bool m_anchored = false;                     // placed by setup(): x is the anchor point, not a box's left edge
};

} // namespace coney::gui
