// SPDX-License-Identifier: GPL-3.0-or-later

// Entry point. SDL_main.h lets SDL provide the right entry on each OS (WinMain on Windows), which is why main lives
// in src/platform/: it is the one function that is part of the operating-system boundary.

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_main.h>

#include "ai/ai_config.h"
#include "audio/object_sounds.h"
#include "characters/character_types.h"
#include "core/chunk_system.h"
#include "core/deferred_input.h"
#include "core/error.h"
#include "core/event_log.h"
#include "core/frame_clock.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/language.h"
#include "core/options.h"
#include "debug/combat_tunables.h"
#include "debug/debug_session.h"
#include "debug/game_tunables.h"
#include "debug/tunables.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/idle_mode.h"
#include "gamemodes/level_start.h"
#include "gamemodes/load_entry_mode.h"
#include "gamemodes/memory_card_mode.h"
#include "gamemodes/rumble_menu_mode.h"
#include "gamemodes/sheet_viewer_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gamemodes/text_viewer_mode.h"
#include "graphics/font.h"
#include "gui/global_strings.h"
#include "gui/text_layout.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "movies/movie_mode.h"
#include "platform/audio_output.h"
#include "platform/character_viewer_mode.h"
#include "platform/debug_menus.h"
#include "platform/error_dialogs.h"
#include "platform/frame_pacer.h"
#include "platform/game_session.h"
#include "platform/imgui_overlay.h"
#include "platform/pad_pipe.h"
#include "platform/play_level_mode.h"
#include "platform/profile_folder.h"
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
#include "scripting/lua_value.h"
#include "scripting/script_bindings.h"
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

