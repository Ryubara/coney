// SPDX-License-Identifier: GPL-3.0-or-later
// The scheduler: when scripts run. Every run of a script, whatever starts it, is an entry of the ScriptScheduler, in
// the order the game's frame gives (docs/guides/asset-and-script-architecture.md#script-parts,
// docs/guides/studio/lua-sdk.md#play).
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "scripting/lua_value.h"

namespace coney::script {

class LuaVm;
class ScriptSystem;

/// What starts a script run.
enum class ScriptEntryKind : std::uint8_t {
    Files,        ///< WAD scripts run in turn: the legal screen's preload lists.
    File,         ///< One WAD script: `doFile`, a level's helper chunk, each script of a list or a level entry.
    LevelEntry,   ///< A level's scripts: `global.lua`, then `<level>.lua`.
    Preloads,     ///< The files `preLoadFile` asked for arriving, each with its callback (the level start's preload).
    Frame,        ///< The scripts' frame of a step: arriving preloads, the due scheduled calls, the update function.
    Call,         ///< A function called by name from C++: a callback a binding kept (menus, pads, spawners, scenes).
    Message,      ///< An object's message handler, the trigger boxes' and spheres' events (3, 4, 5) among them.
    AnimCallback, ///< An animation callback: a human started the anim id it watches.
    Console,      ///< Coney's debug menus: the Lua console and the Natives page's calls.
};

/// The name of a kind, for logs and tests (`frame`, `message`).
[[nodiscard]] std::string_view scriptEntryKindName(ScriptEntryKind kind);

/// The project the game's own scripts run as. Mod projects will run under their own names (lua-sdk.md#play).
inline constexpr std::string_view kGameProject = "game";

/// One script run to make: what starts it and what it needs. The views it holds (names, files, arguments, the console
/// body) must last until run() returns; an entry is made where it is run.
struct ScriptEntry {
    ScriptEntryKind kind = ScriptEntryKind::Call;
    std::string_view project = kGameProject; ///< Whose time the run counts as; a string that outlives the run.
    std::string_view name;                   ///< The file, the level, or the function (dotted, as call() takes).
    std::span<const std::string_view> files; ///< Files: the scripts, in order.
    std::span<const Value> args;             ///< Call, Message, AnimCallback: the arguments.
    std::uint64_t nowMs = 0;                 ///< Frame: the game time.
    double stepSeconds = 0.0;                ///< Frame: the step since the last frame.
    bool all = false;                        ///< Preloads: also the files those scripts ask for.
    const std::function<void(LuaVm&)>* body = nullptr; ///< Console: what to run in the state's VM.

