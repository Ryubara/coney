// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/console_lua.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "debug/lua_console.h"

namespace coney::debug {

namespace {

// The command's one argument: the line, as the catalog has it (src/debug/console_commands.toml).
constexpr std::array<console::ArgSpec, 1> kLuaArgs{{
    {.name = "line", .type = console::ArgType::Text, .help = "A line of Lua 4.0; quote it when it holds spaces"},
}};

// The specification, as src/debug/console_commands.toml gives it.
constexpr console::CommandSpec kLuaSpec{
    .name = "lua",
    .group = "Scripts",
    .access = console::Access::Developer,
    .scope = console::Scope::Session,
    .summary = "Runs a line of Lua in the game's script state, as the Lua console page does.",
    .args = kLuaArgs,
};

} // namespace

std::expected<console::CommandId, Error> registerLuaCommand(console::Registry& registry, DebugSession& session) {
    return registry.addBuiltin(
        kLuaSpec, [&session](console::CommandContext& context) -> std::expected<void, std::string> {
            // The line runs as one scheduler entry of the state, its prints and values going to the terminal.
            const std::string line(context.args.get("line"));
            const LuaConsole::PrintSink toTerminal = [&context](const std::string& out) { context.print(out); };
            std::expected<std::vector<std::string>, Error> printed;
            session.runInState([&](script::LuaVm& vm) { printed = session.console().run(vm, line, toTerminal); });
            if (!printed) {
                return std::unexpected(printed.error().message);
            }
            for (const std::string& out : *printed) {
                context.print(out);
            }
            return {};
        });
}

} // namespace coney::debug
