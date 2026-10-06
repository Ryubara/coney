// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/option_grid.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "core/assert.h"
#include "graphics/font.h"

namespace coney::gui {

namespace {

// The style an item's text, or its separator, is laid out in: left-aligned from `x`, centred on `y`.
TextStyle itemStyle(float x, float y, float scale, graphics::Rgba colour, int fontSlot) {
    TextStyle style;
    style.x = x;
    style.y = y;
    style.scale = scale;
    style.colour = colour;
    style.alignment = TextAlignment::Left;
    style.fontSlot = fontSlot;
    style.shadowAlpha = TextWidget::kShadowAlpha;
    return style;
}

} // namespace

void OptionGrid::setup(OptionGridSetup setup) {
    if (setup.rows.size() > kMaxRowCounts) {
        setup.rows.resize(kMaxRowCounts);
    }
    m_setup = std::move(setup);
    m_items.clear();
    m_texts.clear();
    m_rowStarts.clear();
    m_selected = 0;
    m_focused = false;
}

bool OptionGrid::addItem(OptionGridItem item) {
    if (m_items.size() >= kMaxItems) {
        return false;
    }
    auto text = std::make_unique<TextWidget>();
    text->setText(item.text);
    m_texts.push_back(std::move(text));
    m_items.push_back(std::move(item));
    buildRows();
    return true;
}

void OptionGrid::buildRows() {
    m_rowStarts.clear();
    std::size_t next = 0;
    for (const std::size_t count : m_setup.rows) {
        if (next >= m_items.size()) {
            return;
        }
        m_rowStarts.push_back(next);
        next += std::max<std::size_t>(count, 1);
    }
    // Items past the counts: a row each (Coney's choice).
    for (; next < m_items.size(); ++next) {
        m_rowStarts.push_back(next);
    }
}

std::pair<std::size_t, std::size_t> OptionGrid::cell(std::size_t index) const {
    CONEY_ASSERT(index < m_items.size());
    const auto after = std::ranges::upper_bound(m_rowStarts, index);
    const auto row = static_cast<std::size_t>(after - m_rowStarts.begin()) - 1;
    return {row, index - m_rowStarts[row]};
}

void OptionGrid::select(std::size_t index) {
    CONEY_ASSERT(index < m_items.size());
    m_selected = index;
}

void OptionGrid::takeFocus(MenuInput& input, std::uint64_t nowMs) {
    // Clears the input record and stamps the time: the d-pad waits a moment, the last command is forgotten.
    input.focus(nowMs);
    input.setDpadRepeat(true);
    m_input = &input;
    m_focused = true;
}

std::optional<int> OptionGrid::handle(MenuCommand command) {
    if (m_items.empty()) {
        return std::nullopt;
    }
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down:
        // Up and down need two rows; with one they do nothing.
        if (m_rowStarts.size() >= 2) {
            moveTo(verticalTarget(command == MenuCommand::Up ? -1 : 1));
        }
        return std::nullopt;
    case MenuCommand::Left:
        moveTo(horizontalTarget(-1));
        return std::nullopt;
    case MenuCommand::Right:
        moveTo(horizontalTarget(1));
        return std::nullopt;
    case MenuCommand::Accept:
        return m_items[m_selected].enabled ? std::optional<int>(m_items[m_selected].code) : std::nullopt;
    case MenuCommand::Back:
        break;
    }
    return std::nullopt;
}

std::size_t OptionGrid::verticalTarget(int step) const {
    const bool wraps = m_items.size() >= 3;
    const auto rowCount = static_cast<std::ptrdiff_t>(m_rowStarts.size());
    const auto [startRow, column] = cell(m_selected);
    auto row = static_cast<std::ptrdiff_t>(startRow);
    // Row by row in the direction, keeping the column (clamped to the row), until a selectable item.
    for (std::ptrdiff_t tried = 1; tried < rowCount; ++tried) {
        row += step;
        if (row < 0 || row >= rowCount) {
            if (!wraps) {
                return m_selected;
            }
            row = (row + rowCount) % rowCount;
        }
        const auto rowIndex = static_cast<std::size_t>(row);
        const std::size_t first = m_rowStarts[rowIndex];
        const std::size_t end = rowIndex + 1 < m_rowStarts.size() ? m_rowStarts[rowIndex + 1] : m_items.size();
        const std::size_t target = std::min(first + column, end - 1);
        if (m_items[target].enabled) {
            return target;
        }
    }
    return m_selected;
}

