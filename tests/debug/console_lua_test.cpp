// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/console_lua.h"

#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "console/console.h"
#include "console/registry.h"
#include "debug/debug_session.h"
#include "debug/tunables.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"

// The developer set's `lua <line>` (docs/guides/studio/console.md#developer-set): a line typed or scripted into the
// console runs in the debug session's script state, as the Lua console page runs it.

using coney::console::Access;
using coney::console::Console;
using coney::console::ConsoleSettings;
using coney::console::LineSource;
using coney::console::Outcome;
using coney::debug::DebugServices;
using coney::debug::DebugSession;

TEST_CASE("the console's lua command runs a line in the script state at the step boundary", "[debug][console]") {
    coney::debug::TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    Console console(ConsoleSettings{.testMode = true, .highest = Access::Developer}, {});
    REQUIRE(coney::debug::registerLuaCommand(console.registry(), session));
    // A session command: queued, then run between two steps.
    CHECK(console.submit("lua \"SCENETEST = 40 + 2\"", LineSource::Script) == Outcome::Queued);
    CHECK(console.runQueued() == 1);
    CHECK(session.vm().global("SCENETEST").number() == std::optional<double>(42.0));
    // A syntax error is the command's failure, and nothing runs.
    CHECK(console.submit("lua \"SCENETEST = \"", LineSource::Script) == Outcome::Queued);
    console.runQueued();
    CHECK(session.vm().global("SCENETEST").number() == std::optional<double>(42.0));
}

TEST_CASE("a console that takes player commands only refuses the lua command", "[debug][console]") {
    coney::debug::TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    Console release(ConsoleSettings{.testMode = true, .highest = Access::Player}, {});
    CHECK_FALSE(coney::debug::registerLuaCommand(release.registry(), session).has_value());
    CHECK(release.submit("lua \"SCENETEST = 1\"", LineSource::Script) == Outcome::Refused);
}
