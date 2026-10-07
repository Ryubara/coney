// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/frame_rate_meter.h"
#include "debug/audio_controls.h"
#include "debug/input_gate.h"
#include "debug/lua_console.h"
#include "debug/menu_model.h"
#include "debug/menu_navigator.h"
#include "debug/native_caller.h"
#include "debug/play_controls.h"
#include "debug/time_control.h"
#include "debug/tunables.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney::debug {

/// What the Display page switches: overlays a front end draws over the game's screen.
struct DisplayOptions {
    bool frameStats = false;    ///< Frames run, steps run and the time controls, in a corner.
    bool fpsCounter = false;    ///< The frame rate, frame time and steps a second, under the frame stats.
    bool safeArea = false;      ///< The GUI square's edges: the overlay camera's built-in safe-area margin.
    bool logicalBounds = false; ///< The 640 x 448 logical screen's edges.
    bool coordinates = false;   ///< Player 1's feet and heading, in a corner (needs a play mode).
};

/// What the debug menus reach in the running game. Every field may be empty; a page whose service is missing says so
/// instead of acting.
struct DebugServices {
    /// The game's script system, when a game runs (the front end's); null otherwise. Native calls and the console use
    /// the session's sandbox state when it is null or has no state.
    std::function<script::ScriptSystem*()> scripts;
    /// The calls the game's recording stubs kept; null for none.
    std::function<const script::RecordedCalls*()> recorded;
    /// The game state (the level table, the difficulty); null for none.
    std::function<GameState*()> gameState;
    /// Starts the level named `name` (`level2`); returns false when it cannot. Empty: levels cannot be loaded yet.
    std::function<bool(std::string_view name)> loadLevel;
    /// The levels the play mode can play (`level2`), listed on the Levels page in place of the game state's level
    /// table; empty: the table is listed.
    std::function<std::vector<std::string>()> playableLevels;
    /// Where the Tunables page saves and loads overrides (`coney-tunables.ini` in the user's config folder, or the
    /// `--tunables` file); empty: no saving.
    std::string tunablesFile;
    /// The real time the last frame took, in milliseconds, as the platform measures it; empty in a run with no real
    /// clock (headless, tests), which then shows no frame time. Only shown and plotted: the steps never depend on it.
    std::function<double()> frameMilliseconds;
    /// The frame and step rates of the last half second (the frame pacer's FrameRateMeter), nothing before the first;
    /// empty in a run with no real clock. Only shown, like the frame time.
    std::function<std::optional<FrameRateReading>()> frameRate;
    /// The frame cap (frames a second, 0: none) and how to change it live; empty in a run with no real clock.
    std::function<std::uint32_t()> fpsCap;
    std::function<void(std::uint32_t)> setFpsCap;
    /// Whether a present waits for the vertical blank, and how to change it live; empty without a window.
    std::function<bool()> vsync;
    std::function<void(bool)> setVsync;
    /// The mode the player plays in, when one runs (the play mode); null otherwise. The Player, Camera and Spawner
    /// pages act on it, asking for it at each use, since the mode can change while a page is open.
    std::function<PlayControls*()> play;
    /// The folder of sandbox layouts the Levels page lists (listSandboxLayouts()); empty: none.
    std::filesystem::path sandboxFolder;
    /// Plays the sandbox layout named `name` (in place of the mode playing now); returns false when it cannot. Empty:
    /// sandboxes cannot be loaded from the menu in this run.
    std::function<bool(std::string_view name)> loadSandbox;
    /// The sound output (its volumes and test tone, for the Audio page); null in a run without sound.
    std::function<AudioControls*()> audio;
};

/// One debug session: the menu model with every page, the state its pages share (the time controls, the log, the Lua
/// console, the display switches, the sandbox script state) and the pad menu's navigator. The game makes one, gives
/// its InputGate to the mode stack, and gives the model to the front ends, which only render it.
///
/// The pages (src/debug/debug_pages.h): Favourites, Time, Tunables, Natives, Lua console, Cheats, Levels, Display,
/// Audio, Input. Adding a feature is adding a page or items here, once (docs/guides/debug-menu.md#adding-a-feature).
class DebugSession {
  public:
    /// A session over `tunables` (which must outlive it) and `services`; `inner` is the input source the gate wraps
    /// (null: none). `log` gets the lines the session prints (calls, results); empty: nowhere but its own log.
    DebugSession(TunableRegistry& tunables, DebugServices services, InputSource* inner,
                 std::function<void(std::string_view)> log = {});
    ~DebugSession();
    DebugSession(const DebugSession&) = delete;
    DebugSession& operator=(const DebugSession&) = delete;
    DebugSession(DebugSession&&) = delete;
    DebugSession& operator=(DebugSession&&) = delete;

    /// The menu model the front ends render.
    [[nodiscard]] MenuModel& model() { return m_model; }
    /// The pad menu's navigation state.
    [[nodiscard]] MenuNavigator& navigator() { return m_navigator; }
    /// The input source to give the mode stack in place of `inner`.
    [[nodiscard]] InputGate& gate() { return m_gate; }
    /// The time controls; the main loop asks it whether to step.
    [[nodiscard]] TimeControl& time() { return m_time; }
    /// The tunables.
    [[nodiscard]] TunableRegistry& tunables() { return m_tunables; }
    /// The overlays to draw.
    [[nodiscard]] DisplayOptions& display() { return m_display; }
    /// What the Debug draw page switches, for the play mode to draw.
    [[nodiscard]] DebugDrawOptions& debugDraw() { return m_debugDraw; }
    /// The mode the player plays in now (DebugServices::play), or null.
    [[nodiscard]] PlayControls* play() const { return m_services.play ? m_services.play() : nullptr; }
    /// The session's log: native calls, results, console output.
    [[nodiscard]] DebugLog& log() { return m_log; }
    /// The Lua console.
    [[nodiscard]] LuaConsole& console() { return m_console; }
    /// The services.
    [[nodiscard]] const DebugServices& services() const { return m_services; }

    /// The VM native calls and the console run in: the game's when it has a state, else the sandbox's (made on first
    /// use). Never null.
    [[nodiscard]] script::LuaVm& vm();
    /// Whether vm() is the game's state.
    [[nodiscard]] bool usingGameScripts() const;
    /// The recorded calls of the state vm() is in; null for none.
    [[nodiscard]] const script::RecordedCalls* recorded();

    /// Prints `line` to the session's log and to the log callback.
    void print(std::string line);

    /// The lines the Display page's corner overlays show now, top first: the frame stats and the FPS counter, each
    /// while switched on. A front end draws them in the top-right corner, with the menu open or closed.
    [[nodiscard]] std::vector<std::string> cornerLines() const;

  private:
    TunableRegistry& m_tunables;
    DebugServices m_services;
    std::function<void(std::string_view)> m_logCallback;
    MenuModel m_model;
    MenuNavigator m_navigator{m_model};
    InputGate m_gate;
    TimeControl m_time;
    DisplayOptions m_display;
    DebugDrawOptions m_debugDraw;
    DebugLog m_log;
    LuaConsole m_console;
    std::unique_ptr<SandboxScripts> m_sandbox;
};

} // namespace coney::debug
