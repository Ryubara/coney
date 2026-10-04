// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "gui/text_layout.h"
#include "gui/widget.h"

namespace coney::gui {

/// A text widget: a marked-up text (docs/research/gui.md#markup) laid out and drawn each frame, one sprite per
/// character, through the batches of its font slots. Its time for `<PULSE>` and `<DISPLAYTIME>` counts from the first
/// update after the text was set.
///
/// Research: docs/research/gui.md#text
/// @orig 0x001ccf88 TextWidget::TextWidget (unknown)
class TextWidget : public Widget {
  public:
    /// The drop shadow text widgets draw with: half strength, as the text viewer's.
    static constexpr std::uint8_t kShadowAlpha = 128;

    TextWidget();

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

  private:
    std::string m_text;
    TextStyle m_style;
    std::optional<std::uint64_t> m_shownSinceMs; // game time of the first update after setText()
};

} // namespace coney::gui
