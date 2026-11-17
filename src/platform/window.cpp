// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/window.h"

#include <SDL3/SDL.h>

#include "core/assert.h"

namespace coney::platform {

Window::Window(void* handle) : m_handle(handle) { CONEY_ASSERT(handle != nullptr); }

bool Window::pumpEvents(const EventFilter& filter) {
    // Drain the whole queue every frame: leaving events behind makes the OS think the window has stopped responding.
    bool keepRunning = true;
    const SDL_WindowID ours = SDL_GetWindowID(static_cast<SDL_Window*>(m_handle));
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        const bool taken = filter && filter(&event);
        // Quit, a close request for our window, or an Escape nobody else took.
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == ours) ||
            (!taken && event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
            keepRunning = false;
        }
    }
    return keepRunning;
}

double Window::refreshRate() const {
    // The display holding most of the window; 0 when the window is on none SDL knows.
    const SDL_DisplayID display = SDL_GetDisplayForWindow(static_cast<SDL_Window*>(m_handle));
    if (display == 0) {
        return 0.0;
    }
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(display);
    return mode != nullptr ? static_cast<double>(mode->refresh_rate) : 0.0;
}

} // namespace coney::platform