    /// The scripts `names`, in turn.
    [[nodiscard]] static ScriptEntry fileList(std::span<const std::string_view> names);
    /// The WAD script `name`.
    [[nodiscard]] static ScriptEntry file(std::string_view name);
    /// The level entry of `level`.
    [[nodiscard]] static ScriptEntry levelEntry(std::string_view level);
    /// The preloaded files arriving; with `all`, the files they ask for too.
    [[nodiscard]] static ScriptEntry preloads(bool all);
    /// The scripts' frame at game time `nowMs`, `stepSeconds` after the last.
    [[nodiscard]] static ScriptEntry frame(std::uint64_t nowMs, double stepSeconds);
    /// A call of the function `name` with `args`.
    [[nodiscard]] static ScriptEntry call(std::string_view name, std::span<const Value> args = {});
    /// A message handler `name` called with `args`.
    [[nodiscard]] static ScriptEntry message(std::string_view name, std::span<const Value> args);
    /// An animation callback `name` called with `args`.
    [[nodiscard]] static ScriptEntry animCallback(std::string_view name, std::span<const Value> args);
    /// The debug menus' `body`, run in the state's VM.
    [[nodiscard]] static ScriptEntry console(const std::function<void(LuaVm&)>& body);
};

/// How long one project's scripts ran: in the step that ended last, and so far in the step now running. Recorded for
/// a monitor; nothing in the game reads it, so a run's results never depend on the clock.
struct ProjectTime {
    std::string project;                 ///< The project (kGameProject).
    std::chrono::nanoseconds lastStep{}; ///< The whole of the last step that ended (with a Frame entry).
    std::chrono::nanoseconds thisStep{}; ///< The step running now, so far.
    std::uint64_t steps = 0;             ///< Steps ended.
};

/// The one way any script runs. Game code starts a run with run(); the script system's own operations (runFile(),
/// call(), update(), ...) open their entry here too, so a run started from anywhere is an entry: watched, timed, and
/// nested inside the entry that caused it (a scheduled call inside the frame, a file inside a level entry).
///
/// **The order** is the frame's, which the game mode keeps (docs/guides/studio/lua-sdk.md#play, step 7): the gameplay
/// step's messages and callbacks (trigger boxes and spheres, animation callbacks, pads), then the Frame entry, inside
/// which the update runs in the original's order: the preloads that arrived, the due scheduled calls oldest first
/// (due time, then the order scheduled), then the update function
/// (docs/research/scripting.md#scheduled-calls).
///
/// **No budget.** The maintainer decided on 2026-10-29 that scripts have no instruction or time budget for now: the
/// scheduler only records each project's time per step (projectTime()), for a monitor to come. A step ends with the
/// outermost Frame entry. Coney's own; the original's script system has no such object.
class ScriptScheduler {
  public:
    /// The time now, for the per-step record; by default the profiler's clock (perf::now(), which the platform
    /// installs; none in tests and test mode, so nothing is timed there).
    using Clock = std::function<std::chrono::nanoseconds()>;
    /// Told of every entry as it starts, with how deep it is nested (0 for one started from outside any run).
    using Watcher = std::function<void(const ScriptEntry& entry, int depth)>;

    /// The scheduler of `scripts`' state.
    explicit ScriptScheduler(ScriptSystem& scripts);

    /// Runs `entry` through the script system. Returns the results for a call kind (Call, Message, AnimCallback),
    /// nothing when the function is missing or fails; for the other kinds an empty list, or nothing when a File fails.
    std::optional<std::vector<Value>> run(const ScriptEntry& entry);

    /// Whether an entry is running (a binding is asking from inside a script).
    [[nodiscard]] bool running() const { return m_depth > 0; }
    /// The time `project`'s scripts ran; null before its first entry.
    [[nodiscard]] const ProjectTime* projectTime(std::string_view project = kGameProject) const;
    /// Every project's time, in the order each first ran.
    [[nodiscard]] std::span<const ProjectTime> projectTimes() const { return m_times; }

    /// Replaces the clock (a test's own); an empty one restores the profiler's.
    void setClock(Clock clock);
    /// Watches every entry from now on (a trace, a test); an empty one stops.
    void watch(Watcher watcher) { m_watcher = std::move(watcher); }

  private:
    friend class ScriptSystem;

    // An entry open while its run lasts: records its start and, when it closes, its time.
    class Scope {
      public:
        Scope(ScriptScheduler& scheduler, const ScriptEntry& entry);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        Scope(Scope&&) = delete;
        Scope& operator=(Scope&&) = delete;

      private:
        ScriptScheduler& m_scheduler;
        ScriptEntryKind m_kind;
        std::string_view m_project;
    };

    // Opens `entry` for the script system's operation that runs it.
    [[nodiscard]] Scope enter(const ScriptEntry& entry) { return Scope(*this, entry); }
    // The record of `project`, made on its first entry.
    ProjectTime& timeOf(std::string_view project);
    // The clock's reading.
    [[nodiscard]] std::chrono::nanoseconds now() const;

    ScriptSystem& m_scripts;
    Clock m_clock;
    Watcher m_watcher;
    std::vector<ProjectTime> m_times;
    std::chrono::nanoseconds m_outerStart{}; // when the outermost entry running began
    int m_depth = 0;
};

} // namespace coney::script
