// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/messages.h"

#include <algorithm>
#include <utility>

#include "gui/text_layout.h"
#include "hud/hint_box.h"

namespace coney::hud {

gui::TextStyle messageStyle(float x) {
    gui::TextStyle style;
    style.x = x;
    style.scale = metricsOfHeight(kMessageTextHeight).width * 30.0F;
    style.shadowAlpha = 128;
    return style;
}

void drawMessage(const HudCanvas& canvas, std::string_view text, gui::TextStyle style, float centreY) {
    if (!canvas.text.fonts) {
        return;
    }
    // Measured at y 0, then moved so that the block's middle is on the given y.
    const gui::TextLayout measured = gui::layoutText(text, style, canvas.text.fonts);
    const float lineHeight = metricsOfHeight(kMessageTextHeight).height;
    style.y = centreY - measured.height / 2.0F + lineHeight / 2.0F;
    gui::addTextSprites(gui::layoutText(text, style, canvas.text.fonts), canvas.text.textBatch);
}

void ScrollInQueue::queue(ScrollInMessage message) { m_queue.push_back(std::move(message)); }

void ScrollInQueue::update(std::uint64_t nowMs, const HudSound& audio) {
    m_nowMs = nowMs;
    if (m_queue.empty()) {
        m_startMs.reset();
        return;
    }
    if (m_startMs && nowMs - *m_startMs >= std::uint64_t{m_queue.front().ms} + kScrollInTailMs) {
        m_queue.pop_front();
        m_startMs.reset();
    }
    if (!m_startMs && !m_queue.empty()) {
        m_startMs = nowMs;
        const std::optional<int> cue = m_queue.front().cue;
        if (cue) {
            audio.playCue(*cue);
        }
    }
}

float ScrollInQueue::fade() const {
    if (!showing()) {
        return 0.0F;
    }
    const std::uint64_t age = m_nowMs - m_startMs.value_or(m_nowMs);
    const std::uint64_t ms = m_queue.front().ms;
    if (age <= ms) {
        return 1.0F;
    }
    return std::max(0.0F, 1.0F - static_cast<float>(age - ms) / static_cast<float>(kScrollInTailMs));
}

void ScrollInQueue::render(const HudCanvas& canvas) const {
    if (!showing()) {
        return;
    }
    const ScrollInMessage& message = m_queue.front();
    gui::TextStyle style = messageStyle(message.place.x);
    style.fade = fade();
    if (canvas.text.fonts) {
        drawMessage(canvas, wrapText(message.text, style, canvas.text.fonts, kScrollInWrapWidth), style,
                    message.place.y);
    }
}

float ScrollInQueue::showingHeight(const gui::FontLookup& fonts) const {
    if (!showing() || !fonts) {
        return 0.0F;
    }
    const gui::TextStyle style = messageStyle(m_queue.front().place.x);
    return gui::layoutText(wrapText(m_queue.front().text, style, fonts, kScrollInWrapWidth), style, fonts).height;
}

} // namespace coney::hud
