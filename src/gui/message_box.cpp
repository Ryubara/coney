// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/message_box.h"

#include <utility>

#include "graphics/font.h"
#include "gui/colour_table.h"

namespace coney::gui {

void MessageBox::setMessage(std::string_view text, MessageStyle style) {
    m_message.init();
    m_message.setText(text);
    if (style == MessageStyle::Centre) {
        m_message.setup(TextWidgetSetup{.x = 0.5F,
                                        .y = 0.5F,
                                        .scale = 1.0F,
                                        .colour = kMenuGrey,
                                        .alignment = TextAlignment::Centre,
                                        .fontSlot = kBigFontSlot,
                                        .wrapWidth = kMessageWrap});
    } else {
        m_message.setup(TextWidgetSetup{.x = 0.5F,
                                        .y = 0.9F,
                                        .scale = 1.0F,
                                        .colour = kCornerColour,
                                        .alignment = TextAlignment::Centre,
                                        .fontSlot = kTextFontSlot});
    }
}

void MessageBox::showTimed(std::string_view text, std::uint64_t durationMs, MessageStyle style, std::uint64_t nowMs) {
    setMessage(text, style);
    m_endMs = nowMs + durationMs;
    m_open = true;
    m_dialog = false;
    m_chosen.reset();
    m_input = nullptr;
}

void MessageBox::showChoice(std::string_view text, const std::vector<std::string>& labels, std::size_t defaultChoice,
                            std::string_view usage, MenuInput& input, std::uint64_t nowMs) {
    setMessage(text, MessageStyle::Centre);
    // One row of choices, centred; a single choice gets an empty label.
    m_choices.init();
    m_choices.setup(OptionGridSetup{
        .y = kChoiceY, .rows = {labels.size()}, .centreX = 0.5F, .moveCue = kMoveCue, .playCue = [this](int cue) {
            playCue(cue);
        }});
    if (labels.size() == 1) {
        m_choices.addItem(
            OptionGridItem{.text = "", .code = 0, .colour = kDimGrey, .scale = 1.0F, .fontSlot = kTextFontSlot});
    } else {
        for (std::size_t i = 0; i < labels.size(); ++i) {
            m_choices.addItem(OptionGridItem{.text = labels[i],
                                             .code = static_cast<int>(i),
                                             .separator = i + 1 < labels.size(),
                                             .colour = kDimGrey,
                                             .scale = 1.0F,
                                             .fontSlot = kTextFontSlot});
        }
    }
    if (defaultChoice < m_choices.items()) {
        m_choices.select(defaultChoice);
    }
    m_choices.takeFocus(input, nowMs);
    m_usage.init();
    m_usage.place(0.5F, kUsageY, false);
    m_usage.setLegend(usage);
    m_input = &input;
    m_open = true;
    m_dialog = true;
    m_chosen.reset();
}

void MessageBox::update(const GuiFrame& frame) {
    if (!m_open) {
        return;
    }
    m_message.update(frame);
    if (!m_dialog) {
        if (frame.timeMs >= m_endMs) {
            m_open = false;
        }
        return;
    }
    m_choices.update(frame);
    m_usage.update(frame);
    if (frame.pad == nullptr || m_input == nullptr) {
        return;
    }
    const std::optional<MenuCommand> command = m_input->dispatch(*frame.pad, frame.timeMs);
    if (!command) {
        return;
    }
    if (const std::optional<int> code = m_choices.handle(*command)) {
        // Accept: the cue, then the choice (the original waits for the cue to finish before its callback).
        playCue(kAcceptCue);
        m_chosen = static_cast<std::size_t>(*code);
        m_open = false;
    }
}

void MessageBox::render(const GuiCanvas& canvas) const {
    if (!m_open || !visible() || !canvas.fonts || !canvas.textBatch) {
        return;
    }
    // The message's lines centred as a block on its y: the first line moves up by half the block's extra height.
    TextLayout text = m_message.layout(canvas);
    if (text.lines > 1) {
        const float lineHeight = graphics::fontMetrics(m_message.style().scale).height;
        const float shift = (text.height - lineHeight) / 2.0F;
        for (TextSprite& sprite : text.sprites) {
            sprite.sprite.position.y += shift; // overlay y is up: moving the text up adds
        }
    }
    addTextSprites(text, canvas.textBatch);
    if (m_dialog) {
        m_choices.render(canvas);
        m_usage.render(canvas);
    }
}

void MessageBox::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

} // namespace coney::gui
