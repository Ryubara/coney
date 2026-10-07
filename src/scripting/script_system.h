// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"

namespace coney::script {

/// Reads a script by its WAD file name, such as `global.lua`.
using ScriptSource = std::function<std::expected<std::vector<std::byte>, Error>(std::string_view name)>;

/// The scripts the legal screen runs, in order: the enumerations, then the configuration
/// (docs/research/scripting.md#life-of-the-lua-state). The original keeps them as two NULL-terminated lists
/// (`0x00512b08`, `0x00512b10`).
inline constexpr std::array<std::string_view, 1> kEnumPreloadScripts{"enum_preload.lua"};
inline constexpr std::array<std::string_view, 3> kConfigPreloadScripts{"config_preload.lua", "config_preload2.lua",
                                                                       "config_preload3.lua"};
/// The script every level runs before its own (`<level>.lua`).
inline constexpr std::string_view kGlobalScript = "global.lua";

/// The game's script system: the one Lua 4.0 state, the scripts it runs, the calls C++ makes into it by name, and the
/// schedule of delayed calls. The original's object (`g_ScriptSystem`, 0x28 bytes) is made at start-up and destroyed
/// and made again by every level unload, so a level starts from the bindings alone; create() and destroy() are that
/// pair.
///
/// - **A fresh state** has the string, base and math libraries, then the game's bindings (the installer given to the
///   constructor), and `_ERRORMESSAGE` and `_ALERT` set to functions that do nothing: a script error stops that chunk
///   or call and leaves no trace in the original. Coney still writes each error to its log and counts it (errors()),
///   so a run shows what failed.
/// - **Scripts** are WAD files run by name (runFile()); the level entry runs `global.lua` then `<level>.lua` in the
///   same state, so a level sees whatever ran before it.
/// - **Calls from C++** name a function as the scripts do, dotted (`Menu.onStart`); a `:` (`Obj:method`) passes the
///   table as `self`. Names are looked up when the call happens, so a script may replace the function behind a name.
/// - **The schedule** holds calls of named functions with up to two number arguments, due a number of milliseconds of
///   game time later; update() runs those that are due, in order.
///
/// Coney's choices, where the research is silent or Coney differs (docs/research/scripting.md#coneys-implementation):
/// - A call of an unset global (the game's state has all 956 bindings; Coney's has the ones it lists) is skipped as a
///   no-op returning nothing, and logged once per name, rather than stopping the script.
/// - Calls due at the same time run in the order they were scheduled; a call scheduled while update() runs waits for
///   the next update, even with a delay of 0.
/// - No garbage collection: Coney's VM frees by reference counting, so the forced collection every 2 s and `gc()` do
///   nothing.
/// - A dotted name's parts are not cut at 32 characters as the original's lookup buffer is.
///
/// Research: docs/research/scripting.md
class ScriptSystem {
  public:
    /// Adds the game's bindings to a freshly made state. It runs on every create(), so the bindings may capture the
    /// state's script system but must not keep the VM past destroy().
    using BindingInstaller = std::function<void(ScriptSystem& scripts, LuaVm& vm)>;
    /// Writes one line (ending in a newline) to Coney's log.
    using Log = std::function<void(std::string_view line)>;

    /// A script system that reads scripts through `source`, installs bindings with `install` and logs to `log` (either
    /// may be empty). No state exists until create().
    ScriptSystem(ScriptSource source, BindingInstaller install, Log log);

    /// Makes the Lua state: libraries, bindings, the silent error handlers; drops any scheduled calls and the update
    /// function. Replaces a state that exists. Must not be called from inside a script (CONEY_ASSERT).
    /// @orig 0x00356390 ScriptSystem_Create (ScriptLua.cpp)
    /// @orig 0x003564d8 ScriptSystem::ScriptSystem (ScriptLua.cpp)
    void create();

    /// Closes the state and drops the schedule. Must not be called from inside a script (CONEY_ASSERT).
    /// @orig 0x00356450 ScriptSystem_Destroy (ScriptLua.cpp)
    /// @orig 0x00356610 ScriptSystem::~ScriptSystem (ScriptLua.cpp)
    void destroy();

