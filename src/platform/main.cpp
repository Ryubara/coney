// SPDX-License-Identifier: GPL-3.0-or-later

// Entry point. SDL_main.h lets SDL provide the right entry on each OS (WinMain on Windows), which is why main lives
// in src/platform/: it is the one function that is part of the operating-system boundary.

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL_main.h>

#include "core/chunk_system.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/language.h"
#include "core/options.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/idle_mode.h"
#include "gamemodes/load_entry_mode.h"
#include "gamemodes/sheet_viewer_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gamemodes/text_viewer_mode.h"
#include "graphics/font.h"
#include "gui/global_strings.h"
#include "gui/text_layout.h"
#include "platform/character_viewer_mode.h"
#include "platform/play_level_mode.h"
#include "platform/reference_renderer.h"
#include "platform/render_engine.h"
#include "platform/sdl_input.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "platform/texture_viewer_mode.h"
#include "platform/window.h"
#include "platform/world_viewer_mode.h"
#include "scripting/config_strings.h"
#include "world/sector_budget.h"

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

// Loads the sprite sheet `request` names for the sheet viewer: a resource name first (`menu_system`, found by the
// decimal CRC-32 of the name), then a WAD entry name or hash. Prints its shape: counts only, never the data.
std::expected<coney::graphics::SpriteSheet, coney::Error>
loadSheetForViewer(const coney::io::Wad& wad, const coney::chunk::ChunkHandlerTable& table, const std::string& request,
                   const coney::platform::RenderEngine& engine) {
    auto sheet = coney::platform::loadSpriteSheetResource(wad, table, request, engine.drawsPixels());
    if (!sheet && sheet.error().code == coney::ErrorCode::NotFound) {
        auto entry = wad.lookup(request);
        if (!entry) {
            return std::unexpected(std::move(sheet.error()));
        }
        sheet = coney::platform::loadSpriteSheet(wad, **entry, table, engine.drawsPixels());
    }
    if (!sheet) {
        return sheet;
    }
    const coney::graphics::Texture* texture = sheet->texture.get();
    printText(std::format("{}: {} rectangles, first glyph {}, texture {}x{}\n", request, sheet->page.rects.size(),
                          sheet->page.firstGlyph, texture != nullptr ? texture->width() : 0,
                          texture != nullptr ? texture->height() : 0));
    return sheet;
}

