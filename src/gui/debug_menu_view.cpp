// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/debug_menu_view.h"

#include <algorithm>
#include <format>
#include <utility>
#include <vector>

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::gui {

namespace {

// The button hints at the bottom of the panel.
constexpr std::string_view kHints = "X ok  O back  [] pin  /\\ reset  L1 R1 page  L2 R2 fine/coarse";
// The hints while typing.
constexpr std::string_view kEditHints = "Up/Down letter  Left/Right move  [] delete  L1/R1 history  X enter  O cancel";

} // namespace

std::string fitText(const DebugTextPainter& painter, std::string text, float width) {
    if (painter.measure(text) <= width) {
        return text;
    }
    while (!text.empty() && painter.measure(text + "~") > width) {
        text.pop_back();
    }
    return text + "~";
}

void DebugMenuView::rect(float x, float y, float width, float height, graphics::Rgba colour) {
    graphics::LogicalQuad quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.colour = colour;
    m_quads.push_back(quad);
}

void DebugMenuView::outline(float x, float y, float width, float height, graphics::Rgba colour) {
    rect(x, y, width, 1.0F, colour);
    rect(x, y + height - 1.0F, width, 1.0F, colour);
    rect(x, y, 1.0F, height, colour);
    rect(x + width - 1.0F, y, 1.0F, height, colour);
}

void DebugMenuView::drawPlot(const std::vector<float>& values, float x, float y, float width, float height) {
    rect(x, y, width, height, graphics::Rgba{0, 0, 0, 160});
    if (values.empty()) {
        return;
    }
    // Scaled to the samples' range, at least [-1, 1] around zero so a still stick reads as flat.
    const auto [low, high] = std::ranges::minmax(values);
    const float bottom = std::min(low, -1.0F);
    const float top = std::max(high, 1.0F);
    const float span = top - bottom;
    const float zeroY = y + height - (0.0F - bottom) / span * height;
    rect(x, zeroY, width, 1.0F, graphics::Rgba{90, 90, 110, 200});
    // One column per sample, newest at the right; a dot at the value.
    const float column = width / static_cast<float>(values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float valueY = y + height - (values[i] - bottom) / span * height;
        rect(x + static_cast<float>(i) * column, std::clamp(valueY - 1.0F, y, y + height - 2.0F),
             std::max(column, 1.0F), 2.0F, kValueColour);
    }
}

void DebugMenuView::drawItem(const debug::MenuNavigator& navigator, const debug::MenuItem& item, bool underCursor,
                             float y, DebugTextPainter& painter) {
    const float left = kPanelX + kPadding;
    const float right = kPanelX + kPanelWidth - kPadding;
    if (underCursor) {
        rect(kPanelX + 1.0F, y, kPanelWidth - 2.0F, painter.lineHeight(), kCursorColour);
    }
    // The value: the text being typed when editing this item, otherwise what the model says the item shows.
    std::string value;
    if (underCursor && navigator.editing()) {
        value = navigator.editText();
    } else if (item.kind == debug::ItemKind::Log) {
        value = std::format("{} lines", item.lines ? item.lines().size() : 0);
    } else {
        value = debug::valueText(item);
    }
    const float valueRoom = (right - left) * 0.55F;
    value = fitText(painter, std::move(value), valueRoom);
    const float valueWidth = painter.measure(value);
    const float labelRoom = right - left - valueWidth - 8.0F;
    const graphics::Rgba labelColour =
        item.kind == debug::ItemKind::Watch || item.kind == debug::ItemKind::Log ? kDimColour : kTextColour;
    painter.text(fitText(painter, item.label, labelRoom), left, y, labelColour);
    painter.text(value, right - valueWidth, y, kValueColour);
    // While typing, a bar under the character the spinners turn.
    if (underCursor && navigator.editing()) {
        const std::string& text = navigator.editText();
        const std::size_t at = std::min(navigator.editCursor(), text.size());
        const float before = painter.measure(text.substr(0, at));
        const float glyph = std::max(painter.measure(text.substr(at, 1)), 3.0F);
        rect(right - valueWidth + before, y + painter.lineHeight() - 1.0F, glyph, 1.0F, kTextColour);
    }
}

void DebugMenuView::drawMenu(debug::DebugSession& session, DebugTextPainter& painter) {
    debug::MenuNavigator& navigator = session.navigator();
    const debug::MenuPage* page = navigator.page();
    if (page == nullptr) {
        return;
    }
    const float line = painter.lineHeight();
    const float left = kPanelX + kPadding;
    const float textWidth = kPanelWidth - 2.0F * kPadding;
    const debug::MenuItem* current = navigator.current();

    // The footer's contents, so the rows can take the rest of the screen.
    std::vector<std::string> logLines;
    for (const debug::MenuItem& item : page->items()) {
        if (item.kind == debug::ItemKind::Log && item.lines) {
            auto lines = item.lines();
            const std::size_t from = lines.size() > kLogRows ? lines.size() - kLogRows : 0;
            logLines.insert(logLines.end(), lines.begin() + static_cast<std::ptrdiff_t>(from), lines.end());
        }
    }
    const debug::TimeSeries* plot =
        current != nullptr && !current->channel.empty() ? session.model().channel(current->channel) : nullptr;
    const float footerHeight = line * static_cast<float>(2 + logLines.size()) + (plot != nullptr ? kPlotHeight + 4 : 0);
    const float bottom = graphics::kLogicalHeight - kPanelY;
    const float rowsTop = kPanelY + line + 2.0F * kPadding;
    const auto rows = static_cast<std::size_t>(std::max(1.0F, (bottom - footerHeight - rowsTop - kPadding) / line));
    navigator.setVisibleRows(rows);

    // The panel and the title bar with the breadcrumb.
    const std::size_t count = page->items().size();
    const std::size_t shown = std::min(rows, count - std::min(count, navigator.scrollTop()));
    const float panelHeight =
        rowsTop - kPanelY + line * static_cast<float>(std::max<std::size_t>(shown, 1)) + footerHeight + 2.0F * kPadding;
    rect(kPanelX, kPanelY, kPanelWidth, panelHeight, kPanelColour);
    rect(kPanelX, kPanelY, kPanelWidth, line + kPadding, kTitleColour);
    std::string crumbs;
    for (const std::string& title : navigator.breadcrumb()) {
        crumbs += (crumbs.empty() ? "" : " > ") + title;
    }
    const std::string position = count == 0 ? std::string{} : std::format("{}/{}", navigator.cursor() + 1, count);
    painter.text(fitText(painter, crumbs, textWidth - painter.measure(position) - 8.0F), left,
                 kPanelY + kPadding * 0.5F, kTextColour);
    painter.text(position, kPanelX + kPanelWidth - kPadding - painter.measure(position), kPanelY + kPadding * 0.5F,
                 kTextColour);

    // The rows.
    float y = rowsTop;
    for (std::size_t i = navigator.scrollTop(); i < count && i < navigator.scrollTop() + rows; ++i) {
        drawItem(navigator, page->items()[i], i == navigator.cursor(), y, painter);
        y += line;
    }
    if (count == 0) {
        painter.text("(empty)", left, y, kDimColour);
        y += line;
    }

    // The footer: help or status, the plot, the log, the hints.
    y += kPadding;
    rect(kPanelX + kPadding, y - 2.0F, textWidth, 1.0F, kDimColour);
    const std::string help = !navigator.status().empty() ? navigator.status()
                             : current != nullptr        ? current->help
                                                         : std::string{};
    painter.text(fitText(painter, help, textWidth), left, y, navigator.status().empty() ? kDimColour : kValueColour);
    y += line;
    if (plot != nullptr) {
        drawPlot(plot->values(), left, y + 2.0F, textWidth, kPlotHeight);
        y += kPlotHeight + 4.0F;
    }
    for (const std::string& logLine : logLines) {
        painter.text(fitText(painter, logLine, textWidth), left, y, kTextColour);
        y += line;
    }
    painter.text(fitText(painter, std::string(navigator.editing() ? kEditHints : kHints), textWidth), left, y,
                 kDimColour);
}

void DebugMenuView::drawOverlays(debug::DebugSession& session, DebugTextPainter& painter) {
    const debug::DisplayOptions& display = session.display();
    if (display.logicalBounds) {
        outline(0.0F, 0.0F, graphics::kLogicalWidth, graphics::kLogicalHeight, kOverlayColour);
    }
    if (display.safeArea) {
        const graphics::OverlayCamera camera;
        const graphics::LogicalPoint topLeft = camera.guiToLogical(0.0F, 0.0F);
        const graphics::LogicalPoint bottomRight = camera.guiToLogical(1.0F, 1.0F);
        outline(topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y,
                graphics::Rgba{230, 200, 60, 200});
    }
    // The corner lines (frame stats, FPS counter), right-aligned and stacked, each on its own backing.
    float y = 4.0F;
    for (const std::string& text : session.cornerLines()) {
        const float width = painter.measure(text);
        const float x = graphics::kLogicalWidth - width - 8.0F;
        rect(x - 3.0F, y, width + 6.0F, painter.lineHeight() + 2.0F, kPanelColour);
        painter.text(text, x, y + 1.0F, kTextColour);
        y += painter.lineHeight() + 2.0F;
    }
}

void DebugMenuView::draw(debug::DebugSession& session, DebugTextPainter& painter, graphics::RenderDevice& device) {
    m_quads.clear();
    drawOverlays(session, painter);
    if (session.navigator().isOpen()) {
        drawMenu(session, painter);
    }
    // The flat quads under the text.
    if (!m_quads.empty()) {
        device.drawQuads(nullptr, m_quads);
    }
    painter.flush(device);
}

} // namespace coney::gui
