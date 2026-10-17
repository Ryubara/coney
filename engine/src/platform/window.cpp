#include "platform/platform.hpp"

#include <SDL3/SDL.h>

namespace coney::platform {

Result<std::unique_ptr<Window>, std::string> Window::open(Platform& platform, const core::WindowSettings& settings) {
    SDL_Window* window =
        SDL_CreateWindow(settings.title.c_str(), settings.width, settings.height, SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        return Failure{std::string("SDL_CreateWindow failed: ") + SDL_GetError()};
    }
    platform.logger().info(core::log_category::kWindow, "opened \"{}\" at {}x{}", settings.title, settings.width,
                           settings.height);
    return std::unique_ptr<Window>(new Window(platform, window));
}

Window::~Window() {
    SDL_DestroyWindow(window_);
    platform_.logger().debug(core::log_category::kWindow, "closed");
}

Window::Size Window::pixel_size() const {
    Size size;
    if (!SDL_GetWindowSizeInPixels(window_, &size.width, &size.height)) {
        platform_.logger().warning(core::log_category::kWindow, "cannot read the window size: {}", SDL_GetError());
    }
    return size;
}

void Window::request_size(int width, int height) {
    if (!SDL_SetWindowSize(window_, width, height)) {
        platform_.logger().warning(core::log_category::kWindow, "cannot resize the window: {}", SDL_GetError());
    }
}

void Window::request_close() {
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = SDL_GetWindowID(window_);
    if (!SDL_PushEvent(&event)) {
        platform_.logger().warning(core::log_category::kWindow, "cannot queue a close request: {}", SDL_GetError());
    }
}

} // namespace coney::platform
