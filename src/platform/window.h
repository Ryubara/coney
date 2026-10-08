// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string_view>

// No SDL type appears in this header, so code that holds a Window stays platform-neutral and never includes SDL.

namespace coney::platform {

/// What the window should look like when it opens.
struct WindowDesc {
    std::string_view title = "Coney"; ///< The title bar text.
    int width = 960;                  ///< Client area width in pixels: 4:3, the shape of the logical screen.
    int height = 720;                 ///< Client area height in pixels.
    bool hidden = false;              ///< Keep the window off the screen, for tools that draw offscreen.
    /// Let the window take the keyboard focus when it opens. A hidden window never does: librw shows the window
    /// before Coney can hide it, which would otherwise take the focus for that moment.
    bool activate = true;
    /// The frame is the logical screen, as the original's frame buffer is: it fills the whole frame, whatever its
    /// shape, and the 3D view keeps the 4:3 display shape across it (`--render-size`). Otherwise the logical screen
    /// keeps its shape inside the window, with black bars round it.
    bool logicalFrame = false;
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

    /// Sees one OS event (an `const SDL_Event*`, opaque here) before the window does; returns true when it took the
    /// event, which then is not a key for the window (Escape does not quit).
    using EventFilter = std::function<bool(const void* event)>;

    /// Handles every pending OS event, each shown to `filter` first when there is one (the developer overlay, which
    /// takes the keyboard while a text box has it). Returns false once the user has asked to quit: closed the window,
    /// or pressed Escape when the filter did not take it.
    [[nodiscard]] bool pumpEvents(const EventFilter& filter = {});

  private:
    void* m_handle; // SDL_Window*, kept opaque here
};

} // namespace coney::platform
