// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/text_widget.h"

namespace coney::gui {

TextWidget::TextWidget() { m_style.shadowAlpha = kShadowAlpha; }

void TextWidget::setText(std::string_view text) {
    m_text = text;
    m_shownSinceMs.reset();
    m_style.timeMs = 0;
}

void TextWidget::centreOn(float centreX, float y, float boxWidth) {
    m_style.x = centreX - boxWidth / 2.0F;
    m_style.y = y;
    m_style.boxWidth = boxWidth;
    m_style.alignment = TextAlignment::Centre;
}

void TextWidget::update(const GuiFrame& frame) {
    if (!m_shownSinceMs) {
        m_shownSinceMs = frame.timeMs;
    }
    m_style.timeMs = static_cast<std::uint32_t>(frame.timeMs - *m_shownSinceMs);
}

void TextWidget::render(const GuiCanvas& canvas) const {
    if (!visible() || m_text.empty() || !canvas.fonts || !canvas.textBatch) {
        return;
    }
    addTextSprites(layout(canvas), canvas.textBatch);
}

TextLayout TextWidget::layout(const GuiCanvas& canvas) const {
    // A canvas without fonts lays out nothing, as a widget whose font slots hold none.
    return canvas.fonts ? layoutText(m_text, m_style, canvas.fonts) : TextLayout{};
}

} // namespace coney::gui
