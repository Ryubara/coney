// Runs under SDL's offscreen video driver; CTest sets SDL_VIDEODRIVER for these tests.

#include "platform/platform.hpp"

#include <SDL3/SDL_log.h>
#include <catch2/catch_test_macros.hpp>

using namespace coney;

namespace {

struct Fixture {
    core::SteadyClock clock;
    core::Logger logger{clock, core::LogLevel::Debug};
    core::MemorySink sink;
    Fixture() { logger.add_sink(sink); }

    [[nodiscard]] bool logged(std::string_view text) const {
        for (const auto& line : sink.lines) {
            if (line.find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("the window opens with the settings' size and reports a close request") {
    Fixture f;
    auto platform = platform::Platform::start(f.logger);
    REQUIRE(platform);
    {
        auto window = platform::Window::open(**platform, {.title = "test", .width = 320, .height = 200});
        REQUIRE(window);
        const auto size = (*window)->pixel_size();
        CHECK(size.width == 320);
        CHECK(size.height == 200);

        while ((*platform)->poll_event()) {
        }
        (*window)->request_close();
        bool closed = false;
        while (const auto event = (*platform)->poll_event()) {
            closed = closed || std::holds_alternative<platform::CloseRequested>(*event);
        }
        CHECK(closed);
    }
    CHECK(f.logged("INFO    window: opened \"test\" at 320x200"));
    CHECK(f.logged("DEBUG   window: closed"));
}

TEST_CASE("the window reports size changes") {
    Fixture f;
    auto platform = platform::Platform::start(f.logger);
    REQUIRE(platform);
    auto window = platform::Window::open(**platform, {.title = "test", .width = 320, .height = 200});
    REQUIRE(window);
    while ((*platform)->poll_event()) {
    }

    (*window)->request_size(400, 300);
    std::optional<platform::Resized> resized;
    while (const auto event = (*platform)->poll_event()) {
        if (const auto* r = std::get_if<platform::Resized>(&*event)) {
            resized = *r;
        }
    }
    REQUIRE(resized);
    CHECK(resized->width == 400);
    CHECK(resized->height == 300);
}

TEST_CASE("SDL's log output goes through the logger") {
    Fixture f;
    {
        auto platform = platform::Platform::start(f.logger);
        REQUIRE(platform);
        CHECK(f.logged("INFO    platform: SDL 3."));
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "synthetic %d", 7);
        CHECK(f.logged("WARNING sdl.video: synthetic 7"));
        f.logger.set_level(core::LogLevel::Error);
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "filtered");
        CHECK_FALSE(f.logged("filtered"));
        f.logger.set_level(core::LogLevel::Debug);
    }
    CHECK(f.logged("DEBUG   platform: SDL shut down"));
}

TEST_CASE("the GPU context clears the window") {
    Fixture f;
    auto platform = platform::Platform::start(f.logger);
    REQUIRE(platform);
    auto window = platform::Window::open(**platform, {.title = "gpu", .width = 160, .height = 120});
    REQUIRE(window);
    auto gpu = platform::GpuContext::create(**window);
    REQUIRE(gpu);
    for (int i = 0; i < 3; ++i) {
        CHECK((*gpu)->clear({0.2f, 0.4f, 0.6f, 1.0f}));
    }
}
