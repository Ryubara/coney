// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string_view>

#include "core/error.h"

// No SDL type appears in this header, so code that holds a Window stays platform-neutral and never includes SDL.

namespace coney::platform {

/// What the window should look like when it opens.
struct WindowDesc {
    std::string_view title = "Coney"; ///< The title bar text.
    int width = 1280;                 ///< Client area width in pixels.
    int height = 720;                 ///< Client area height in pixels.
};

/// Initialises SDL's video subsystem and owns one window. Move-only; destroying it closes the window and shuts SDL
/// down. Only one Window should exist at a time, because its destructor shuts all of SDL down.
class Window {
  public:
    /// Initialises SDL video and opens a resizable window. Fails with ErrorCode::PlatformFailure, carrying SDL's own
    /// error text, when SDL cannot start (SDL_Init undoes its own partial start) or cannot create the window (SDL is
    /// then shut down again).
    [[nodiscard]] static std::expected<Window, Error> open(const WindowDesc& desc);

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    /// Handles every pending OS event. Returns false once the user has asked to quit.
    [[nodiscard]] bool pumpEvents();

  private:
    // Only open() makes one, taking ownership of the SDL_Window it created.
    explicit Window(void* handle);

    /// Destroys the window and shuts SDL down, if this object still owns them.
    void close() noexcept;

    void* m_handle = nullptr; // SDL_Window*, kept opaque here; null after a move, so only one object cleans up
};

} // namespace coney::platform
