// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// @coney-only the Coney console (docs/guides/console.md); the original has no console.

/// The Coney console's model: the command specifications, the registry, the tokenizer and argument types, the
/// terminal, the step-boundary queue and the host interfaces through which commands reach the game
/// (docs/guides/console.md). It has no user interface and names nothing of the debug tools, the scripting runtime or
/// the game modes: it includes only `console/`, `core/` and the standard library (python/tests/
/// test_debug_tools_split.py).
namespace coney::console {

/// Which builds hold a command (docs/guides/studio/console.md#access): every build, or only builds with the debug
/// tools. A property of the code, not a switch: the release build registers no developer command.
enum class Access : std::uint8_t {
    Player,    ///< Every build, the release build included.
    Developer, ///< Only builds with the debug tools (`dev`, `playtest`, `ci`, `asan`) and Studio.
};

/// What a command touches, which decides who may run it in a network session.
enum class Scope : std::uint8_t {
    Local,   ///< This machine's view: a reading, a panel, a setting for this session, leaving.
    Session, ///< The game everyone in the session plays: only the host (or an offline player) may run it.
};

/// What must exist for a command to run (the catalog's `needs`), one bit each.
enum class Need : std::uint8_t {
    Game = 1U << 0U,    ///< A game session (a disc's game, not a viewer).
    Profile = 1U << 1U, ///< An active player profile.
    Level = 1U << 2U,   ///< A level in play.
    Disc = 1U << 3U,    ///< A disc to read.
};

/// A set of Need bits.
using Needs = std::uint8_t;

/// The bit of `need` in a Needs set.
[[nodiscard]] constexpr Needs needBit(Need need) { return static_cast<Needs>(need); }

/// What an argument holds, which brings its check and its completion (arg_types.h).
enum class ArgType : std::uint8_t {
    Int,        ///< A whole number.
    Number,     ///< A decimal number.
    Bool,       ///< `on` or `off`.
    Text,       ///< Any text; quoted when it has spaces.
    Choice,     ///< One of a fixed list (ArgSpec::choices).
    Mission,    ///< A mission's number, from the missions the profile allows.
    Checkpoint, ///< A checkpoint's number, of the mission named before it.
    Level,      ///< A level's name, from the levels the command may start.
    Mod,        ///< An installed mod's name.
    Setting,    ///< A setting's name.
    Command,    ///< A console command's name.
};

/// One argument of a command, as the catalog gives it. Views static text: the generated table's, or an OwnedSpec's.
struct ArgSpec {
    std::string_view name; ///< Shown in the usage line: `<mission>`, `[checkpoint]`.
    ArgType type = ArgType::Text;
    std::string_view help{};                     ///< One line saying what to give.
    bool optional = false;                       ///< May be left out (always, when it has a default).
    std::string_view defaultValue{};             ///< What a left-out argument holds; empty when it has none.
    std::span<const std::string_view> choices{}; ///< A Choice argument's values.
};

/// One built-in command, as `coney-tools console render-cpp` writes it from `src/console/commands.toml` into the
/// generated table (commands_table.cpp). The handler is bound by name when it is registered (catalog.h).
struct CommandSpec {
    std::string_view name;    ///< Lower case, words joined by `-` or `_`.
    std::string_view group{}; ///< The heading `help` and the reference list it under.
    Access access = Access::Player;
    Scope scope = Scope::Local;
    /// A local command that changes something (quitting, a setting) waits for the step boundary as a session command
    /// does; one that only reads runs at once.
    bool changes = false;
    std::string_view summary; ///< One line: what it does.
    Needs needs = 0;
    /// The Coney feature it waits for (`mission-select`); empty when it waits for none. Absent from the registry until
    /// that feature is built (Registry::setBuiltFeatures()).
    std::string_view available{};
    std::span<const ArgSpec> args{};
};

/// Whether a command runs at the next step boundary rather than at once: a session command, or a local one that
/// changes something (docs/guides/studio/console.md#terminal).
[[nodiscard]] constexpr bool runsAtStepBoundary(Scope scope, bool changes) {
    return scope == Scope::Session || changes;
}

/// The catalog's word for an access level (`player`, `developer`), as the generator spells it.
[[nodiscard]] std::string_view accessName(Access access);
/// The catalog's word for a scope (`local`, `session`).
[[nodiscard]] std::string_view scopeName(Scope scope);
/// The catalog's word for an argument type (`int`, `mission`), as refusals name it.
[[nodiscard]] std::string_view argTypeName(ArgType type);
/// The catalog's word for a need (`game`, `profile`).
[[nodiscard]] std::string_view needName(Need need);

/// Whether `name` is spelled as a command name may be: 1 to `maxLength` characters of lower-case letters, digits,
/// `-` and `_`, starting with a letter.
[[nodiscard]] bool isCommandName(std::string_view name, std::size_t maxLength);

} // namespace coney::console
