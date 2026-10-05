// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "debug/debug_session.h"
#include "graphics/font.h"
#include "graphics/render_device.h"
#include "gui/debug_menu_view.h"
#include "gui/debug_text_painter.h"

namespace coney::platform {

/// The tunables file used when `--tunables` is not given: `coney-tunables.ini` in the user's config folder (SDL's
/// preference path for Coney: `%APPDATA%\Coney\Coney\` on Windows, `~/.local/share/Coney/Coney/` on Linux,
/// `~/Library/Application Support/Coney/Coney/` on macOS). Empty when SDL cannot give one.
[[nodiscard]] std::string defaultTunablesPath();

/// The pad debug menu as the platform draws it: the session's view over a text painter, drawn at the end of every
/// frame through the renderer's present overlay, so it shows over whatever mode runs.
///
/// With the game's text font (a disc is loaded) it draws in that font; without one, in Coney's bitmap font.
class PadMenuOverlay {
  public:
    /// The line height in logical pixels with the game's font.
    static constexpr float kGameFontLine = 10.0F;

    /// An overlay for `session` (which must outlive it) drawing text in `gameFont`, or in the bitmap font without it.
    PadMenuOverlay(debug::DebugSession& session, std::optional<graphics::Font> gameFont);

    /// Draws the menu and the overlays onto `device`, inside its frame.
    void draw(graphics::RenderDevice& device) { m_view.draw(m_session, *m_painter, device); }

    /// Whether the game's font is in use.
    [[nodiscard]] bool usesGameFont() const { return m_gameFont; }

  private:
    debug::DebugSession& m_session;
    std::unique_ptr<gui::DebugTextPainter> m_painter;
    gui::DebugMenuView m_view;
    bool m_gameFont = false;
};

} // namespace coney::platform
