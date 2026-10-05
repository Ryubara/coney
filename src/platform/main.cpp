// SPDX-License-Identifier: GPL-3.0-or-later

// Entry point. SDL_main.h lets SDL provide the right entry on each OS (WinMain on Windows), which is why main lives
// in src/platform/: it is the one function that is part of the operating-system boundary.

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#include "core/chunk_system.h"
#include "core/error.h"
#include "core/frame_clock.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/language.h"
#include "core/options.h"
#include "debug/combat_tunables.h"
#include "debug/debug_session.h"
#include "debug/game_tunables.h"
#include "debug/tunables.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/idle_mode.h"
#include "gamemodes/level_start.h"
#include "gamemodes/load_entry_mode.h"
#include "gamemodes/sheet_viewer_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gamemodes/text_viewer_mode.h"
#include "graphics/font.h"
#include "gui/global_strings.h"
#include "gui/text_layout.h"
#include "human/player.h"
#include "platform/character_viewer_mode.h"
#include "platform/debug_menus.h"
#include "platform/frame_pacer.h"
#include "platform/imgui_overlay.h"
#include "platform/play_level_mode.h"
#include "platform/reference_renderer.h"
#include "platform/render_engine.h"
#include "platform/sandbox_viewer_mode.h"
#include "platform/sdl_input.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "platform/texture_viewer_mode.h"
#include "platform/window.h"
#include "platform/world_set.h"
#include "platform/world_viewer_mode.h"
#include "sandbox/sandbox_world.h"
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

// The folder of sandbox layouts: `sandbox` in the folder --assets names, or in the `assets` folder the build copies
// beside the executable.
std::filesystem::path sandboxFolder(const coney::Options& options) {
    if (const std::optional<std::string> assets = options.assetsDir; assets) {
        return std::filesystem::path(*assets) / "sandbox";
    }
    const char* base = SDL_GetBasePath(); // owned by SDL; null when the platform cannot tell
    const std::filesystem::path executableFolder =
        base != nullptr ? std::filesystem::path(base) : std::filesystem::path(".");
    return executableFolder / "assets" / "sandbox";
}

// Loads sandbox layout `name` from the sandbox folder and prints its size: counts only.
std::expected<coney::sandbox::SandboxWorld, coney::Error> loadSandbox(const coney::Options& options,
                                                                      std::string_view name) {
    auto world = coney::sandbox::SandboxWorld::load(sandboxFolder(options), name);
    if (!world) {
        return world;
    }
    const coney::sandbox::BakeStats& bake = world->bakeStats();
    printText(std::format("sandbox {}: {} primitives, {} spawn points, {} viewpoints; {} vertices, {} triangles; {} "
                          "collision triangles; light baked with {} occlusion and {} shadow rays\n",
                          name, world->layout().primitives.size(), world->layout().spawns.size(),
                          world->layout().views.size(), world->mesh().vertices.size(), world->mesh().triangles.size(),
                          world->collision() != nullptr ? world->collision()->triangles().size() : 0,
                          bake.occlusionRays, bake.shadowRays));
    return world;
}

// Player 1's start as the level script created him, in the play mode's terms; nothing when the script made none.
std::optional<coney::human::PlayerStart> playerStartOf(const coney::LevelStart& start) {
    if (!start.player) {
        return std::nullopt;
    }
    const coney::HumanCreation& player = *start.player;
    if (!player.position) {
        return std::nullopt;
    }
    const std::array<float, 3>& p = *player.position;
    return coney::human::PlayerStart{.position = coney::anim::Vec3{p[0], p[1], p[2]},
                                     .headingDegrees = player.headingDegrees};
}

