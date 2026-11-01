// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "console/command_spec.h"
#include "console/owned_spec.h"
#include "core/error.h"

// @coney-only the Coney console's command registry (docs/guides/console.md#registry); the original has no console.

namespace coney::console {

/// A registered command, by id rather than by pointer: a queued line keeps its command's id, and a command removed
/// before the queue runs (its mod stopped) is found missing instead of dangling.
struct CommandId {
    std::uint32_t value = 0;
    friend constexpr auto operator<=>(CommandId, CommandId) = default;
};

/// A player slot: 0 is player 1 (the host, offline the one player), 1 player 2. Whose line a command runs for.
using PlayerSlot = std::uint8_t;

/// The arguments a handler runs with: every argument of its spec by name, checked against its type and with the
/// defaults of those left out filled in (empty for an optional one with no default).
class CommandArgs {
  public:
    CommandArgs(std::span<const ArgSpec> specs, std::vector<std::string> values);

    /// The value of the argument named `name` (CONEY_ASSERT that the spec has it).
    [[nodiscard]] std::string_view get(std::string_view name) const;
    /// The values in the spec's order.
    [[nodiscard]] std::span<const std::string> values() const { return m_values; }

  private:
    std::vector<std::string> m_names; // copies: a handler may replace its own registration
    std::vector<std::string> m_values;
};

/// What a handler is given: its arguments, the slot of the player whose line it is, and where its output goes (the
/// terminal and the run log).
struct CommandContext {
    const CommandArgs& args;
    PlayerSlot slot = 0;
    std::function<void(std::string_view text)> print;
};

/// A command's code. Its error is one line the terminal shows as the command's failure.
using Handler = std::function<std::expected<void, std::string>(CommandContext& context)>;

/// Who registered a command.
enum class Origin : std::uint8_t {
    Builtin, ///< Coney: the player catalog, or in a build with the debug tools the developer set.
    Mod,     ///< A running mod, through the SDK (docs/guides/studio/console.md#mod-commands).
};

/// One registered command.
struct Command {
    CommandId id;
    Origin origin = Origin::Builtin;
    std::string mod; ///< The mod that registered it; empty for a built-in.
    std::unique_ptr<const OwnedSpec> owned;
    Handler handler;

    /// Its specification.
    [[nodiscard]] const CommandSpec& spec() const { return owned->spec(); }
};

/// The longest name a mod's command may have, and how many one mod may register.
inline constexpr std::size_t kModCommandNameMax = 32;
inline constexpr std::size_t kModCommandsMax = 64;
/// The longest built-in name the registry takes (the catalog's names are far shorter).
inline constexpr std::size_t kBuiltinNameMax = 64;

/// The console's commands: the built-ins, registered at start-up through addBuiltin() (the player catalog by
/// registerBuiltins(), and in a build with the debug tools the developer set), and the running mods' commands
/// (docs/guides/console.md#registry). The one registration call for each kind; the console itself names no command.
class Registry {
  public:
    /// A registry that takes commands up to `highest`: the release build's takes Player only.
    explicit Registry(Access highest = Access::Player) : m_highest(highest) {}

    /// Registers a built-in. Fails with ErrorCode::InvalidArgument when the name is badly spelled, already a
    /// built-in's or a mod's, or the access level is above the registry's.
    [[nodiscard]] std::expected<CommandId, Error> addBuiltin(const CommandSpec& spec, Handler handler);

    /// Registers `mod`'s command `spec` (its access is Player whatever the spec says). Registering a name the same mod
    /// already has replaces that command, keeping its id. Fails with ErrorCode::InvalidArgument when the name is badly
    /// spelled or over kModCommandNameMax, a built-in's, or the mod already has kModCommandsMax commands.
    [[nodiscard]] std::expected<CommandId, Error> addModCommand(std::string_view mod, const CommandSpec& spec,
                                                                Handler handler);

    /// Removes every command of `mod` (it stopped, was disabled or restarts) and returns how many.
    std::size_t removeMod(std::string_view mod);

    /// The Coney features that are built: a command whose `available` names another is absent (not listed, not
    /// completed, unknown when typed).
    void setBuiltFeatures(std::vector<std::string> features) { m_features = std::move(features); }

    /// The command `name` names: a built-in's name, a mod command's bare name when one mod alone has it, or
    /// `<mod>:<name>`. Fails with ErrorCode::NotFound for an unknown name, saying the closest known one when there is
    /// one (`unknown command "noclp"; did you mean "noclip"? help lists the commands`), or ErrorCode::InvalidArgument
    /// for a bare name two mods have, listing both.
    [[nodiscard]] std::expected<CommandId, Error> resolve(std::string_view name) const;

    /// The command of `id`, or null when it was removed or is absent.
    [[nodiscard]] const Command* find(CommandId id) const;

    /// The names a line may use, sorted: every present built-in, each mod command's bare name when it is unique and
    /// its `<mod>:<name>` form when it is not.
    [[nodiscard]] std::vector<std::string> names() const;

    /// The present commands in registration order (built-ins first, as they register at start-up).
    [[nodiscard]] std::vector<const Command*> commands() const;

  private:
    // Whether a command is present: registered and its feature built.
    [[nodiscard]] bool present(const Command& command) const;
    // The present built-in named `name`, or null.
    [[nodiscard]] const Command* builtin(std::string_view name) const;
    // The present mod commands named `name` (bare).
    [[nodiscard]] std::vector<const Command*> modCommands(std::string_view name) const;
    // Stores a new command and returns its id.
    CommandId store(Origin origin, std::string_view mod, const CommandSpec& spec, Handler handler);

    Access m_highest;
    std::vector<std::unique_ptr<Command>> m_commands; // by id - 1; null once removed
    std::vector<std::string> m_features;
};

} // namespace coney::console
