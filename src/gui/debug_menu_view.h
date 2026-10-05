// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "graphics/render_device.h"
#include "gui/debug_text_painter.h"

namespace coney::gui {

/// The pad debug menu's renderer: draws a DebugSession's open page as the in-game list, and the Display page's
/// overlays, onto the logical screen. It holds no feature logic: each item kind has one way to be drawn, and what an
/// item shows comes from the model (debug::valueText(), the channels, the logs).
///
/// Layout, in logical pixels: a panel at the left with the breadcrumb on a title bar, the page's items (label left,
/// value right, the cursor's row highlighted), then a footer with the cursor item's help or the status line, its plot
/// when it watches a channel, the page's log lines, and the button hints. The number of item rows that fit is told to
/// the navigator, which keeps its cursor in view.
///
/// Coney's own tool: the original has no debug menu (docs/research/debug.md#not-present).
class DebugMenuView {
  public:
    /// The panel's left edge.
    static constexpr float kPanelX = 14.0F;
    /// The panel's top edge.
    static constexpr float kPanelY = 12.0F;
    /// The panel's width.
    static constexpr float kPanelWidth = 380.0F;
    /// Space around the text inside the panel.
    static constexpr float kPadding = 4.0F;
    /// The most log lines the footer shows.
    static constexpr std::size_t kLogRows = 6;
    /// The plot's height.
    static constexpr float kPlotHeight = 28.0F;

    /// The panel's background.
    static constexpr graphics::Rgba kPanelColour{14, 16, 24, 225};
    /// The title bar: the Warriors' red.
    static constexpr graphics::Rgba kTitleColour{150, 32, 30, 255};
    /// The cursor's row.
    static constexpr graphics::Rgba kCursorColour{220, 170, 40, 110};
    /// Labels.
    static constexpr graphics::Rgba kTextColour{235, 235, 235, 255};
    /// Values.
    static constexpr graphics::Rgba kValueColour{250, 205, 90, 255};
    /// Help, hints and log lines.
    static constexpr graphics::Rgba kDimColour{150, 156, 170, 255};
    /// Overlay outlines.
    static constexpr graphics::Rgba kOverlayColour{80, 220, 120, 200};

    /// Draws the session through `painter` onto `device` (between its beginFrame() and present()): the overlays the
    /// Display page switched on, then the menu when it is open. Sets the navigator's visible rows to what fits.
    void draw(debug::DebugSession& session, DebugTextPainter& painter, graphics::RenderDevice& device);

  private:
    // The open menu's panel.
    void drawMenu(debug::DebugSession& session, DebugTextPainter& painter);
    // The Display page's overlays.
    void drawOverlays(debug::DebugSession& session, DebugTextPainter& painter);
    // One item's row at `y`.
    void drawItem(const debug::MenuNavigator& navigator, const debug::MenuItem& item, bool underCursor, float y,
                  DebugTextPainter& painter);
    // A line graph of `values` in the rectangle.
    void drawPlot(const std::vector<float>& values, float x, float y, float width, float height);
    // A filled rectangle.
    void rect(float x, float y, float width, float height, graphics::Rgba colour);
    // A rectangle's outline, one pixel wide.
    void outline(float x, float y, float width, float height, graphics::Rgba colour);

    std::vector<graphics::LogicalQuad> m_quads; // this frame's flat quads, drawn before the text
};

/// `text` cut to at most `width` logical pixels in `painter`, ending in `~` when cut.
[[nodiscard]] std::string fitText(const DebugTextPainter& painter, std::string text, float width);

} // namespace coney::gui
