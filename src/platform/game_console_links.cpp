// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_console_links.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/display_preset.h"
#include "install/installer.h"

// @coney-only what the console takes from the running game (docs/guides/console.md#in-the-game); the original has no
// console.

namespace coney::platform {

namespace {

// The frame clock's rhythm for a frame cap: 30, the original's one step a frame, is lockstep; any other cap blends
// between steps (the entry points' own rule when they start).
FramePacing pacingForCap(std::uint32_t cap) {
    constexpr std::uint32_t kStepsPerSecond = 30;
    return cap == kStepsPerSecond ? FramePacing::Lockstep : FramePacing::Interpolated;
}

} // namespace

std::vector<GameSetting> playerSettings(const GameConsoleSources& sources) {
    std::vector<GameSetting> settings;
    // The frame cap: with a frame pacer only, as a lockstep run has no real clock to cap.
    if (FramePacer* pacer = sources.pacer; pacer != nullptr) {
        RenderEngine* renderer = sources.renderer;
        FrameClock* clock = sources.clock;
        settings.push_back(wholeSetting(
            "fps_cap", "Frames a second at most; 0 for no cap, 30 for the original's one step a frame", 0, 1000,
            [pacer] { return static_cast<int>(pacer->cap()); },
            [pacer, renderer, clock](int cap) {
                const auto frames = static_cast<std::uint32_t>(cap);
                pacer->setCap(frames);
                // The GPU renderer's present mode follows the cap.
                if (renderer != nullptr) {
                    renderer->setFpsCap(frames);
                }
                if (clock != nullptr) {
                    clock->setPacing(pacingForCap(frames));
                }
            }));
    }
    // Vsync: in a window only.
    if (RenderEngine* renderer = sources.renderer; renderer != nullptr && renderer->window().has_value()) {
        settings.push_back(boolSetting(
            "vsync", "Whether a frame waits for the display's vertical blank", [renderer] { return renderer->vsync(); },
            [renderer](bool on) { renderer->setVsync(on); }));
    }
    // The master volume of the game's sound.
    if (AudioOutput* audio = sources.audio; audio != nullptr) {
        settings.push_back(numberSetting(
            "volume", "The master volume, from 0 (silent) to 1", 0.0, 1.0,
            [audio] { return static_cast<double>(audio->volume(0)); },
            [audio](double volume) { audio->setVolume(0, static_cast<float>(volume)); }));
    }
    // The display preset: the PS2's picture, or what the Xbox port draws and beyond (docs/engine/renderer.md). A run
    // in test mode draws the pinned preset whatever this says.
    settings.push_back(choiceSetting(
        "display_preset", "The picture: ps2, xbox, enhanced or high", {"ps2", "xbox", "enhanced", "high"},
        [] { return std::string(displayPresetName(displaySettings().preset())); },
        [](std::string_view name) {
            if (const std::optional<DisplayPreset> preset = parseDisplayPresetName(name)) {
                displaySettings().choose(*preset);
            }
        }));
    return settings;
}

GameConsoleLinks makeGameConsoleLinks(const GameConsoleSources& sources) {
    GameConsoleLinks links;
    const auto session = [find = sources.session]() -> GameSession* { return find ? find() : nullptr; };
    // What exists: the game over a disc and the level in play. There is no profile system to read yet, so no profile.
    links.has = [session](console::Need need) {
        switch (need) {
        case console::Need::Game:
        case console::Need::Disc:
            return session() != nullptr;
        case console::Need::Level:
            return session() != nullptr && session()->inPlay();
        case console::Need::Profile:
            break;
        }
        return false;
    };
    // Stand-in: the only disc Coney knows is the NTSC-U PlayStation 2 one (install/known_discs.cpp), so a disc's region
    // is read as that; it reads the disc's own identity once Coney knows more than one.
    links.buildInfo = [session, debugTools = sources.debugTools, kind = sources.rendererKind] {
        return console::BuildInfo{.version = std::string(install::coneyVersion()),
                                  .build = debugTools ? "debug tools build" : "release build",
                                  .discRegion = session() != nullptr ? "NTSC-U" : "",
                                  .renderer = std::string(rendererName(kind))};
    };
    links.levelName = [session]() -> std::string {
        GameSession* game = session();
        if (game == nullptr || !game->inPlay()) {
            return {};
        }
        return game->flow().levelFlow().currentLevel();
    };
    links.settings = playerSettings(sources);
    links.writeBugReport = sources.writeBugReport;
    return links;
}

} // namespace coney::platform