    /// Whether a state exists.
    [[nodiscard]] bool exists() const { return m_vm != nullptr; }
    /// The state's VM. Only while exists() (CONEY_ASSERT).
    [[nodiscard]] LuaVm& vm();
    /// How many states create() has made: 1 after start-up, one more per level unload.
    [[nodiscard]] std::uint64_t generation() const { return m_generation; }

    /// Runs the script `name` (a WAD name such as `config_preload2.lua`), then would collect garbage. Returns false
    /// when the script is missing, damaged or fails; the failure is logged and counted, never passed on.
    /// @orig 0x00356af8 ScriptSystem::RunFile (ScriptLua.cpp)
    bool runFile(std::string_view name);

    /// Runs each script of `names` in turn (runFile()); a failing one does not stop the others.
    /// @orig 0x00356c58 ScriptSystem::RunFiles (ScriptLua.cpp)
    void runFiles(std::span<const std::string_view> names);

    /// The level entry: runs `global.lua`, then `<level>.lua`.
    /// @orig 0x003569d8 ScriptSystem::EnterLevel (ScriptLua.cpp)
    void enterLevel(std::string_view level);

    /// Calls the function named `name` (`Menu.onStart`, `Obj:method`) with `args`. Returns false when no state exists,
    /// the name does not resolve to a function (logged) or the call fails (logged and counted).
    /// @orig 0x00356e08 ScriptSystem::FindFunction (ScriptLua.cpp)
    /// @orig 0x00357188 ScriptSystem::Call (ScriptLua.cpp)
    bool call(std::string_view name, std::span<const Value> args = {});
    /// call(), returning the function's results; nothing when call() would return false.
    std::optional<std::vector<Value>> callResults(std::string_view name, std::span<const Value> args = {});
    /// Whether `name` (dotted as call() takes it) names a function in the current state.
    [[nodiscard]] bool hasFunction(std::string_view name) const;

    /// Schedules a call of `name` with `args` (at most two numbers; more is a programmer error) `delayMs` milliseconds
    /// of game time after now().
    /// @orig 0x003571b8 ScriptSystem::Schedule (ScriptLua.cpp)
    /// @orig 0x003572e8 ScriptSystem::ScheduleArg1 (ScriptLua.cpp)
    /// @orig 0x00357430 ScriptSystem::ScheduleArg2 (ScriptLua.cpp)
    void schedule(std::string name, std::uint64_t delayMs, std::span<const double> args = {});

    /// Drops scheduled calls: those of `name`, or all of them for an empty name.
    /// @orig 0x00357588 ScriptSystem::FlushScheduled (ScriptLua.cpp)
    void flushScheduled(std::string_view name = {});

    /// Sets the function update() calls every frame (empty for none).
    /// @orig 0x003578d8 ScriptSystem::SetUpdateFunction (ScriptLua.cpp)
    void setUpdateFunction(std::string name) { m_updateFunction = std::move(name); }
    /// The function update() calls every frame; empty for none.
    [[nodiscard]] const std::string& updateFunction() const { return m_updateFunction; }

    /// Sets the game time, in milliseconds, that schedule() counts from. A mode calls it at the top of its frame, so a
    /// call scheduled from that frame's input is timed from the frame.
    void setTime(std::uint64_t nowMs) { m_nowMs = nowMs; }
    /// The game time schedule() counts from.
    [[nodiscard]] std::uint64_t now() const { return m_nowMs; }

    /// One frame of the scripts at game time `nowMs`, `stepSeconds` after the last: runs the scheduled calls that are
    /// due, then the update function with the step in milliseconds.
    /// @orig 0x003566d8 ScriptSystem::Update (ScriptLua.cpp)
    void update(std::uint64_t nowMs, double stepSeconds);

