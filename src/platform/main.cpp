// SPDX-License-Identifier: GPL-3.0-or-later

// Entry point. SDL_main.h lets SDL provide the right entry on each OS (WinMain on Windows), which is why main lives
// in src/platform/: it is the one function that is part of the operating-system boundary.

#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

#include <SDL3/SDL_main.h>

#include "core/options.h"
#include "platform/render_engine.h"
#include "platform/window.h"

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    auto options = coney::parseOptions(args);
    if (!options) {
        const std::string_view usage = coney::usageText();
        std::fprintf(stderr, "coney: %s\n\n%.*s", options.error().message.c_str(), static_cast<int>(usage.size()),
                     usage.data());
        return 2;
    }
    if (options->showHelp) {
        const std::string_view usage = coney::usageText();
        std::fwrite(usage.data(), 1, usage.size(), stdout);
        return 0;
    }

    auto window = coney::platform::Window::open({});
    if (!window) {
        std::fprintf(stderr, "coney: %s\n", window.error().message.c_str());
        return 1;
    }
    // librw starts after the window and, being declared later, stops before it: the GL3 backend will need the
    // window to exist for the whole life of the engine, so keep that order now.
    auto engine = coney::platform::RenderEngine::start();
    if (!engine) {
        std::fprintf(stderr, "coney: %s\n", engine.error().message.c_str());
        return 1;
    }

    // No sleeping or timing here yet: frame pacing arrives with the renderer, and the engine's test mode (a fixed
    // timestep for reference comparisons; AGENTS.md, "Checks") must stay possible, so the loop must not depend on
    // the wall clock.
    // A local copy: clang-tidy cannot prove that the optional inside `options` stays set across the loop.
    const std::optional<int> frameLimit = options->frameLimit;
    int frames = 0;
    while (window->pumpEvents()) {
        ++frames;
        if (frameLimit && frames >= *frameLimit) {
            break;
        }
    }
    return 0;
}
