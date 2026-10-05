// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "debug/native_signatures.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

namespace coney::debug {

/// How far Coney has a binding, as the Natives page shows it.
enum class NativeStatus : std::uint8_t {
    Implemented, ///< Real in Coney's binding table: it does its job.
    Partial,     ///< Routed: handed to a stand-in (music, movies) that logs it.
    Stub,        ///< Registered, but returns its documented default and does nothing else.
    Missing,     ///< Not registered: a script's call of it is skipped.
};

/// The status of the binding `name`, from Coney's binding table (script::bindingTable()).
[[nodiscard]] NativeStatus nativeStatus(std::string_view name);
/// The name of a status, for the menus (`implemented`, `missing`).
[[nodiscard]] std::string_view nativeStatusName(NativeStatus status);

/// A value as the debug menus show it: a number in its shortest form, a string in quotes, `nil`, a table's entries (at
/// most 8), `function`.
[[nodiscard]] std::string formatValue(const script::Value& value);

/// The arguments of one call being edited, built from a binding's signature with each argument at its default (or 0,
/// false, an empty string, a table of `count` zeros). Front ends edit the fields; toValues() makes the call's values.
class NativeArguments {
  public:
    /// One argument's edited value; which fields count depends on the argument's type.
    struct Argument {
        double number = 0.0;         ///< Number, Integer, Handle.
        bool flag = false;           ///< Boolean.
        bool omitted = false;        ///< Leave the argument off (and every one after it), so its default applies.
        std::string text;            ///< String; a StringTable's elements separated by commas.
        std::vector<double> numbers; ///< NumberTable.
    };

    /// The arguments of `signature`, each at its default.
    explicit NativeArguments(const NativeSignature& signature);

    /// The signature.
    [[nodiscard]] const NativeSignature& signature() const { return *m_signature; }
    /// The edited values, one per argument.
    [[nodiscard]] std::vector<Argument>& values() { return m_values; }
    /// The edited values, one per argument.
    [[nodiscard]] const std::vector<Argument>& values() const { return m_values; }

    /// The Lua values to call with: up to the first omitted argument. Booleans are 1 or nil, as tolua reads them;
    /// tables are fresh tables (kept by tables(), so what the binding writes into them can be shown); userdata is nil.
    [[nodiscard]] std::vector<script::Value> toValues();
    /// The tables the last toValues() made, by argument index (null for a non-table argument).
    [[nodiscard]] const std::vector<std::shared_ptr<script::Table>>& tables() const { return m_tables; }

    /// The call as a script would write it: `BrDead(3, 1)`.
    [[nodiscard]] std::string callText() const;

  private:
    const NativeSignature* m_signature;
    std::vector<Argument> m_values;
    std::vector<std::shared_ptr<script::Table>> m_tables;
};

/// Calls the binding `name` with `args` in `vm` the way a script's call does: the global's function, called through
/// the VM, so a call from a debug menu behaves exactly as the same call from a script. Fails with ErrorCode::NotFound
/// when the global is not set (a binding Coney is missing), and as the call fails otherwise.
[[nodiscard]] std::expected<std::vector<script::Value>, Error> callNative(script::LuaVm& vm, std::string_view name,
                                                                          std::span<const script::Value> args);

/// The lines a debug session printed: calls, results, console output. Keeps the newest kCapacity lines.
class DebugLog {
  public:
    /// Lines kept.
    static constexpr std::size_t kCapacity = 200;

    /// Appends one line.
    void add(std::string line);
    /// The lines, oldest first.
    [[nodiscard]] std::vector<std::string> lines() const { return {m_lines.begin(), m_lines.end()}; }
    /// The newest `count` lines, oldest first.
    [[nodiscard]] std::vector<std::string> last(std::size_t count) const;
    /// Lines kept.
    [[nodiscard]] std::size_t size() const { return m_lines.size(); }
    /// Forgets every line.
    void clear() { m_lines.clear(); }

  private:
    std::deque<std::string> m_lines;
};

/// A script state of the debug menus' own, for runs with no game scripts (no disc, the sandbox): the bindings over a
/// fresh game state, with a host that logs what the menus and audio would do. Calls through it behave as Coney's
/// bindings do anywhere; only what they act on is the sandbox's.
class SandboxScripts final : public script::BindingHost {
  public:
    /// Makes the state; `log` gets every line the host and the scripts write.
    explicit SandboxScripts(std::function<void(std::string_view)> log);

    /// The script system, with a state.
    [[nodiscard]] script::ScriptSystem& scripts() { return m_scripts; }
    /// The calls the recording stubs kept.
    [[nodiscard]] const script::RecordedCalls& recorded() const { return m_recorded; }
    /// The game state the bindings act on.
    [[nodiscard]] GameState& state() { return m_state; }

    void showProfileManager(std::string_view onRumble, std::string_view onStartGame) override;
    void showRumbleModeInterface(std::string_view onCancel, std::string_view onStart, double players) override;
    void menuLoadLevel(std::string_view level) override;
    void playMovie(std::string_view name) override;
    void playMusic(std::string_view track) override;
    void stopMusic() override;
    void queueScreenEffect(int type, double seconds) override;

  private:
    std::function<void(std::string_view)> m_log;
    GameState m_state;
    gui::GlobalStrings m_strings;
    script::RecordedCalls m_recorded;
    script::BindingContext m_context;
    script::ScriptSystem m_scripts; // after everything its bindings refer to
};

} // namespace coney::debug