// What a level run alone starts with: the game's random table from the disc's executable when it has the NTSC-U one
// (counted, never printed), and for a Rumble arena the Rumble menu's default set-up.
coney::LevelScriptOptions levelScriptOptions(const coney::io::Wad& wad, std::string_view name,
                                             std::vector<std::uint32_t>& table) {
    coney::LevelScriptOptions options;
    if (auto words = coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    options.rumbleArena = coney::rumbleArenaOf(name);
    return options;
}

// The setup of a sandbox's player: **Coney's choice**, level99's configuration (the combat training level whose
// fighters the sandbox's stand in for, docs/research/ai.md#level99) for the AI fighters and the character types, from
// its scripts run alone and quietly; the player is Rembrandt.
coney::platform::PlayerSetup sandboxSetup(const coney::io::Wad& wad) {
    std::vector<std::uint32_t> table;
    const coney::LevelScriptRun run = coney::runLevelScriptAlone(
        coney::script::wadScriptSource(wad), "level99", 1, [](std::string_view /*line*/) {},
        levelScriptOptions(wad, "level99", table));
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(run.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(run.recorded);
    printText(std::format("fighters: level99's configuration ({} calls read); {} character types\n", setup.ai.callsRead,
                          setup.types.all().size()));
    return setup;
}

} // namespace

// Coney throws no exceptions; what could escape is a failed allocation inside the standard library or librw, which
// ends the program either way.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    // A failure must end the run on stderr, not wait on a dialog: scripted and headless runs have nobody to click.
    coney::platform::reportErrorsToConsole();

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

    // `--event-log` and `--pad-pipe`: the events go to the file, and to the driver as `@ev` lines (a hint's line
    // breaks become spaces there, one event a line). Opened before anything runs, so the first event is in it; the
    // guard closes the log before the file goes. With `--play-level` the log starts with the level's fresh Lua state
    // (the session's `ready`, below) at step 0, so the loading screen's events stay out of it, as they are out of a
    // playthrough of the original started from a save state.
    std::shared_ptr<std::ofstream> eventFile;
    const auto eventsRecording = std::make_shared<bool>(!options->playLevel);
    if (const std::optional<std::string> eventPath = options->eventLogFile; eventPath) {
        eventFile = std::make_shared<std::ofstream>(*eventPath, std::ios::binary | std::ios::trunc);
        if (!*eventFile) {
            std::fprintf(stderr, "coney: --event-log: cannot write %s\n", eventPath->c_str());
            return 1;
        }
        *eventFile << coney::events::kCsvHeader;
    }
    if (eventFile || options->padPipe) {
        coney::events::setSink(
            [eventFile, piped = options->padPipe, eventsRecording](std::uint64_t step, std::string_view kind,
                                                                   std::string_view name, std::string_view detail) {
                if (!*eventsRecording) {
                    return;
                }
                std::string line = coney::events::csvLine(step, kind, name, detail);
                if (eventFile) {
                    *eventFile << line;
                }
                if (piped) {
                    line.pop_back();
                    std::ranges::replace(line, '\n', ' ');
                    std::ranges::replace(line, '\r', ' ');
                    std::cout << "@ev " << line << '\n';
                }
            });
    }
    struct EventLogGuard {
        EventLogGuard() = default;
        EventLogGuard(const EventLogGuard&) = delete;
        EventLogGuard& operator=(const EventLogGuard&) = delete;
        EventLogGuard(EventLogGuard&&) = delete;
        EventLogGuard& operator=(EventLogGuard&&) = delete;
        ~EventLogGuard() { coney::events::setSink({}); }
    } const eventLogGuard;

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
    // Runs started by agents and tools (test mode, --no-activate) must not take the focus from someone typing.
    windowDesc.activate = coney::activatesWindow(*options);
    // `--render-size`: frames of that size holding exactly the logical screen, as the original's frame buffer does.
    if (const std::optional<coney::RenderSize> size = options->renderSize; size) {
        windowDesc.width = size->width;
        windowDesc.height = size->height;
        windowDesc.logicalFrame = true;
    }
    auto engine = coney::platform::RenderEngine::start(
        headless ? coney::platform::RenderBackend::Null : coney::platform::RenderBackend::OpenGl, windowDesc);
    if (!engine) {
        std::fprintf(stderr, "coney: %s\n", engine.error().message.c_str());
        return 1;
    }
    coney::platform::RenderEngine& renderer = **engine;
    renderer.setVsync(options->vsync);
    // Reference renders are model sheets, not the game's picture: drawn as they are.
    renderer.setLineBlend(options->lineBlend && !options->renderReferences.has_value());

    if (!options->loads.empty()) {
        if (!wad) {
            return 2; // parseOptions refuses --load without --disc, so this is never reached
        }
        coney::LoadEntryMode loader(*wad, chunkHandlers, options->loads, printText);
        modes.push(loader);
        modes.runUntilEmpty(timer, {}, frameLimit);
        return loader.failures() == 0 ? 0 : 1;
    }

    // --render-references: one image per character, object, car, radar icon and traced particle effect, then exit
    // (docs/guides/building.md#reference-images).
    if (const std::optional<std::string> outDir = options->renderReferences; outDir) {
        if (!wad) {
            return 2; // parseOptions refuses --render-references without --disc, so this is never reached
        }
        const coney::ReferenceKind kind = options->referenceKind.value_or(coney::ReferenceKind::All);
        coney::platform::ReferenceRenderSettings settings;
        settings.outDir = *outDir;
        const auto renders = [kind](coney::ReferenceKind list) {
            return kind == coney::ReferenceKind::All || kind == list;
        };
        settings.characters = renders(coney::ReferenceKind::Characters);
        settings.objects = renders(coney::ReferenceKind::Objects);
        settings.cars = renders(coney::ReferenceKind::Cars);
        settings.radar = renders(coney::ReferenceKind::Radar);
        settings.particles = renders(coney::ReferenceKind::Particles);
        settings.only = options->only;
        settings.namesFile = options->namesFile.value_or(std::string{});
        auto report = coney::platform::renderReferences(renderer, *wad, settings, printText);
        if (!report) {
            std::fprintf(stderr, "coney: %s\n", report.error().message.c_str());
            return 1;
        }
        std::printf("reference images: %zu characters, %zu objects, %zu cars, %zu radar icons and %zu particle "
                    "effects written, %zu objects without a model Coney can load as one, %zu failed\n",
                    report->characters, report->objects, report->cars, report->radar, report->particles,
                    report->noModel, report->failed);
        return report->failed == 0 ? 0 : 1;
    }

    // The modes. Declared after the renderer, so they are destroyed before it: their textures are librw's.
    coney::IdleMode idle(&renderer);
    const auto loadSheet = [&wad, &chunkHandlers, &renderer](
                               std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
        if (!wad) {
            return coney::fail(coney::ErrorCode::NotFound, "no disc to load from");
        }
        auto sheet = coney::platform::loadSpriteSheetResource(*wad, chunkHandlers, name, renderer.drawsPixels());
        // A sheet whose resource name is not known is asked for by its WAD file name (the Rumble menu's background).
        if (!sheet && sheet.error().code == coney::ErrorCode::NotFound) {
            if (auto entry = wad->lookup(name); entry) {
                return coney::platform::loadSpriteSheet(*wad, **entry, chunkHandlers, renderer.drawsPixels());
            }
        }
        return sheet;
    };
    coney::gui::GlobalStrings strings;
    std::optional<coney::platform::TextureViewerMode> viewer;
    std::optional<coney::SheetViewerMode> sheetViewer;
    std::optional<coney::TextViewerMode> textViewer;
    // The world viewer's memory budget: the original's Sector Pool, at its retail size.
    coney::world::SectorBudget sectorBudget(coney::world::kSectorPoolSize);
    std::unique_ptr<coney::platform::WorldViewerMode> worldViewer;
    std::unique_ptr<coney::platform::CharacterViewerMode> characterViewer;
    std::unique_ptr<coney::platform::PlayLevelMode> playLevel;
    std::unique_ptr<coney::platform::SandboxViewerMode> sandboxViewer;
    // The glass panes' and doors' sounds, for every level: silent until the sound output starts (below).
    coney::audio::ObjectSounds objectSounds;
    // The game over the disc (platform/game_session.h): the story from boot, the level `--play-level` names and the
    // levels the debug menus jump to all run in it, set up one way
    // (docs/guides/testing.md#test-through-the-players-path). Made below when the run plays the game, or by the debug
    // menus' first level jump.
    std::optional<coney::platform::GameSession> session;
    coney::platform::GameSessionSettings sessionSettings;
    sessionSettings.language = options->language;
    sessionSettings.skipMovies = options->skipMovies;
    sessionSettings.rumble = options->rumble;
    // The saved profiles: the player's folder, or none in test mode (docs/research/save.md#coney).
    sessionSettings.profiles = coney::platform::profileFolder(*options);
    sessionSettings.cardCheckingMs = coney::MemoryCardMode::kCheckingMessageMs;
    const auto makeSession = [&]() -> coney::platform::GameSession& {
        return session.emplace(renderer, modes, *wad, chunkHandlers, sectorBudget, strings, objectSounds,
                               sessionSettings, printText);
    };
    // The level `--play-level` names: the session starts there directly once the sound exists, so the preloads
    // configure the sound.
    std::optional<std::string> commandLineLevel;
    // The play mode of the session's level; null when none is loaded.
    const auto sessionPlayMode = [&session]() -> coney::platform::PlayLevelMode* {
        return session ? session->play() : nullptr;
    };
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
        if (const std::optional<std::string> layout = coney::sandboxOfPlayLevel(*playName); layout) {
            auto world = loadSandbox(*options, *layout);
            auto playMode =
                world ? coney::platform::PlayLevelMode::createInSandbox(renderer, *wad, std::move(*world),
                                                                        options->spawn, printText, sandboxSetup(*wad))
                      : std::unexpected(std::move(world.error()));
            if (!playMode) {
                std::fprintf(stderr, "coney: %s: %s\n", playName->c_str(), playMode.error().message.c_str());
                return 1;
            }
            playLevel = std::move(*playMode);
            // `--start`: the player (and the camera) somewhere else from the first step, a trace scenario's start.
            if (const std::optional<coney::StartPlace> start = options->start; start) {
                playLevel->startAt(*start);
            }
            if (const std::optional<coney::CameraPin> pin = options->cameraPin; pin) {
                playLevel->pinCamera(*pin);
            }
            if (options->freezeWorld) {
                playLevel->freezeWorld();
            }
            if (const std::optional<std::string> tracePath = options->traceFile; tracePath) {
                if (auto traced = playLevel->traceTo(*tracePath); !traced) {
                    std::fprintf(stderr, "coney: %s\n", traced.error().message.c_str());
                    return 1;
                }
            }
            modes.push(*playLevel);
        } else {
            // The level as the story reaches it, started directly in the game session (GameSession::startAtLevel(),
            // once the sound exists) so that it is set up as the story sets it up. A level with no world fails here.
            if (auto worlds = coney::platform::worldNamesFor(*wad, *playName); !worlds) {
                std::fprintf(stderr, "coney: %s: %s\n", playName->c_str(), worlds.error().message.c_str());
                return 1;
            }
            commandLineLevel = *playName;
            makeSession();
        }
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
        // The game from boot (GameSession::startStory(), once the sound exists): the start-up flow as the original's
        // main pushes it (docs/research/boot.md#main).
        makeSession();
    } else {
        // No disc: no game to run, only the idle screen.
        modes.push(idle);
    }

    // Pad input (docs/research/frontend.md#input): a script in test mode, otherwise SDL's gamepads and keyboard when
    // there is a window; a headless run without a script has no pads. Declared after the renderer, so it is destroyed
    // before SDL stops. With `--play-level` a script starts at the level's first frame of play (DeferredInput), so its
    // frames keep their meaning however long the loading screen and an intro movie take.
    std::unique_ptr<coney::ScriptedInput> levelScript;   // the script under the deferred start; before `input`
    std::unique_ptr<coney::platform::PadPipe> levelPipe; // the pad pipe under the deferred start; before `input`
    std::unique_ptr<coney::InputSource> input;
    coney::DeferredInput* deferredInput = nullptr;
    // The SDL input, when it is the source: the developer overlay mutes its keyboard.
    coney::platform::SdlInput* devices = nullptr;
    if (const std::optional<std::string> scriptPath = options->inputScript; scriptPath) {
        auto events = coney::loadInputScript(*scriptPath);
        if (!events) {
            std::fprintf(stderr, "coney: %s\n", events.error().message.c_str());
            return 1;
        }
        auto scripted = std::make_unique<coney::ScriptedInput>(std::move(*events));
        // `steer` lines read player 1 and his camera from the level in play (a sandbox's or a story level's).
        scripted->setSteerSource([&playLevel, &sessionPlayMode](std::size_t port) -> std::optional<coney::SteerView> {
            const coney::platform::PlayLevelMode* mode = playLevel ? playLevel.get() : sessionPlayMode();
            if (port != 0 || mode == nullptr) {
                return std::nullopt;
            }
            const coney::anim::Vec3 feet = mode->playerFeet();
            const coney::anim::Vec3 eye = mode->cameraEye();
            const coney::anim::Vec3 target = mode->cameraTarget();
            constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
            return coney::SteerView{.x = feet.x,
                                    .y = feet.y,
                                    .cameraHeading = std::atan2(-(target.x - eye.x), target.y - eye.y) * kDegrees};
        });
        if (commandLineLevel) {
            levelScript = std::move(scripted);
            auto deferred = std::make_unique<coney::DeferredInput>(*levelScript,
                                                                   [&session] { return session && session->inPlay(); });
            deferredInput = deferred.get();
            input = std::move(deferred);
        } else {
            input = std::move(scripted);
        }
    } else if (options->padPipe) {
        // `--pad-pipe`: a driver program plays player 1 by what it sees of the level in play. With `--play-level` the
        // pipe starts at the level's first frame of play (DeferredInput), as a script does, so the driver's first view
        // is of the level and not of its loading screen.
        auto pipe =
            std::make_unique<coney::platform::PadPipe>(std::cin, std::cout, [&playLevel, &sessionPlayMode, &session]() {
                coney::platform::PipeView view;
                view.mode = playLevel ? playLevel.get() : sessionPlayMode();
                view.cars = view.mode != nullptr ? view.mode->cars() : nullptr;
                if (session) {
                    view.records = &session->flow().spawnRecords();
                    view.pickups = session->gameplay().pickups();
                }
                return view;
            });
        if (commandLineLevel) {
            levelPipe = std::move(pipe);
            auto deferred =
                std::make_unique<coney::DeferredInput>(*levelPipe, [&session] { return session && session->inPlay(); });
            deferredInput = deferred.get();
            input = std::move(deferred);
        } else {
            input = std::move(pipe);
        }
    } else if (renderer.window()) {
        auto started = coney::platform::SdlInput::start();
        if (started) {
            devices = started->get();
            devices->setLog(printText);
            input = std::move(*started);
        } else {
            std::fprintf(stderr, "coney: %s; running without pads\n", started.error().message.c_str());
        }
    }

    // The debug menus (docs/guides/debug-menu.md), Coney's own tools: a session over the game's services whose input
    // gate sits between the pads and the game, drawn over every frame by the pad menu overlay.
    coney::debug::DebugServices debugServices;
    // The main loop's pacing (docs/guides/conventions.md#update-and-render). Test mode runs in lockstep with no clock:
    // one step and one render per frame. Otherwise the frame pacer measures real time and the frame clock turns it
    // into fixed steps, rendering blended between them; a cap of 30 is the original's rhythm, one step per frame,
    // unblended.
    const bool testMode = coney::isTestMode(*options);
    constexpr std::uint32_t kStepsPerSecond = 30;
    const auto pacingFor = [](std::uint32_t cap) {
        return cap == kStepsPerSecond ? coney::FramePacing::Lockstep : coney::FramePacing::Interpolated;
    };
    const auto fpsCap = static_cast<std::uint32_t>(options->fpsCap.value_or(0));
    coney::FrameClock clock(testMode ? coney::FramePacing::Lockstep : pacingFor(fpsCap));
    std::optional<coney::platform::FramePacer> pacer;
    if (!testMode) {
        pacer.emplace(fpsCap, options->showFps ? std::function<void(std::string_view)>(printText)
                                               : std::function<void(std::string_view)>());
    }
    // The real time of each frame and the rates, for the Time and Display pages, and the cap and vsync, changed live
    // there: the frame pacer measures and paces, so only outside test mode, which keeps no real clock.
    const bool windowed = renderer.window().has_value();
    double frameMilliseconds = 0.0;
    if (pacer) {
        debugServices.frameMilliseconds = [&frameMilliseconds] { return frameMilliseconds; };
        debugServices.frameRate = [&pacer] { return pacer->meter().reading(); };
        debugServices.fpsCap = [&pacer] { return pacer->cap(); };
        debugServices.setFpsCap = [&pacer, &clock, pacingFor](std::uint32_t cap) {
            pacer->setCap(cap);
            clock.setPacing(pacingFor(cap));
        };
    }
    if (windowed) {
        debugServices.vsync = [&renderer] { return renderer.vsync(); };
        debugServices.setVsync = [&renderer](bool on) { renderer.setVsync(on); };
    }
    // The sound output (docs/research/sound.md#coneys-implementation): SDL's playback device, or in test mode the
    // offline one, which opens no device and mixes a fixed step of frames per frame. A device that cannot open leaves
    // the run silent, never stopped. Declared after the renderer, so it is destroyed before SDL stops.
    std::unique_ptr<coney::platform::AudioOutput> audio;
    if (!options->noAudio) {
        auto started = coney::platform::AudioOutput::start(testMode ? coney::platform::AudioSink::Offline
                                                                    : coney::platform::AudioSink::Device);
        if (started) {
            audio = std::move(*started);
            if (!testMode) {
                printText(audio->startLine());
            }
            if (options->audioTest) {
                audio->setTestTone(true);
            }
            // The game's sound (docs/research/sound.md): the tables, banks and streams from the disc.
            if (wad) {
                if (auto soundData = audio->startEngine(*wad); !soundData) {
                    std::fprintf(stderr, "coney: %s; the game's sounds stay silent\n",
                                 soundData.error().message.c_str());
                }
            }
            debugServices.audio = [&audio]() -> coney::debug::AudioControls* { return audio.get(); };
            audio->game().setLog(printText);
        } else {
            std::fprintf(stderr, "coney: %s; running without sound\n", started.error().message.c_str());
        }
    }
    // The game session gets the sound before its first frame, then starts: from boot, or directly at the command
    // line's level, where the run's own options (`--start`, `--camera`, `--freeze-world`, `--trace`, `--scene`,
    // `--script-trace`) apply to the first level it loads.
    if (session) {
        session->attachAudio(audio.get());
        if (commandLineLevel) {
            session->setFirstLevelSetup(
                [&options](coney::platform::PlayLevelMode& mode) -> std::expected<void, coney::Error> {
                    // `--start`: the player (and the camera) somewhere else from the first step, a trace scenario's
                    // start.
                    if (const std::optional<coney::StartPlace> place = options->start; place) {
                        mode.startAt(*place);
                    }
                    // `--camera` and `--freeze-world`: a stable, matched view for comparing with the original's frames.
                    if (const std::optional<coney::CameraPin> pin = options->cameraPin; pin) {
                        mode.pinCamera(*pin);
                    }
                    if (options->freezeWorld) {
                        mode.freezeWorld();
                    }
                    if (const std::optional<std::string> tracePath = options->traceFile; tracePath) {
                        if (auto traced = mode.traceTo(*tracePath); !traced) {
                            return std::unexpected(std::move(traced.error()));
                        }
                    }
                    // `--scene`: a scene plays at once, a test aid.
                    if (const std::optional<std::string> scene = options->scene; scene) {
                        if (auto played = mode.playScene(*scene); !played) {
                            return std::unexpected(std::move(played.error()));
                        }
                    }
                    return {};
                });
            session->gameplay().setWorldFrozen(options->freezeWorld);
            // `--script-trace`: every binding call and call into the scripts from the level's Lua state on, to a file
            // the trace keeps open.
            // `--event-log` and `--pad-pipe`: the events from here on, counted from step 0.
            std::shared_ptr<std::ofstream> scriptTraceFile;
            if (const std::optional<std::string> scriptTrace = options->scriptTraceFile; scriptTrace) {
                scriptTraceFile = std::make_shared<std::ofstream>(*scriptTrace, std::ios::binary | std::ios::trunc);
                if (!*scriptTraceFile) {
                    std::fprintf(stderr, "coney: --script-trace: cannot write %s\n", scriptTrace->c_str());
                    return 1;
                }
            }
            std::function<void()> ready = [&session, scriptTraceFile, eventsRecording] {
                if (scriptTraceFile) {
                    session->flow().scripts().traceCalls(
                        [file = scriptTraceFile](std::string_view line) { *file << line; });
                }
                coney::events::setStep(0);
                *eventsRecording = true;
            };
            session->startAtLevel(*commandLineLevel, options->checkpoint.value_or(1), std::move(ready));
        } else {
            session->startStory();
        }
    }
    // The debug menus' level jumps (the Levels and Missions pages), carried out at the start of the next frame, outside
    // any step: by the game session (the level flow's jump, from the front end or a level in play), or, before a
    // session runs (a viewer or a sandbox), by a new session started directly at that level.
    std::optional<std::pair<std::string, int>> pendingStart;
    const auto jumpTo = [&session, &pendingStart, &wad](std::string_view name, int checkpoint) {
        if (session) {
            return session->jumpToLevel(name, checkpoint);
        }
        if (!wad || !coney::platform::worldNamesFor(*wad, name)) {
            return false;
        }
        pendingStart.emplace(std::string(name), checkpoint);
        return true;
    };
    if (wad) {
        // The game's scripts and state for the Lua console and the Cheats page: the session's, however it started.
        coney::platform::connectDebugServices(
            debugServices, [&session]() -> coney::platform::GameSession* { return session ? &*session : nullptr; });
        debugServices.loadLevel = [jumpTo](std::string_view name) { return jumpTo(name, 1); };
        debugServices.loadLevelAt = [jumpTo](std::string_view name, int checkpoint) {
            return jumpTo(name, checkpoint);
        };
    }
    // Before a game session runs, the Levels page lists every level with a world on the disc.
    if (!session && wad) {
        debugServices.playableLevels = [&wad] { return coney::platform::playableLevelNames(*wad); };
    }
    // The play mode, for the Player, Camera and Spawner pages; and the sandbox layouts the Levels page plays, switched
    // to at the start of the next frame (playSandbox below), outside any step.
    debugServices.play = [&playLevel, &sessionPlayMode]() -> coney::debug::PlayControls* {
        if (playLevel) {
            return playLevel.get();
        }
        return sessionPlayMode();
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
    // A sandbox's play mode plays the HUD's sounds through the game-facing player, when there is sound.
    const auto sandboxHudSound = [&audio](std::string_view name) {
        if (audio) {
            audio->sounds().play(name);
        }
    };
    if (playLevel) {
        playLevel->setDebugDraw(&debugSession.debugDraw());
        playLevel->setSounds(audio ? &audio->sounds() : nullptr);
        playLevel->hud()->setSoundOutput(sandboxHudSound);
    }
    if (session) {
        session->setDebugDraw(&debugSession.debugDraw());
    }
    // The mode a sandbox or a new game session replaces: the sandbox's play mode or the sandbox viewer; null when
    // neither runs.
    const auto replaceable = [&playLevel, &sandboxViewer]() -> coney::GameMode* {
        if (playLevel) {
            return playLevel.get();
        }
        return sandboxViewer.get();
    };
    // Plays sandbox layout `name` in place of the play mode or sandbox viewer on top (or above whatever runs): with
    // the player when there is a disc for his character, else with the free camera.
    const auto playSandbox = [&](const std::string& name) {
        auto world = loadSandbox(*options, name);
        if (!world) {
            debugSession.print("levels: " + world.error().message);
            return;
        }
        // The mode it replaces must be on top, so no mode above it still runs on it.
        coney::GameMode* replaced = replaceable();
        if (replaced != nullptr && modes.top() != replaced) {
            debugSession.print("levels: finish the mode on top first");
            return;
        }
        if (wad) {
            auto mode = coney::platform::PlayLevelMode::createInSandbox(renderer, *wad, std::move(*world), std::nullopt,
                                                                        printText, sandboxSetup(*wad));
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
            playLevel->setSounds(audio ? &audio->sounds() : nullptr);
            playLevel->hud()->setSoundOutput(sandboxHudSound);
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
    // Starts a game session directly at level `name`, as `--play-level` does, in place of the sandbox's play mode or
    // viewer on top: the debug menus' first level jump when no session runs yet. The mode it replaces goes first, so
    // its sectors leave the budget before the level loads.
    const auto startSessionAt = [&](const std::string& name, int checkpoint) {
        coney::GameMode* replaced = replaceable();
        if (replaced != nullptr && modes.top() != replaced) {
            debugSession.print("levels: finish the mode on top first");
            return;
        }
        if (replaced != nullptr) {
            modes.pop();
        }
        playLevel.reset();
        sandboxViewer.reset();
        coney::platform::GameSession& started = makeSession();
        started.attachAudio(audio.get());
        started.setDebugDraw(&debugSession.debugDraw());
        started.startAtLevel(name, checkpoint);
        debugSession.print(std::format("levels: playing {} at checkpoint {}", name, checkpoint));
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

    // `--frames` (and so the screenshot of the last frame, which needs it: parseOptions makes sure of it). With
    // `--play-level` they count from the level's first frame of play, like the input script, so a run is as long
    // however long the loading screen and an intro movie take: the loop is stopped by the frame hook below instead.
    const std::optional<std::string> screenshotPath = options->screenshotPath;
    const std::optional<std::uint64_t> playFrameLimit = commandLineLevel ? frameLimit : std::nullopt;
    if (playFrameLimit) {
        frameLimit.reset();
    }
    if (screenshotPath && frameLimit) {
        renderer.requestCapture(*frameLimit - 1, *screenshotPath);
    }
    // How long a `--play-level` run may wait for its level to play before it gives up: 10 minutes of frames, longer
    // than any loading screen and intro movie.
    constexpr std::uint64_t kPlayStartFrames = 18000;
    std::uint64_t frameIndex = 0;
    std::optional<std::uint64_t> playOrigin;
    const auto playFramesDone = [&]() {
        const std::uint64_t frame = frameIndex++;
        if (!playFrameLimit) {
            return false;
        }
        if (!playOrigin && session && session->inPlay()) {
            playOrigin = frame;
            if (screenshotPath) {
                renderer.requestCapture(frame + *playFrameLimit - 1, *screenshotPath);
            }
        }
        if (!playOrigin && frame >= kPlayStartFrames) {
            std::fprintf(stderr, "coney: %s never started playing\n", commandLineLevel->c_str());
            return true;
        }
        return playOrigin && frame - *playOrigin >= *playFrameLimit;
    };

    // The main loop, paced as set up above.
    std::optional<coney::platform::Window> window = renderer.window();
    // The window's events go past the developer overlay first; while it has the keyboard, the keyboard pad is off.
    coney::FrameHooks hooks;
    coney::platform::HumanWatch humanWatch; // the humans' events for the event log
    hooks.beginFrame = [&window, &devOverlay, devices, &pendingSandbox, &playSandbox, &pendingStart, &startSessionAt,
                        &session, &playFramesDone, &deferredInput, &humanWatch, &playLevel, &sessionPlayMode,
                        &eventFile] {
        // The event log: what the last frame's step did to the humans, then the step the next frame runs.
        if (coney::events::enabled()) {
            humanWatch.update(playLevel ? playLevel.get() : sessionPlayMode());
            coney::events::setStep(coney::events::step() + 1);
            // Flushed every frame: a driver may end the run by killing Coney, and the log must hold every frame.
            if (eventFile) {
                eventFile->flush();
            }
        }
        // A sandbox or level the Levels page asked for, between two frames.
        if (pendingSandbox) {
            const std::string name = *pendingSandbox;
            pendingSandbox.reset();
            playSandbox(name);
        }
        if (pendingStart) {
            const std::pair<std::string, int> start = *pendingStart;
            pendingStart.reset();
            startSessionAt(start.first, start.second);
        }
        if (session) {
            session->beginFrame();
        }
        if (playFramesDone()) {
            return false;
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
    if (pacer) {
        coney::platform::FramePacer& paced = *pacer;
        // The pacer's measure of each frame is also the debug menus' frame time.
        hooks.waitForFrame = [&paced, &frameMilliseconds] {
            const std::uint64_t nanoseconds = paced.waitForFrame();
            frameMilliseconds = static_cast<double>(nanoseconds) / 1'000'000.0;
            return nanoseconds;
        };
        hooks.endFrame = [&paced, &audio](std::uint32_t steps) {
            paced.endFrame(steps);
            if (audio) {
                audio->endFrame(steps);
            }
        };
    } else if (audio) {
        hooks.endFrame = [&audio](std::uint32_t steps) { audio->endFrame(steps); };
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
    if (const coney::platform::PlayLevelMode* level = sessionPlayMode(); level != nullptr) {
        printText(level->summary());
    }
    if (session && session->movies().counts().movies > 0) {
        const coney::movies::MovieMode::Counts& counts = session->movies().counts();
        printText(std::format("movies: {} played ({} skipped, {} failed), {} frames decoded, {} shown, {} sound "
                              "samples, {} captions\n",
                              counts.movies, counts.skipped, counts.failed, counts.framesDecoded, counts.framesShown,
                              counts.samples, counts.captionsShown));
    }
    if (sandboxViewer) {
        printText(sandboxViewer->summary());
    }
    if (audio && options->audioTest) {
        printText(audio->summary());
    }
    // A game played in test mode, from the story or a level: what its humans asked to be heard and what the glass and
    // doors played (counts only).
    if (audio && testMode && session) {
        printText(audio->game().summary());
        printText(std::format("object sounds: {} played, {} material pairs, {} unplayed\n", objectSounds.played(),
                              objectSounds.materialPairs(), objectSounds.unplayed()));
    }

    // The game session goes before the sound output (declared further down, so destroyed first), whose game sound its
    // scripts' binding context and play modes still point at: tearing those down afterwards read freed sound state and
    // crashed `--play-level level95` at exit.
    session.reset();

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
