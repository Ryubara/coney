// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_look.h"

#include <algorithm>
#include <array>

#include "core/assert.h"
#include "gui/text_layout.h"
#include "gui/usage_info.h"

namespace coney::gui {

namespace pm_look {

float gridY(std::size_t rows) {
    static constexpr std::array kGridY{0.81F, 0.76F, 0.71F, 0.657F};
    return kGridY.at(std::clamp<std::size_t>(rows, 1, kGridY.size()) - 1);
}

float titleY(std::size_t rows) {
    static constexpr std::array kTitleY{0.745F, 0.70F, 0.65F, 0.60F};
    return kTitleY.at(std::clamp<std::size_t>(rows, 1, kTitleY.size()) - 1);
}

} // namespace pm_look

void placePmText(TextWidget& widget, std::string_view text, float x, float y, float scale, graphics::Rgba colour,
                 int fontSlot) {
    if (!widget.initialised()) {
        widget.init();
    }
    widget.setText(text);
    TextStyle& style = widget.style();
    style.x = x;
    style.y = y;
    style.boxWidth = 0.0F;
    style.alignment = TextAlignment::Left;
    style.scale = scale;
    style.colour = colour;
    style.fontSlot = fontSlot;
}

void PmGridView::build(const PmChoices& choices, float top) {
    clear();
    m_top = top;
    const std::vector<std::size_t>& rows = choices.rows();
    std::size_t index = 0;
    for (const std::size_t rowSize : rows) {
        for (std::size_t column = 0; column < rowSize; ++column, ++index) {
            auto text = std::make_unique<TextWidget>();
            placePmText(*text, choices.items().at(index).text, pm_look::kX, 0.0F, pm_look::kItemScale, pm_look::kRed,
                        kBigFontSlot);
            m_items.push_back(std::move(text));
            if (column + 1 < rowSize) {
                auto separator = std::make_unique<TextWidget>();
                placePmText(*separator, pm_look::kSeparator, pm_look::kX, 0.0F, pm_look::kItemScale, pm_look::kRed,
                            kTextFontSlot);
                m_separators.push_back(std::move(separator));
            } else {
                m_separators.push_back(nullptr);
            }
        }
    }
    m_placed = false;
}

void PmGridView::render(const PmChoices& choices, const GuiCanvas& canvas) {
    // Place each row's items left to right once the fonts can measure them.
    if (!m_placed && canvas.fonts) {
        std::size_t index = 0;
        for (std::size_t row = 0; row < choices.rows().size(); ++row) {
            float x = pm_look::kX;
            const float y = m_top + static_cast<float>(row) * pm_look::kRowPitch;
            for (std::size_t column = 0; column < choices.rows().at(row); ++column, ++index) {
                TextWidget& item = *m_items.at(index);
                item.style().x = x;
                item.style().y = y;
                x += item.layout(canvas).width;
                if (TextWidget* separator = m_separators.at(index).get()) {
                    separator->style().x = x;
                    separator->style().y = y;
                    x += separator->layout(canvas).width;
                }
            }
        }
        m_placed = true;
    }
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        m_items.at(i)->style().colour = i == choices.selected() ? pm_look::kGrey : pm_look::kRed;
        m_items.at(i)->render(canvas);
        if (const TextWidget* separator = m_separators.at(i).get()) {
            separator->render(canvas);
        }
    }
}

void PmGridView::clear() {
    m_items.clear();
    m_separators.clear();
    m_placed = false;
}

const TextWidget& PmGridView::item(std::size_t index) const {
    CONEY_ASSERT(index < m_items.size());
    return *m_items.at(index);
}

void placePmUsage(TextWidget& widget, const PmShared& shared) {
    placePmText(widget, shared.string(UsageInfo::kMenuUsageString), pm_look::kX, pm_look::kUsageY, pm_look::kUsageScale,
                pm_look::kGrey, kTextFontSlot);
}

} // namespace coney::gui