std::size_t OptionGrid::horizontalTarget(int step) const {
    const bool wraps = m_items.size() >= 3;
    // The range walked: every item, or the selection's row.
    std::size_t first = 0;
    std::size_t end = m_items.size();
    if (m_setup.stayInRow) {
        const std::size_t row = cell(m_selected).first;
        first = m_rowStarts[row];
        end = row + 1 < m_rowStarts.size() ? m_rowStarts[row + 1] : m_items.size();
    }
    const auto count = static_cast<std::ptrdiff_t>(end - first);
    auto position = static_cast<std::ptrdiff_t>(m_selected - first);
    for (std::ptrdiff_t tried = 1; tried < count; ++tried) {
        position += step;
        if (position < 0 || position >= count) {
            if (!wraps) {
                return m_selected;
            }
            position = (position + count) % count;
        }
        const std::size_t target = first + static_cast<std::size_t>(position);
        if (m_items[target].enabled) {
            return target;
        }
    }
    return m_selected;
}

void OptionGrid::moveTo(std::size_t index) {
    // A move that cannot leave its item: the refused cue, and the d-pad stops repeating until the next move.
    if (index == m_selected) {
        playCue(kRefusedCue);
        if (m_input != nullptr) {
            m_input->setDpadRepeat(false);
        }
        return;
    }
    m_selected = index;
    playCue(m_setup.moveCue);
    if (m_input != nullptr) {
        m_input->setDpadRepeat(true);
    }
}

void OptionGrid::playCue(int cue) const {
    if (m_setup.playCue) {
        m_setup.playCue(cue);
    }
}

void OptionGrid::update(const GuiFrame& frame) {
    for (const auto& text : m_texts) {
        text->update(frame);
    }
}

float OptionGrid::rowPitch(float scale) const {
    const graphics::FontMetrics metrics = graphics::fontMetrics(scale);
    return metrics.height + metrics.lineGap + kRowPitchExtra + m_setup.rowGap;
}

float OptionGrid::itemWidth(std::size_t index, const GuiCanvas& canvas) const {
    const OptionGridItem& item = m_items[index];
    float width =
        layoutText(item.text, itemStyle(0.0F, 0.0F, item.scale, item.colour, item.fontSlot), canvas.fonts).width;
    if (item.separator) {
        width +=
            layoutText(kSeparator, itemStyle(0.0F, 0.0F, item.scale, item.colour, kTextFontSlot), canvas.fonts).width;
    }
    return width;
}

std::pair<float, float> OptionGrid::itemPosition(std::size_t index, const GuiCanvas& canvas) const {
    CONEY_ASSERT(index < m_items.size());
    const auto [row, column] = cell(index);
    // The row's y: each row one pitch (of its first item's size) below the one before.
    float y = m_setup.y;
    for (std::size_t r = 0; r < row; ++r) {
        y += rowPitch(m_items[m_rowStarts[r]].scale);
    }
    if (!canvas.fonts) {
        return {m_setup.leftX.value_or(m_setup.centreX), y};
    }
    const std::size_t first = m_rowStarts[row];
    float x = 0.0F;
    for (std::size_t i = first; i < first + column; ++i) {
        x += itemWidth(i, canvas);
    }
    if (m_setup.leftX) {
        return {*m_setup.leftX + x, y};
    }
    // A centred row: its whole width centred on centreX.
    const std::size_t end = row + 1 < m_rowStarts.size() ? m_rowStarts[row + 1] : m_items.size();
    float rowWidth = 0.0F;
    for (std::size_t i = first; i < end; ++i) {
        rowWidth += itemWidth(i, canvas);
    }
    return {m_setup.centreX - rowWidth / 2.0F + x, y};
}

graphics::Rgba OptionGrid::itemColour(std::size_t index) const {
    CONEY_ASSERT(index < m_items.size());
    const OptionGridItem& item = m_items[index];
    graphics::Rgba colour = index == m_selected && item.enabled && m_focused ? kSelectedGrey : item.colour;
    colour.a = kMenuGrey.a;
    return colour;
}

void OptionGrid::render(const GuiCanvas& canvas) const {
    if (!visible() || !canvas.fonts || !canvas.textBatch) {
        return;
    }
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const OptionGridItem& item = m_items[i];
        const auto [x, y] = itemPosition(i, canvas);
        TextStyle style = itemStyle(x, y, item.scale, itemColour(i), item.fontSlot);
        style.timeMs = m_texts[i]->style().timeMs;
        const TextLayout text = layoutText(item.text, style, canvas.fonts);
        addTextSprites(text, canvas.textBatch);
        if (item.separator) {
            // The separator in the item's own colour, whatever the selection.
            graphics::Rgba colour = item.colour;
            colour.a = kMenuGrey.a;
            addTextSprites(
                layoutText(kSeparator, itemStyle(x + text.width, y, item.scale, colour, kTextFontSlot), canvas.fonts),
                canvas.textBatch);
        }
    }
}

int OptionGrid::code(std::size_t index) const {
    CONEY_ASSERT(index < m_items.size());
    return m_items[index].code;
}

const OptionGridItem& OptionGrid::item(std::size_t index) const {
    CONEY_ASSERT(index < m_items.size());
    return m_items[index];
}

} // namespace coney::gui
