// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"

namespace coney::script {

/// What the bindings ask of the game outside the script system and the game state: the menus, the level flow, the
/// screen effects and the audio and movie stand-ins. The front end (gamemodes/start_up_flow.h) implements it.
class BindingHost {
  public:
    virtual ~BindingHost() = default;
    BindingHost() = default;
    BindingHost(const BindingHost&) = delete;
    BindingHost& operator=(const BindingHost&) = delete;
    BindingHost(BindingHost&&) = delete;
    BindingHost& operator=(BindingHost&&) = delete;

    /// `ShowProfileManager(onRumble, onStartGame)`: keep the two callback names, show the menus.
    virtual void showProfileManager(std::string_view onRumble, std::string_view onStartGame) = 0;
    /// `ShowRumbleModeInterface(onCancel, onStart, n)`: show the Rumble mode's menus.
    virtual void showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double players) = 0;
    /// `MenuLoadLevel(name)`: choose the level the level flow starts next.
    virtual void menuLoadLevel(std::string_view level) = 0;
    /// `PlayMovie(name)`.
    virtual void playMovie(std::string_view name) = 0;
    /// `SoundPlayMusicTrack(track)` and `SoundLoopMusicTrack(track)`.
    virtual void playMusic(std::string_view track) = 0;
    /// `SoundStopMusicTrack()`.
    virtual void stopMusic() = 0;
    /// `ScreenQueueEffect(type, seconds)`: 0 fades in, 1 fades out.
    virtual void queueScreenEffect(int type, double seconds) = 0;
    /// `HUDLaunchMissionComplete(kind)`: show the mission-complete mode (0xb) with `kind`. Does nothing by default, for
    /// a host without game modes.
    virtual void launchMissionComplete(int /*kind*/) {}
};

/// How far Coney implements a binding.
enum class BindingKind : std::uint8_t {
    Real,   ///< Does the binding's job.
    Routed, ///< Hands the request to a Coney stand-in (music, movies, the Rumble mode menus), which logs it.
    Stub,   ///< Does nothing but return its documented default: its subsystem does not exist in Coney yet.
};

/// What a stub returns, by the original's conventions (docs/research/scripting.md#argument-and-result-conventions).
enum class StubResult : std::uint8_t {
    Nothing, ///< No result (nil to a script that reads one).
    Handle,  ///< A new handle, a number: what the original's object-making bindings return.
    Zero,    ///< The number 0.
    False,   ///< false: nil.
    True,    ///< true: the number 1.
};

/// One entry of the binding table.
struct BindingInfo {
    std::string_view name;
    BindingKind kind;
    StubResult stubResult = StubResult::Nothing; ///< For a stub.
    bool records = false;                        ///< A stub that keeps its arguments (RecordedCalls).
};

/// Every binding Coney registers, real and stub, by name: the game's front-end path needs these. The original
/// registers 956 (docs/research/scripting.md#bindings); the others are not set, and a call of one is skipped
/// (ScriptSystem).
[[nodiscard]] std::span<const BindingInfo> bindingTable();

/// Bindings in the table of `kind`.
[[nodiscard]] std::size_t bindingCount(BindingKind kind);

/// The arguments stub bindings received, by binding name, for configuration that later subsystems will need
/// (`CfgObj`, `CfgChar`, ...): kept untyped until the research describes the arguments. Numbers and strings are kept;
/// a table argument is kept as nil.
class RecordedCalls {
  public:
    /// Keeps one call of `binding` with `args`.
    void add(std::string_view binding, std::span<const Value> args);
    /// The calls of `binding`, oldest first; empty when there were none.
    [[nodiscard]] std::span<const std::vector<Value>> calls(std::string_view binding) const;
    /// Calls of `binding`.
    [[nodiscard]] std::size_t count(std::string_view binding) const { return calls(binding).size(); }
    /// Calls kept, of every binding.
    [[nodiscard]] std::size_t total() const;
    /// Forgets every call.
    void clear() { m_calls.clear(); }

  private:
    std::map<std::string, std::vector<std::vector<Value>>, std::less<>> m_calls;
};

/// What the bindings work on. Everything must outlive the script system's states.
struct BindingContext {
    GameState* state = nullptr;            ///< Read by the getters, filled by `CfgLevelName`.
    gui::GlobalStrings* strings = nullptr; ///< Filled by `CfgHUDMessage` and the other string bindings.
    BindingHost* host = nullptr;           ///< Menus, level flow, screen effects, audio and movies.
    RecordedCalls* recorded = nullptr;     ///< The recording stubs' arguments.
    CreatedHumans* humans = nullptr;       ///< Where `HuCreate` keeps the humans it makes; null keeps none.
};

/// Registers every binding of bindingTable() in `vm`, a fresh state of `scripts`: the real ones working on `context`,
/// the stubs returning their defaults. Use it as (part of) the ScriptSystem's BindingInstaller.
/// @orig 0x0037d420 RegisterBindings (unknown)
void installBindings(ScriptSystem& scripts, LuaVm& vm, const BindingContext& context);

} // namespace coney::script
