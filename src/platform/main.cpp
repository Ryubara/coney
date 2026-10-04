// SPDX-License-Identifier: GPL-3.0-or-later

// Entry point. SDL_main.h lets SDL provide the right entry on each OS (WinMain on Windows), which is why main lives
// in src/platform/: it is the one function that is part of the operating-system boundary.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_main.h>

#include "core/chunk_system.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "core/options.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/idle_mode.h"
#include "gamemodes/load_entry_mode.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/texture_viewer_mode.h"
#include "platform/window.h"

namespace {

// Writes `text` to stdout by its size and flushes, so each line shows at once even when stdout is a pipe.
void printText(std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

// Loads the texture dictionaries of the WAD entry `request` names for the viewer, converts them for drawing when the
// renderer draws, and prints how many there are: counts only, never the data.
std::expected<std::vector<coney::platform::TextureDictionary>, coney::Error>
loadForViewer(const coney::io::Wad& wad, const coney::chunk::ChunkHandlerTable& table, const std::string& request,
              const coney::platform::RenderEngine& engine) {
    auto entry = wad.lookup(request);
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    const coney::io::WadEntry& found = **entry;
    auto dictionaries = coney::platform::loadTextureDictionaries(wad, found, table);
    if (!dictionaries) {
        return std::unexpected(std::move(dictionaries.error()));
    }
    std::string text = std::format("{}: entry {}, hash {:#010x}, {} texture dictionar{}\n", request, found.index,
                                   found.nameHash, dictionaries->size(), dictionaries->size() == 1 ? "y" : "ies");
    for (std::size_t i = 0; i < dictionaries->size(); ++i) {
        coney::platform::TextureDictionary& dictionary = (*dictionaries)[i];
        text += std::format("  dictionary {}: {} textures\n", i, dictionary.info().textures.size());
        for (const coney::graphics::Ps2TextureInfo& texture : dictionary.info().textures) {
            text += std::format("    {}x{}, {} bits, raster format {:#06x}, layout version {}\n", texture.width,
                                texture.height, texture.depth, texture.rasterFormat, texture.version);
        }
        if (engine.drawsPixels()) {
            if (auto converted = dictionary.convertForDrawing(); !converted) {
                return std::unexpected(std::move(converted.error()));
            }
        }
    }
    printText(text);
    return dictionaries;
}

} // namespace

// Coney throws no exceptions; what could escape is a failed allocation inside the standard library or librw, which
// ends the program either way.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    // Parse the command line; exit 2 on a usage error, as command-line tools do.
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
    coney::chunk::ChunkHandlerTable chunkHandlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(chunkHandlers);

    // Game time runs on the fixed 1/30 s step and never reads a real clock, which keeps the engine's test mode
    // (docs/guides/conventions.md#platform-code) possible.
    coney::GameTimer timer;
    timer.setFixedStep(true);
    coney::GameModeStack modes;
    std::optional<std::uint64_t> frameLimit;
    if (const std::optional<int> limit = options->frameLimit; limit) {
        frameLimit = static_cast<std::uint64_t>(*limit);
    }

    // The renderer: OpenGL in a window, or the headless NULL renderer for --headless and for --load, a console job.
    // librw runs either way, because the texture dictionary handlers read through it.
    const bool headless = options->headless || !options->loads.empty();
    auto engine = coney::platform::RenderEngine::start(
        headless ? coney::platform::RenderBackend::Null : coney::platform::RenderBackend::OpenGl, {});
    if (!engine) {
        std::fprintf(stderr, "coney: %s\n", engine.error().message.c_str());
        return 1;
    }
    coney::platform::RenderEngine& renderer = **engine;

    if (!options->loads.empty()) {
        if (!wad) {
            return 2; // parseOptions refuses --load without --disc, so this is never reached
        }
        coney::LoadEntryMode loader(*wad, chunkHandlers, options->loads, printText);
        modes.push(loader);
        modes.runUntilEmpty(timer, {}, frameLimit);
        return loader.failures() == 0 ? 0 : 1;
    }

    // The mode at the bottom of the stack: the texture viewer for --view-txd, or an idle screen while Coney has no
    // game to run. Declared after the renderer, so they are destroyed before it: the viewer's textures are librw's.
    coney::IdleMode idle(&renderer);
    std::optional<coney::platform::TextureViewerMode> viewer;
    if (const std::optional<std::string> viewTxd = options->viewTxd; viewTxd) {
        if (!wad) {
            return 2; // parseOptions refuses --view-txd without --disc, so this is never reached
        }
        auto dictionaries = loadForViewer(*wad, chunkHandlers, *viewTxd, renderer);
        if (!dictionaries) {
            std::fprintf(stderr, "coney: %s: %s\n", viewTxd->c_str(), dictionaries.error().message.c_str());
            return 1;
        }
        modes.push(viewer.emplace(renderer, std::move(*dictionaries)));
    } else {
        modes.push(idle);
    }

    // A screenshot is of the last frame, so it needs the frame limit (parseOptions makes sure of it).
    const std::optional<std::string> screenshotPath = options->screenshotPath;
    if (screenshotPath && frameLimit) {
        renderer.requestCapture(*frameLimit - 1, *screenshotPath);
    }

    std::optional<coney::platform::Window> window = renderer.window();
    modes.runUntilEmpty(timer, [&window] { return !window || window->pumpEvents(); }, frameLimit);

    // Report the screenshot: where it went and a summary that says whether anything was drawn.
    if (const auto& capture = renderer.capture(); capture) {
        if (!capture->has_value()) {
            std::fprintf(stderr, "coney: %s\n", capture->error().message.c_str());
            return 1;
        }
        const coney::platform::CapturedFrame& frame = capture->value();
        printText(std::format("screenshot {} ({}x{}): {} of {} pixels differ from the background, hash {:#018x}\n",
                              frame.path, frame.size.width, frame.size.height, frame.stats.notBackground,
                              frame.stats.pixels, frame.stats.hash));
    }
    return 0;
}
