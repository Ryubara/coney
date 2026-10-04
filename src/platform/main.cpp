// SPDX-License-Identifier: GPL-3.0-or-later

// Entry point. SDL_main.h lets SDL provide the right entry on each OS (WinMain on Windows), which is why main lives
// in src/platform/: it is the one function that is part of the operating-system boundary.

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_main.h>

#include "core/chunk_system.h"
#include "core/game_timer.h"
#include "core/options.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/idle_mode.h"
#include "gamemodes/load_entry_mode.h"
#include "platform/render_engine.h"
#include "platform/window.h"

namespace {

void printText(std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

} // namespace

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
        printText(coney::usageText());
        return 0;
    }

    // The order follows the original's start-up where Coney has the subsystem (docs/research/boot.md): the WAD is
    // opened, then the chunk handlers are registered, then the game-mode stack runs.
    std::optional<coney::io::Wad> wad;
    // Local copies: clang-tidy cannot prove that an optional inside `options` stays set between the check and the use.
    const std::optional<std::string> discPath = options->discPath;
    if (discPath) {
        auto disc = coney::io::Disc::open(*discPath);
        if (!disc) {
            std::fprintf(stderr, "coney: %s\n", disc.error().message.c_str());
            return 1;
        }
        auto opened = coney::io::Wad::open(std::move(*disc));
        if (!opened) {
            std::fprintf(stderr, "coney: %s: %s\n", discPath->c_str(), opened.error().message.c_str());
            return 1;
        }
        coney::io::Wad& openedWad = wad.emplace(std::move(*opened));
        std::printf("%s: WARRIORS.DIR lists %zu entries\n", discPath->c_str(), openedWad.index().entries().size());
    }
    const coney::chunk::ChunkHandlerTable chunkHandlers = coney::chunk::ChunkHandlerTable::withDefaults();

    // Game time runs on the fixed 1/30 s step and never reads a real clock, which keeps the engine's test mode
    // (docs/guides/conventions.md#platform-code) possible.
    coney::GameTimer timer;
    timer.setFixedStep(true);
    coney::GameModeStack modes;
    std::optional<std::uint64_t> frameLimit;
    if (const std::optional<int> limit = options->frameLimit; limit) {
        frameLimit = static_cast<std::uint64_t>(*limit);
    }

    if (!options->loads.empty()) {
        if (!wad) {
            return 2; // parseOptions refuses --load without --disc, so this is never reached
        }
        // A console job: loading and summarising entries needs no window, so none is opened.
        coney::LoadEntryMode loader(*wad, chunkHandlers, options->loads, printText);
        modes.push(loader);
        modes.runUntilEmpty(timer, {}, frameLimit);
        return loader.failures() == 0 ? 0 : 1;
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

    // Nothing to play yet: an idle mode keeps the loop running until the window closes or the frame limit is hit.
    coney::IdleMode idle;
    modes.push(idle);
    modes.runUntilEmpty(timer, [&window] { return window->pumpEvents(); }, frameLimit);
    return 0;
}
