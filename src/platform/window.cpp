// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/window.h"

#include <string>
#include <utility>

#include <SDL3/SDL.h>

namespace coney::platform {

namespace {

// Recovers the SDL type the header keeps opaque.
SDL_Window* asSdl(void* handle) { return static_cast<SDL_Window*>(handle); }

// The error for an SDL call that failed, carrying SDL's own error text.
std::unexpected<Error> sdlFailure(std::string_view what) {
    return std::unexpected(Error{ErrorCode::PlatformFailure, std::string(what) + " failed: " + SDL_GetError()});
}

} // namespace

std::expected<Window, Error> Window::open(const WindowDesc& desc) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return sdlFailure("SDL_Init");
    }
    // SDL needs a null-terminated title, which a string_view does not promise.
    const std::string title(desc.title);
    SDL_Window* window = SDL_CreateWindow(title.c_str(), desc.width, desc.height, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        // Read the error before SDL_Quit, which may replace it.
        auto failure = sdlFailure("SDL_CreateWindow");
        SDL_Quit();
        return failure;
    }
    return Window(window);
}

Window::Window(void* handle) : m_handle(handle) {}

Window::Window(Window&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        close();
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

Window::~Window() { close(); }

void Window::close() noexcept {
    if (m_handle == nullptr) {
        return;
    }
    SDL_DestroyWindow(asSdl(m_handle));
    m_handle = nullptr;
    SDL_Quit();
}

bool Window::pumpEvents() {
    // Drain the whole queue every frame: leaving events behind makes the OS think the window has stopped responding.
    bool keepRunning = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            keepRunning = false;
        }
    }
    return keepRunning;
}

} // namespace coney::platform
