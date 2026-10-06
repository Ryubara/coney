// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/text_widget.h"

namespace coney::gui {

TextWidget::TextWidget() { m_style.shadowAlpha = kShadowAlpha; }

void TextWidget::setup(const TextWidgetSetup& setup) {
    // A box of no width: a left-aligned line starts at x, a centred one is centred on it, a right-aligned one ends
    // there.
    m_style.x = setup.x;
    m_style.y = setup.y;
    m_style.boxWidth = 0.0F;
    m_style.scale = setup.scale;
    m_style.colour = setup.colour;
    m_style.alignment = setup.alignment;
    m_style.fontSlot = setup.fontSlot;
    m_style.wrapWidth = setup.wrapWidth;
    m_anchored = true;
}

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
    m_anchored = false;
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
    if (!canvas.fonts) {
        return TextLayout{};
    }
    // A text placed by setup() is centred on, or ends at, its x: measure it first, then move it by its width.
    if (m_anchored && m_style.alignment != TextAlignment::Left) {
        TextStyle style = m_style;
        const float width = layoutText(m_text, m_style, canvas.fonts).width;
        style.x -= m_style.alignment == TextAlignment::Centre ? width / 2.0F : width;
        return layoutText(m_text, style, canvas.fonts);
    }
    return layoutText(m_text, m_style, canvas.fonts);
}

} // namespace coney::gui
