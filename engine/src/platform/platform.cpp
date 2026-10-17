#include "platform/platform.hpp"

#include <SDL3/SDL.h>

#include <string>

namespace coney::platform {

namespace {

std::string sdl_category_name(int category) {
    switch (category) {
    case SDL_LOG_CATEGORY_APPLICATION:
        return "sdl.application";
    case SDL_LOG_CATEGORY_ERROR:
        return "sdl.error";
    case SDL_LOG_CATEGORY_ASSERT:
        return "sdl.assert";
    case SDL_LOG_CATEGORY_SYSTEM:
        return "sdl.system";
    case SDL_LOG_CATEGORY_AUDIO:
        return "sdl.audio";
    case SDL_LOG_CATEGORY_VIDEO:
        return "sdl.video";
    case SDL_LOG_CATEGORY_RENDER:
        return "sdl.render";
    case SDL_LOG_CATEGORY_INPUT:
        return "sdl.input";
    case SDL_LOG_CATEGORY_TEST:
        return "sdl.test";
    case SDL_LOG_CATEGORY_GPU:
        return "sdl.gpu";
    default:
        return "sdl." + std::to_string(category);
    }
}

core::LogLevel from_sdl_priority(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_TRACE:
    case SDL_LOG_PRIORITY_VERBOSE:
    case SDL_LOG_PRIORITY_DEBUG:
        return core::LogLevel::Debug;
    case SDL_LOG_PRIORITY_INFO:
        return core::LogLevel::Info;
    case SDL_LOG_PRIORITY_WARN:
        return core::LogLevel::Warning;
    default:
        return core::LogLevel::Error;
    }
}

SDL_LogPriority to_sdl_priority(core::LogLevel level) {
    switch (level) {
    case core::LogLevel::Debug:
        return SDL_LOG_PRIORITY_DEBUG;
    case core::LogLevel::Info:
        return SDL_LOG_PRIORITY_INFO;
    case core::LogLevel::Warning:
        return SDL_LOG_PRIORITY_WARN;
    default:
        return SDL_LOG_PRIORITY_ERROR;
    }
}

void SDLCALL route_sdl_log(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    auto& logger = *static_cast<core::Logger*>(userdata);
    logger.write(from_sdl_priority(priority), sdl_category_name(category), message);
}

} // namespace

Result<std::unique_ptr<Platform>, std::string> Platform::start(core::Logger& logger) {
    SDL_SetLogOutputFunction(route_sdl_log, &logger);
    SDL_SetLogPriorities(to_sdl_priority(logger.level()));
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::string error = std::string("SDL_Init failed: ") + SDL_GetError();
        SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
        return Failure{std::move(error)};
    }
    const int version = SDL_GetVersion();
    logger.info(core::log_category::kPlatform, "SDL {}.{}.{}, video driver {}", SDL_VERSIONNUM_MAJOR(version),
                SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), SDL_GetCurrentVideoDriver());
    return std::unique_ptr<Platform>(new Platform(logger));
}

Platform::~Platform() {
    SDL_Quit();
    logger_.debug(core::log_category::kPlatform, "SDL shut down");
    SDL_ResetLogPriorities();
    SDL_SetLogOutputFunction(SDL_GetDefaultLogOutputFunction(), nullptr);
}

std::optional<Event> Platform::poll_event() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return CloseRequested{};
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            return Resized{event.window.data1, event.window.data2};
        default:
            break; // not used yet
        }
    }
    return std::nullopt;
}

} // namespace coney::platform