// The text `--view-text` shows: the argument itself, or for `@ID` (decimal or 0x hex) that UI string of the language,
// loaded by running the game's string scripts. Prints counts only, never the text.
std::expected<std::string, coney::Error> resolveViewText(const coney::io::Wad& wad, const std::string& argument,
                                                         coney::Language language) {
    if (!argument.starts_with('@')) {
        return argument;
    }
    std::string_view digits = std::string_view(argument).substr(1);
    int base = 10;
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        digits.remove_prefix(2);
        base = 16;
    }
    std::uint32_t id = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), id, base);
    if (digits.empty() || parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
        return coney::fail(coney::ErrorCode::InvalidArgument,
                           std::format("\"{}\" is not a UI string id (@ and a number)", argument));
    }
    coney::gui::GlobalStrings strings;
    auto loaded = coney::script::loadGlobalStrings(coney::script::wadScriptSource(wad), language, strings);
    if (!loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    const std::string_view text = strings.get(id);
    printText(std::format("UI strings ({}): {} HUD strings; string {:#x} has {} bytes\n", coney::languageCode(language),
                          strings.size(coney::gui::StringTable::Hud), id, text.size()));
    if (text.empty()) {
        return coney::fail(coney::ErrorCode::NotFound, std::format("no UI string has the id {:#x}", id));
    }
    return std::string(text);
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
    coney::platform::addSpriteSheetHandlers(chunkHandlers);

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
    // --render-references draws offscreen with OpenGL, so its window stays hidden.
    const bool headless = options->headless || !options->loads.empty();
    coney::platform::WindowDesc windowDesc;
    windowDesc.hidden = options->renderReferences.has_value();
    auto engine = coney::platform::RenderEngine::start(
        headless ? coney::platform::RenderBackend::Null : coney::platform::RenderBackend::OpenGl, windowDesc);
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

    // --render-references: one image per character, then exit (docs/guides/building.md#character-reference-images).
    if (const std::optional<std::string> outDir = options->renderReferences; outDir) {
        if (!wad) {
            return 2; // parseOptions refuses --render-references without --disc, so this is never reached
        }
        coney::platform::ReferenceRenderSettings settings;
        settings.outDir = *outDir;
        settings.only = options->only;
        settings.namesFile = options->namesFile.value_or(std::string{});
        auto report = coney::platform::renderCharacterReferences(renderer, *wad, settings, printText);
        if (!report) {
            std::fprintf(stderr, "coney: %s\n", report.error().message.c_str());
            return 1;
        }
        std::printf("reference images: %zu rendered, %zu failed\n", report->rendered, report->failed);
        return report->failed == 0 ? 0 : 1;
    }

    // The modes. Declared after the renderer, so they are destroyed before it: their textures are librw's.
    coney::IdleMode idle(&renderer);
    const auto loadSheet = [&wad, &chunkHandlers, &renderer](
                               std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
        if (!wad) {
            return coney::fail(coney::ErrorCode::NotFound, "no disc to load from");
        }
        return coney::platform::loadSpriteSheetResource(*wad, chunkHandlers, name, renderer.drawsPixels());
    };
    coney::gui::GlobalStrings strings;
    std::optional<coney::StartUpFlow> startUp;
    std::optional<coney::platform::TextureViewerMode> viewer;
    std::optional<coney::SheetViewerMode> sheetViewer;
    std::optional<coney::TextViewerMode> textViewer;
    // The world viewer's memory budget: the original's Sector Pool, at its retail size.
    coney::world::SectorBudget sectorBudget(coney::world::kSectorPoolSize);
    std::unique_ptr<coney::platform::WorldViewerMode> worldViewer;
    std::unique_ptr<coney::platform::CharacterViewerMode> characterViewer;
    std::unique_ptr<coney::platform::PlayLevelMode> playLevel;
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
    } else if (const std::optional<std::string> viewSheet = options->viewSheet; viewSheet) {
        if (!wad) {
            return 2; // parseOptions refuses --view-sheet without --disc, so this is never reached
        }
        auto sheet = loadSheetForViewer(*wad, chunkHandlers, *viewSheet, renderer);
        if (!sheet) {
            std::fprintf(stderr, "coney: %s: %s\n", viewSheet->c_str(), sheet.error().message.c_str());
            return 1;
        }
        modes.push(sheetViewer.emplace(renderer, *sheet));
    } else if (const std::optional<coney::TextView> viewText = options->viewText; viewText) {
        if (!wad) {
            return 2; // parseOptions refuses --view-text without --disc, so this is never reached
        }
        auto sheet = loadSheetForViewer(*wad, chunkHandlers, viewText->font, renderer);
        auto font = sheet ? coney::graphics::Font::fromSheet(std::move(*sheet))
                          : std::expected<coney::graphics::Font, coney::Error>(std::unexpected(sheet.error()));
        auto text = font ? resolveViewText(*wad, viewText->text, options->language)
                         : std::expected<std::string, coney::Error>(std::unexpected(font.error()));
        if (!text) {
            std::fprintf(stderr, "coney: %s: %s\n", viewText->font.c_str(), text.error().message.c_str());
            return 1;
        }
        // <BIGFONT> needs big_font beside the chosen font.
        std::optional<coney::graphics::Font> bigFont;
        if (viewText->font != coney::gui::kBigFontSheet) {
            auto bigSheet = coney::platform::loadSpriteSheetResource(*wad, chunkHandlers, coney::gui::kBigFontSheet,
                                                                     renderer.drawsPixels());
            if (bigSheet) {
                if (auto big = coney::graphics::Font::fromSheet(std::move(*bigSheet))) {
                    bigFont = std::move(*big);
                }
            }
        }
        modes.push(textViewer.emplace(renderer, std::move(*font), std::move(bigFont), std::move(*text)));
    } else if (const std::optional<std::string> viewWorld = options->viewWorld; viewWorld) {
        if (!wad) {
            return 2; // parseOptions refuses --view-world without --disc, so this is never reached
        }
        auto viewerMode = coney::platform::WorldViewerMode::create(renderer, *wad, *viewWorld, sectorBudget, printText);
        if (!viewerMode) {
            std::fprintf(stderr, "coney: %s: %s\n", viewWorld->c_str(), viewerMode.error().message.c_str());
            return 1;
        }
        worldViewer = std::move(*viewerMode);
        modes.push(*worldViewer);
    } else if (const std::optional<std::string> viewCharacter = options->viewCharacter; viewCharacter) {
        if (!wad) {
            return 2; // parseOptions refuses --view-character without --disc, so this is never reached
        }
        auto viewerMode = coney::platform::CharacterViewerMode::create(
            renderer, *wad, *viewCharacter, options->animClip.value_or(std::string{}), printText);
        if (!viewerMode) {
            std::fprintf(stderr, "coney: %s: %s\n", viewCharacter->c_str(), viewerMode.error().message.c_str());
            return 1;
        }
        characterViewer = std::move(*viewerMode);
        modes.push(*characterViewer);
    } else if (const std::optional<std::string> playName = options->playLevel; playName) {
        if (!wad) {
            return 2; // parseOptions refuses --play-level without --disc, so this is never reached
        }
        auto playMode = coney::platform::PlayLevelMode::create(renderer, *wad, *playName, sectorBudget, printText);
        if (!playMode) {
            std::fprintf(stderr, "coney: %s: %s\n", playName->c_str(), playMode.error().message.c_str());
            return 1;
        }
        playLevel = std::move(*playMode);
        modes.push(*playLevel);
    } else if (wad) {
        // The start-up flow, as the original's main pushes it (docs/research/boot.md#main): the level flow (mode 8) at
        // the bottom, then the memory-card check (mode 6), then the legal screen (mode 5), which runs first; the level
        // flow later shows the menus (mode 0x12). The game's scripts run in the flow's script system: the legal
        // screen's preloads fill the UI strings (docs/research/scripting.md#life-of-the-lua-state).
        coney::LegalScreenSettings legal;
        legal.language = options->language;
        startUp.emplace(renderer, modes, loadSheet, strings, legal, printText, coney::script::wadScriptSource(*wad))
            .start();
    } else {
        // No disc: no game to run, only the idle screen.
        modes.push(idle);
    }

    // Pad input (docs/research/frontend.md#input): a script in test mode, otherwise SDL's gamepads and keyboard when
    // there is a window; a headless run without a script has no pads. Declared after the renderer, so it is destroyed
    // before SDL stops.
    std::unique_ptr<coney::InputSource> input;
    if (const std::optional<std::string> scriptPath = options->inputScript; scriptPath) {
        auto events = coney::loadInputScript(*scriptPath);
        if (!events) {
            std::fprintf(stderr, "coney: %s\n", events.error().message.c_str());
            return 1;
        }
        input = std::make_unique<coney::ScriptedInput>(std::move(*events));
    } else if (renderer.window()) {
        auto devices = coney::platform::SdlInput::start();
        if (devices) {
            input = std::move(*devices);
        } else {
            std::fprintf(stderr, "coney: %s; running without pads\n", devices.error().message.c_str());
        }
    }
    modes.setInput(input.get());

    // A screenshot is of the last frame, so it needs the frame limit (parseOptions makes sure of it).
    const std::optional<std::string> screenshotPath = options->screenshotPath;
    if (screenshotPath && frameLimit) {
        renderer.requestCapture(*frameLimit - 1, *screenshotPath);
    }

    std::optional<coney::platform::Window> window = renderer.window();
    modes.runUntilEmpty(timer, [&window] { return !window || window->pumpEvents(); }, frameLimit);
    if (worldViewer) {
        printText(worldViewer->summary());
    }
    if (characterViewer) {
        printText(characterViewer->summary());
    }
    if (playLevel) {
        printText(playLevel->summary());
    }

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
