// SPDX-License-Identifier: GPL-3.0-or-later

// The release build's entry point (docs/guides/building.md#debug-tools): the game as a player gets it, from the disc
// they own, with none of the debug tools compiled in. It takes a player's options alone (core::OptionSet::Player) and
// runs the story from boot as the original's main does (docs/research/boot.md#main), or the idle screen without a
// disc. Test mode (`--headless`, `--frames`, `--input-script`) stays for the release build's own smoke tests. The
// set-up both entry points share is platform/game_run.h; the debug tools' entry point is main.cpp.

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_main.h>

#include "audio/object_sounds.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "core/frame_clock.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/options.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/idle_mode.h"
#include "gui/global_strings.h"
#include "platform/audio_output.h"
#include "platform/enet_transport.h"
#include "platform/error_dialogs.h"
#include "platform/frame_pacer.h"
#include "platform/game_run.h"
#include "platform/game_session.h"
#include "platform/net_play.h"
#include "platform/render_engine.h"
#include "platform/sdl_input.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "platform/window.h"
#include "world/sector_budget.h"

using coney::platform::printText;

// Coney throws no exceptions; what could escape is a failed allocation inside the standard library or librw, which
// ends the program either way.
// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
    // A failure must end the run on stderr, not wait on a dialog: scripted and headless runs have nobody to click.
    coney::platform::reportErrorsToConsole();

    // A player's options; a developer's is refused with a message that names the debug tools. Exit 2 on a usage
    // error, as command-line tools do.
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    auto options = coney::parseOptions(args, coney::OptionSet::Player);
    if (!options) {
        const std::string usage = coney::usageText(coney::OptionSet::Player);
        std::fprintf(stderr, "coney: %s\n\n%.*s", options.error().message.c_str(), static_cast<int>(usage.size()),
                     usage.data());
        return 2;
    }
    if (options->showHelp) {
        printText(coney::usageText(coney::OptionSet::Player));
        return 0;
    }

    // The disc, then the chunk handlers, then the game-mode stack, in the original's order (docs/research/boot.md).
    auto disc = coney::platform::openGameDisc(*options);
    if (!disc) {
        std::fprintf(stderr, "coney: %s\n", disc.error().message.c_str());
        return 1;
    }
    std::optional<coney::io::Wad> wad = std::move(*disc);
    coney::chunk::ChunkHandlerTable chunkHandlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(chunkHandlers);
    coney::platform::addSpriteSheetHandlers(chunkHandlers);

    // Game time runs on the fixed 1/30 s step and never reads a real clock
    // (docs/guides/conventions.md#update-and-render); only the frame pacer below does, outside test mode.
    coney::GameTimer timer;
    timer.setFixedStep(true);
    coney::GameModeStack modes;
    std::optional<std::uint64_t> frameLimit;
    if (const std::optional<int> limit = options->frameLimit; limit) {
        frameLimit = static_cast<std::uint64_t>(*limit);
    }

    // The renderer: OpenGL in a window, or the headless NULL renderer.
    auto engine = coney::platform::RenderEngine::start(options->headless ? coney::platform::RenderBackend::Null
                                                                         : coney::platform::RenderBackend::OpenGl,
                                                       coney::platform::gameWindow(*options));
    if (!engine) {
        std::fprintf(stderr, "coney: %s\n", engine.error().message.c_str());
        return 1;
    }
    coney::platform::RenderEngine& renderer = **engine;
    renderer.setVsync(options->vsync);
    renderer.setLineBlend(options->lineBlend);

    // The game over the disc (platform/game_session.h), from boot; without a disc only the idle screen. Declared
    // after the renderer, so they are destroyed before it: their textures are librw's.
    coney::IdleMode idle(&renderer);
    coney::gui::GlobalStrings strings;
    coney::world::SectorBudget sectorBudget(coney::world::kSectorPoolSize);
    coney::audio::ObjectSounds objectSounds;
    std::optional<coney::platform::GameSession> session;
    if (wad) {
        session.emplace(renderer, modes, *wad, chunkHandlers, sectorBudget, strings, objectSounds,
                        coney::platform::gameSessionSettings(*options), printText);
    } else {
        modes.push(idle);
    }

    // Pad input (docs/research/frontend.md#input): a script in test mode, otherwise SDL's gamepads and keyboard when
    // there is a window. Declared after the renderer, so it is destroyed before SDL stops.
    std::unique_ptr<coney::InputSource> input;
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
            started->get()->setLog(printText);
            input = std::move(*started);
        } else {
            std::fprintf(stderr, "coney: %s; running without pads\n", started.error().message.c_str());
        }
    }
    // Network play (docs/guides/multiplayer.md), hosted or joined from the start with `--host` or `--join`; a port that
    // cannot be opened ends the run, as any bad argument does.
    coney::platform::NetPlay netPlay(coney::platform::makeEnetTransport, printText);
    if (!coney::platform::startNetPlay(netPlay, *options)) {
        return 1;
    }
    // The game reads its pads through network play: a guest sends what their game reads, and a host's pad port 2 is
    // the guest's pad (docs/guides/multiplayer.md#player-two).
    coney::platform::NetInput netInput(input.get(), netPlay);
    modes.setInput(&netInput);

    // The pacing (docs/guides/conventions.md#update-and-render): test mode runs in lockstep with no clock; otherwise
    // the frame pacer measures real time and the frame clock turns it into fixed steps, a cap of 30 being the
    // original's rhythm of one step a frame.
    const bool testMode = coney::isTestMode(*options);
    constexpr std::uint32_t kStepsPerSecond = 30;
    const auto fpsCap = static_cast<std::uint32_t>(options->fpsCap.value_or(0));
    coney::FrameClock clock(testMode || fpsCap == kStepsPerSecond ? coney::FramePacing::Lockstep
                                                                  : coney::FramePacing::Interpolated);
    std::optional<coney::platform::FramePacer> pacer;
    if (!testMode) {
        pacer.emplace(fpsCap, options->showFps ? std::function<void(std::string_view)>(printText)
                                               : std::function<void(std::string_view)>());
    }

    // The sound, then the story from boot (GameSession::startStory()).
    std::unique_ptr<coney::platform::AudioOutput> audio =
        coney::platform::startGameSound(*options, wad ? &*wad : nullptr);
    if (session) {
        session->attachAudio(audio.get());
        session->startStory();
    }

    // The main loop, paced as set up above.
    std::optional<coney::platform::Window> window = renderer.window();
    coney::FrameHooks hooks;
    hooks.beginFrame = [&window, &session] {
        if (session) {
            session->beginFrame();
        }
        return !window || window->pumpEvents();
    };
    if (pacer) {
        coney::platform::FramePacer& paced = *pacer;
        hooks.waitForFrame = [&paced, &clock, &netPlay] {
            // Joined to a host, network play's nudge (a step in 50 or so faster or slower, so this game's input
            // reaches the host in time); else 100 %. Only a real clock is sped up: test mode has no pacer.
            clock.setSpeed(netPlay.clockSpeedPercent());
            const std::uint64_t nanoseconds = paced.waitForFrame();
            netPlay.advance(nanoseconds);
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
    coney::platform::addNetPlayHooks(hooks, netPlay, pacer.has_value());
    modes.runUntilEmpty(timer, clock, hooks, frameLimit);
    if (pacer && options->showFps) {
        printText(pacer->summary());
    }
    coney::platform::finishNetPlay(netPlay, *options);
    if (session) {
        coney::platform::printGameSummary(*session, audio.get(), objectSounds, testMode);
    }
    // The game session goes before the sound output, whose game sound its scripts and play modes still point at.
    session.reset();
    return 0;
}
