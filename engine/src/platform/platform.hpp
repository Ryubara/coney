#pragma once

// The platform layer: the only part of the engine that talks to SDL. Nothing outside src/platform/ includes SDL
// headers (the engine_sdl_confined test checks this); SDL types appear here only as opaque forward declarations.

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "core/log.hpp"
#include "core/settings.hpp"
#include "result.hpp"

struct SDL_Window;
struct SDL_GPUDevice;

namespace coney::platform {

// The user (or the system) asked to close the window or quit.
struct CloseRequested {};
// The window's drawable size changed; in pixels.
struct Resized {
    int width = 0;
    int height = 0;
};
using Event = std::variant<CloseRequested, Resized>;

// Starts SDL's video subsystem and routes SDL's log output to `logger` (category "sdl.<SDL category>"). One at a
// time; everything else in the platform layer needs it alive.
class Platform {
  public:
    [[nodiscard]] static Result<std::unique_ptr<Platform>, std::string> start(core::Logger& logger);
    ~Platform();
    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    // The next pending event, or nothing when the queue is empty. Never blocks.
    [[nodiscard]] std::optional<Event> poll_event();

    [[nodiscard]] core::Logger& logger() noexcept { return logger_; }

  private:
    explicit Platform(core::Logger& logger) : logger_(logger) {}
    core::Logger& logger_;
};

class Window {
  public:
    // A resizable window with the settings' title and size.
    [[nodiscard]] static Result<std::unique_ptr<Window>, std::string> open(Platform& platform,
                                                                           const core::WindowSettings& settings);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    struct Size {
        int width = 0;
        int height = 0;
    };
    [[nodiscard]] Size pixel_size() const;
    // Asks for a new size; a Resized event follows once the system applies it.
    void request_size(int width, int height);
    // Queues a CloseRequested event, as if the user had closed the window.
    void request_close();

    [[nodiscard]] SDL_Window* sdl_window() const noexcept { return window_; }
    [[nodiscard]] Platform& platform() const noexcept { return platform_; }

  private:
    Window(Platform& platform, SDL_Window* window) : platform_(platform), window_(window) {}
    Platform& platform_;
    SDL_Window* window_;
};

// A GPU device bound to a window's swapchain, through SDL's GPU API.
class GpuContext {
  public:
    [[nodiscard]] static Result<std::unique_ptr<GpuContext>, std::string> create(Window& window);
    ~GpuContext();
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    // Clears the window to `colour` and presents it. False if the frame could not be submitted; a frame skipped
    // because the window has no drawable area (for example while minimised) still returns true.
    bool clear(const core::Colour& colour);

    [[nodiscard]] std::string_view driver_name() const noexcept { return driver_; }

  private:
    GpuContext(Window& window, SDL_GPUDevice* device, std::string driver)
        : window_(window), device_(device), driver_(std::move(driver)) {}
    Window& window_;
    SDL_GPUDevice* device_;
    std::string driver_;
};

} // namespace coney::platform