// Runs level `name`'s scripts alone, as the story would reach it at `checkpoint`
// (docs/guides/building.md#playing-a-level), and prints what they made: the start and counts only. Nothing when the
// script made no player 1 to place.
std::optional<coney::human::PlayerStart> scriptStartFor(const coney::io::Wad& wad, std::string_view name,
                                                        int checkpoint) {
    const coney::LevelScriptRun run =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(wad), name, checkpoint, {});
    const std::optional<coney::human::PlayerStart> start = playerStartOf(run.start);
    if (start && run.start.player) {
        printText(std::format("level script: {} checkpoint {}: player 1 {} (type {}) at ({:.2f}, {:.2f}, {:.2f}) "
                              "heading {:.0f}; {} humans, {} script errors, {} skipped calls\n",
                              name, checkpoint, run.start.player->name, run.start.player->type, start->position.x,
                              start->position.y, start->position.z, start->headingDegrees, run.humans, run.scriptErrors,
                              run.skippedCalls));
    } else {
        printText(std::format("level script: {} checkpoint {}: no player 1 with a position; {} humans, {} script "
                              "errors, {} skipped calls\n",
                              name, checkpoint, run.humans, run.scriptErrors, run.skippedCalls));
    }
    return start;
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
    // (docs/guides/conventions.md#platform-code) possible; only the frame pacer below does, outside test mode.
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
    renderer.setVsync(options->vsync);

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
    std::unique_ptr<coney::platform::SandboxViewerMode> sandboxViewer;
    // The debug lines a story level's play mode draws: the debug session's, once it exists (below).
    const coney::debug::DebugDrawOptions* storyDebugDraw = nullptr;
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
        // A level, or a sandbox layout (`sandbox:NAME`) with the same player and camera on its collision mesh.
        std::expected<std::unique_ptr<coney::platform::PlayLevelMode>, coney::Error> playMode =
            coney::fail(coney::ErrorCode::NotFound, "no level");
        if (const std::optional<std::string> layout = coney::sandboxOfPlayLevel(*playName); layout) {
            auto world = loadSandbox(*options, *layout);
            playMode = world ? coney::platform::PlayLevelMode::createInSandbox(renderer, *wad, std::move(*world),
                                                                               options->spawn, printText)
                             : std::unexpected(std::move(world.error()));
        } else {
            // The level's script says where player 1 starts at the checkpoint, as when the story reaches it.
            const std::optional<coney::human::PlayerStart> start =
                scriptStartFor(*wad, *playName, options->checkpoint.value_or(1));
            playMode =
                coney::platform::PlayLevelMode::create(renderer, *wad, *playName, sectorBudget, printText, start);
        }
        if (!playMode) {
            std::fprintf(stderr, "coney: %s: %s\n", playName->c_str(), playMode.error().message.c_str());
            return 1;
        }
        playLevel = std::move(*playMode);
        modes.push(*playLevel);
    } else if (const std::optional<std::string> sandboxName = options->sandbox; sandboxName) {
        // The sandbox with the free camera: no disc needed.
        auto world = loadSandbox(*options, *sandboxName);
        auto viewerMode = world ? coney::platform::SandboxViewerMode::create(renderer, std::move(*world), printText)
                                : std::unexpected(std::move(world.error()));
        if (!viewerMode) {
            std::fprintf(stderr, "coney: %s: %s\n", sandboxName->c_str(), viewerMode.error().message.c_str());
            return 1;
        }
        sandboxViewer = std::move(*viewerMode);
        modes.push(*sandboxViewer);
    } else if (wad) {
        // The start-up flow, as the original's main pushes it (docs/research/boot.md#main): the level flow (mode 8) at
        // the bottom, then the memory-card check (mode 6), then the legal screen (mode 5), which runs first; the level
        // flow later shows the menus (mode 0x12). The game's scripts run in the flow's script system: the legal
        // screen's preloads fill the UI strings (docs/research/scripting.md#life-of-the-lua-state).
        coney::LegalScreenSettings legal;
        legal.language = options->language;
        // Gameplay (mode 1) loads the chosen level as the play mode, with player 1 where the level script made him.
        const coney::io::Wad& gameWad = *wad;
        coney::GameplayMode::LevelLoader loadLevel =
            [&renderer, &gameWad, &sectorBudget, &storyDebugDraw](
                const coney::LevelStart& start) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
            auto mode = coney::platform::PlayLevelMode::create(renderer, gameWad, start.level, sectorBudget, printText,
                                                               playerStartOf(start));
            if (!mode) {
                return std::unexpected(std::move(mode.error()));
            }
            (*mode)->setDebugDraw(storyDebugDraw);
            return std::unique_ptr<coney::GameMode>(std::move(*mode));
        };
        startUp
            .emplace(renderer, modes, loadSheet, strings, legal, printText, coney::script::wadScriptSource(*wad),
                     std::move(loadLevel))
            .start();
    } else {
        // No disc: no game to run, only the idle screen.
        modes.push(idle);
    }

    // Pad input (docs/research/frontend.md#input): a script in test mode, otherwise SDL's gamepads and keyboard when
    // there is a window; a headless run without a script has no pads. Declared after the renderer, so it is destroyed
    // before SDL stops.
    std::unique_ptr<coney::InputSource> input;
    // The SDL input, when it is the source: the developer overlay mutes its keyboard.
    coney::platform::SdlInput* devices = nullptr;
    if (const std::optional<std::string> scriptPath = options->inputScript; scriptPath) {
        auto events = coney::loadInputScript(*scriptPath);
        if (!events) {
            std::fprintf(stderr, "coney: %s\n", events.error().message.c_str());
            return 1;
        }
        input = std::make_unique<coney::ScriptedInput>(std::move(*events));
    } else if (renderer.window()) {
        auto started = coney::platform::SdlInput::start();
        if (started) {
            devices = started->get();
            input = std::move(*started);
        } else {
            std::fprintf(stderr, "coney: %s; running without pads\n", started.error().message.c_str());
        }
    }

    // The debug menus (docs/guides/debug-menu.md), Coney's own tools: a session over the game's services whose input
    // gate sits between the pads and the game, drawn over every frame by the pad menu overlay.
    coney::debug::DebugServices debugServices;
    // The real time of each frame, for the Time page: the frame pacer measures it, so only outside test mode, which
    // keeps no real clock.
    const bool windowed = renderer.window().has_value();
    double frameMilliseconds = 0.0;
    if (!coney::isTestMode(*options)) {
        debugServices.frameMilliseconds = [&frameMilliseconds] { return frameMilliseconds; };
    }
    if (startUp) {
        debugServices.scripts = [&startUp] { return &startUp->scripts(); };
        debugServices.recorded = [&startUp] { return &startUp->recorded(); };
        debugServices.gameState = [&startUp] { return &startUp->state(); };
        // The level flow starts the chosen level next (MenuLoadLevel); a level in play waits for player-movement.
        debugServices.loadLevel = [&startUp](std::string_view name) {
            startUp->menuLoadLevel(name);
            return true;
        };
    }
    // Without the level flow, a level the Levels page asks for replaces the play mode at the start of the next frame
    // (playLevelNamed below); the page lists every level with a world on the disc.
    std::optional<std::string> pendingLevel;
    if (!startUp && wad) {
        debugServices.loadLevel = [&pendingLevel](std::string_view name) {
            pendingLevel = std::string(name);
            return true;
        };
        debugServices.playableLevels = [&wad] { return coney::platform::playableLevelNames(*wad); };
    }
    // The play mode, for the Player, Camera and Spawner pages; and the sandbox layouts the Levels page plays, switched
    // to at the start of the next frame (playSandbox below), outside any step.
    debugServices.play = [&playLevel, &startUp]() -> coney::debug::PlayControls* {
        if (playLevel) {
            return playLevel.get();
        }
        // A story level in play: gameplay's level is the play mode.
        return startUp ? dynamic_cast<coney::platform::PlayLevelMode*>(startUp->gameplay().level()) : nullptr;
    };
    debugServices.sandboxFolder = sandboxFolder(*options);
    std::optional<std::string> pendingSandbox;
    debugServices.loadSandbox = [&pendingSandbox](std::string_view name) {
        pendingSandbox = std::string(name);
        return true;
    };
    // The overrides file: --tunables, or the default in the user's config folder when there is a window (a headless
    // run touches no user folder).
    debugServices.tunablesFile =
        options->tunablesFile.value_or(renderer.window() ? coney::platform::defaultTunablesPath() : std::string{});
    coney::debug::TunableRegistry& tunables = coney::debug::globalTunables();
    coney::debug::registerGameTunables(tunables);
    coney::debug::registerCombatTunables(tunables);
    if (!debugServices.tunablesFile.empty()) {
        auto loaded = tunables.load(debugServices.tunablesFile);
        if (loaded) {
            printText(std::format("tunables: {} overrides from {}\n", *loaded, debugServices.tunablesFile));
        } else if (loaded.error().code != coney::ErrorCode::NotFound) {
            std::fprintf(stderr, "coney: %s\n", loaded.error().message.c_str());
            return 1;
        }
    }
    coney::debug::DebugSession debugSession(tunables, debugServices, input.get(), printText);
    modes.setInput(&debugSession.gate());
    std::optional<coney::graphics::Font> debugFont;
    if (wad) {
        if (auto sheet = loadSheet(coney::gui::kTextFontSheet); sheet) {
            if (auto font = coney::graphics::Font::fromSheet(std::move(*sheet)); font) {
                debugFont = std::move(*font);
            }
        }
    }
    coney::platform::PadMenuOverlay padMenu(debugSession, std::move(debugFont));
    if (playLevel) {
        playLevel->setDebugDraw(&debugSession.debugDraw());
    }
    storyDebugDraw = &debugSession.debugDraw();
    // Plays sandbox layout `name` in place of the play mode or sandbox viewer on top (or above whatever runs): with
    // the player when there is a disc for his character, else with the free camera.
    const auto playSandbox = [&](const std::string& name) {
        auto world = loadSandbox(*options, name);
        if (!world) {
            debugSession.print("levels: " + world.error().message);
            return;
        }
        // The mode it replaces must be on top, so no mode above it still runs on it.
        coney::GameMode* replaced = playLevel ? static_cast<coney::GameMode*>(playLevel.get())
                                              : static_cast<coney::GameMode*>(sandboxViewer.get());
        if (replaced != nullptr && modes.top() != replaced) {
            debugSession.print("levels: finish the mode on top first");
            return;
        }
        if (wad) {
            auto mode = coney::platform::PlayLevelMode::createInSandbox(renderer, *wad, std::move(*world), std::nullopt,
                                                                        printText);
            if (!mode) {
                debugSession.print("levels: " + mode.error().message);
                return;
            }
            if (replaced != nullptr) {
                modes.pop();
            }
            sandboxViewer.reset();
            playLevel = std::move(*mode);
            playLevel->setDebugDraw(&debugSession.debugDraw());
            modes.push(*playLevel);
            return;
        }
        auto viewerMode = coney::platform::SandboxViewerMode::create(renderer, std::move(*world), printText);
        if (!viewerMode) {
            debugSession.print("levels: " + viewerMode.error().message);
            return;
        }
        if (replaced != nullptr) {
            modes.pop();
        }
        playLevel.reset();
        sandboxViewer = std::move(*viewerMode);
        modes.push(*sandboxViewer);
    };
    // Plays level `name` (`level2`) in place of the play mode or sandbox viewer on top. The mode it replaces goes
    // first, so its sectors leave the budget before the level loads; the name is checked before that, so a typo keeps
    // it.
    const auto playLevelNamed = [&](const std::string& name) {
        if (auto worlds = coney::platform::worldNamesFor(*wad, name); !worlds) {
            debugSession.print("levels: " + worlds.error().message);
            return;
        }
        coney::GameMode* replaced = playLevel ? static_cast<coney::GameMode*>(playLevel.get())
                                              : static_cast<coney::GameMode*>(sandboxViewer.get());
        if (replaced != nullptr && modes.top() != replaced) {
            debugSession.print("levels: finish the mode on top first");
            return;
        }
        if (replaced != nullptr) {
            modes.pop();
        }
        playLevel.reset();
        sandboxViewer.reset();
        auto mode = coney::platform::PlayLevelMode::create(renderer, *wad, name, sectorBudget, printText,
                                                           scriptStartFor(*wad, name, 1));
        if (!mode) {
            debugSession.print(std::format("levels: {}: {}", name, mode.error().message));
            return;
        }
        playLevel = std::move(*mode);
        playLevel->setDebugDraw(&debugSession.debugDraw());
        modes.push(*playLevel);
        debugSession.print(std::format("levels: playing {}", name));
    };
    // The developer overlay (F1), only with a window; without it the pad menu still works.
    std::unique_ptr<coney::platform::ImGuiOverlay> devOverlay;
    if (windowed) {
        auto started =
            coney::platform::ImGuiOverlay::start(renderer, debugSession, coney::platform::defaultOverlayLayoutPath());
        if (started) {
            devOverlay = std::move(*started);
            if (const std::optional<int> shownFor = options->devOverlayFrames; shownFor) {
                devOverlay->showForFrames(static_cast<std::uint64_t>(*shownFor));
            }
        } else {
            std::fprintf(stderr, "coney: %s; running without the developer overlay\n", started.error().message.c_str());
        }
    }
    // Both menus draw over every frame, the overlay last so it stays on top.
    renderer.setPresentOverlay([&padMenu, &devOverlay](coney::graphics::RenderDevice& device) {
        padMenu.draw(device);
        if (devOverlay) {
            devOverlay->draw();
        }
    });
    // The time controls hold steps (pause, slow motion); a held step still reads the pads for the menus, and the
    // frame still renders the game's last step.
    modes.setStepGate([&debugSession] { return debugSession.time().shouldStep(); });

    // A screenshot is of the last frame, so it needs the frame limit (parseOptions makes sure of it).
    const std::optional<std::string> screenshotPath = options->screenshotPath;
    if (screenshotPath && frameLimit) {
        renderer.requestCapture(*frameLimit - 1, *screenshotPath);
    }

    // The main loop (docs/guides/conventions.md#update-and-render). Test mode runs in lockstep with no clock: one step
    // and one render per frame. Otherwise the frame pacer measures real time and the frame clock turns it into fixed
    // steps, rendering blended between them; a cap of 30 is the original's rhythm, one step per frame, unblended.
    const bool testMode = coney::isTestMode(*options);
    const auto fpsCap = static_cast<std::uint32_t>(options->fpsCap.value_or(0));
    constexpr std::uint32_t kStepsPerSecond = 30;
    coney::FrameClock clock(testMode || fpsCap == kStepsPerSecond ? coney::FramePacing::Lockstep
                                                                  : coney::FramePacing::Interpolated);
    std::optional<coney::platform::Window> window = renderer.window();
    // The window's events go past the developer overlay first; while it has the keyboard, the keyboard pad is off.
    coney::FrameHooks hooks;
    hooks.beginFrame = [&window, &devOverlay, devices, &pendingSandbox, &playSandbox, &pendingLevel, &playLevelNamed] {
        // A sandbox or level the Levels page asked for, between two frames.
        if (pendingSandbox) {
            const std::string name = *pendingSandbox;
            pendingSandbox.reset();
            playSandbox(name);
        }
        if (pendingLevel) {
            const std::string name = *pendingLevel;
            pendingLevel.reset();
            playLevelNamed(name);
        }
        if (!window) {
            return true;
        }
        const bool running =
            devOverlay ? window->pumpEvents([&devOverlay](const void* event) { return devOverlay->handleEvent(event); })
                       : window->pumpEvents();
        if (devices != nullptr) {
            devices->setKeyboardEnabled(!(devOverlay && devOverlay->wantsKeyboard()));
        }
        return running;
    };
    std::optional<coney::platform::FramePacer> pacer;
    if (!testMode) {
        coney::platform::FramePacer& paced =
            pacer.emplace(fpsCap, options->showFps ? std::function<void(std::string_view)>(printText)
                                                   : std::function<void(std::string_view)>());
        // The pacer's measure of each frame is also the debug menus' frame time.
        hooks.waitForFrame = [&paced, &frameMilliseconds] {
            const std::uint64_t nanoseconds = paced.waitForFrame();
            frameMilliseconds = static_cast<double>(nanoseconds) / 1'000'000.0;
            return nanoseconds;
        };
        hooks.endFrame = [&paced](std::uint32_t steps) { paced.endFrame(steps); };
    }
    modes.runUntilEmpty(timer, clock, hooks, frameLimit);
    if (pacer && options->showFps) {
        printText(pacer->summary());
    }

    if (worldViewer) {
        printText(worldViewer->summary());
    }
    if (characterViewer) {
        printText(characterViewer->summary());
    }
    if (playLevel) {
        printText(playLevel->summary());
    }
    if (startUp) {
        if (const auto* storyLevel = dynamic_cast<const coney::platform::PlayLevelMode*>(startUp->gameplay().level());
            storyLevel != nullptr) {
            printText(storyLevel->summary());
        }
    }
    if (sandboxViewer) {
        printText(sandboxViewer->summary());
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