    /// Asks for the script `name` without waiting (`preLoadFile`): it runs, then `callback` (when not empty) is called
    /// with no arguments, when the file arrives. Coney's reads are synchronous, so the file "arrives" at the next
    /// servicePreloads(): the top of the next update(), or the level start's preload. Until then the caller's script
    /// goes on, as in the original, where the read takes frames.
    /// @orig 0x00357a68 preLoadFile (unknown)
    void preload(std::string name, std::string callback);
    /// Runs the files preload() asked for, oldest first, each followed by its callback. With `all`, files asked for by
    /// those scripts run too (the level start's preload, which services the file manager until it is idle);
    /// otherwise they wait for the next call.
    /// @orig 0x00356d00 ScriptSystem_PreloadDone (unknown)
    void servicePreloads(bool all);
    /// Files preload() asked for that have not run yet.
    [[nodiscard]] std::size_t preloadsPending() const { return m_preloads.size(); }

    /// Scheduled calls waiting.
    [[nodiscard]] std::size_t scheduled() const { return m_schedule.size(); }
    /// Script errors since start-up (all states): failed runs and calls. The original keeps no such count.
    [[nodiscard]] std::uint64_t errors() const { return m_errors; }
    /// Calls of unset globals skipped since start-up (all states): bindings Coney does not list.
    [[nodiscard]] std::uint64_t skippedCalls() const;

    /// Writes `line` (and a newline) to the log.
    void log(std::string_view line) const;

    /// A developer's trace of what the scripts do: from now on (and in every state create() makes later) each call of
    /// a binding, and each call C++ makes into the scripts by name, writes one line to `trace` with its arguments; an
    /// empty `trace` stops it. Coney's own tool (`--script-trace`); the original has none.
    void traceCalls(Log trace);

    /// The file `name` through the script source (a level's `_objs.txt`, which InitLevel reads beside the scripts).
    [[nodiscard]] std::expected<std::vector<std::byte>, Error> readFile(std::string_view name) const;
    /// Sets where world objects' handles come from: the bindings' counter, which installBindings() hands over, so
    /// what the engine adds outside a binding takes handles from the same space.
    void setObjectHandles(std::function<double()> next) { m_objectHandles = std::move(next); }
    /// The next world object handle; 0 (the nil handle) before setObjectHandles().
    [[nodiscard]] double nextObjectHandle() const { return m_objectHandles ? m_objectHandles() : 0.0; }

  private:
    // One entry of the schedule (the original's is 0x18 bytes: due time, interned name, two flagged numbers).
    struct ScheduledCall {
        std::uint64_t dueMs = 0;
        std::uint64_t sequence = 0; // order of scheduling, which breaks ties between equal due times
        std::string function;
        std::vector<double> args;
    };

    // Runs a parsed chunk, logging and counting a failure; `name` names it in the log.
    bool runChunk(std::span<const std::byte> bytes, std::string_view name);
    // The function `name` resolves to, and the table to pass as `self` for a `:` name; nil function when none.
    [[nodiscard]] std::pair<Value, Value> resolve(std::string_view name) const;
    // Logs and counts a script error.
    void reportError(std::string_view where, const Error& error);
    // Logs, once per name, the calls the VM skipped because the global was unset.
    void noteSkippedCalls();
    // Wraps every binding of the current state so that its calls write to the trace (traceCalls()).
    void wrapBindingsForTrace();

    ScriptSource m_source;
    BindingInstaller m_install;
    std::function<double()> m_objectHandles; // the bindings' world object handle counter (setObjectHandles())
    Log m_log;
    std::unique_ptr<LuaVm> m_vm;
    std::vector<ScheduledCall> m_schedule;                       // kept sorted by (due time, sequence)
    std::vector<std::pair<std::string, std::string>> m_preloads; // preload(): (file, callback), oldest first
    std::uint64_t m_nextSequence = 0;
    std::string m_updateFunction;
    std::uint64_t m_nowMs = 0;
    std::uint64_t m_generation = 0;
    std::uint64_t m_errors = 0;
    std::uint64_t m_skippedBefore = 0;    // skipped calls of the states destroyed so far
    std::set<std::string> m_skippedNames; // names already logged
    std::shared_ptr<Log> m_trace;         // traceCalls()'s sink, shared with the wrapped bindings; null: no trace
};

} // namespace coney::script
