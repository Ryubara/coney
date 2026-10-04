// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string_view>

#include "core/error.h"

// No SDL type appears in this header, so code that holds a Window stays platform-neutral and never includes SDL.

namespace coney::platform {

/// What the window should look like when it opens.
struct WindowDesc {
    std::string_view title = "Coney";
    int width = 1280;
    int height = 720;
};

/// Initialises SDL's video subsystem and owns one window. Move-only; destroying it closes the window and shuts SDL
/// down. Only one Window should exist at a time, because its destructor shuts all of SDL down.
class Window {
  public:
    /// Initialises SDL video and opens a resizable window. Fails with ErrorCode::PlatformFailure, carrying SDL's own
    /// error text, when SDL cannot start or cannot create the window; SDL is shut down again in either case.
    [[nodiscard]] static std::expected<Window, Error> open(const WindowDesc& desc);

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    /// Handles every pending OS event. Returns false once the user has asked to quit.
    [[nodiscard]] bool pumpEvents();

  private:
    explicit Window(void* handle);

    /// Destroys the window and shuts SDL down, if this object still owns them.
    void close() noexcept;

    void* m_handle = nullptr; // SDL_Window*, kept opaque here; null after a move, so only one object cleans up
};

} // namespace coney::platform
