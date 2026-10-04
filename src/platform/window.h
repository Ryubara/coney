// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

// No SDL type appears in this header, so code that holds a Window stays platform-neutral and never includes SDL.

namespace coney::platform {

/// What the window should look like when it opens.
struct WindowDesc {
    std::string_view title = "Coney"; ///< The title bar text.
    int width = 960;                  ///< Client area width in pixels: 4:3, the shape of the logical screen.
    int height = 720;                 ///< Client area height in pixels.
};

/// The game's window, as the main loop sees it: a source of events.
///
/// A Window does not own the operating-system window. librw's GL3 device creates it (it must set the OpenGL attributes
/// before the window exists and create the context with it) and destroys it when the engine stops, so the window
/// lives exactly as long as the RenderEngine that opened it (render_engine.h), which hands out this view. A Window is
/// a cheap copyable handle and must not be used after that engine is gone.
class Window {
  public:
    /// Views the SDL_Window `handle`, which must not be null (checked by CONEY_ASSERT).
    explicit Window(void* handle);

    /// Handles every pending OS event. Returns false once the user has asked to quit: closed the window or pressed
    /// Escape.
    [[nodiscard]] bool pumpEvents();

  private:
    void* m_handle; // SDL_Window*, kept opaque here
};

} // namespace coney::platform
