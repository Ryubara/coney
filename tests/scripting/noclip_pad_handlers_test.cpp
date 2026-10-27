// SPDX-License-Identifier: GPL-3.0-or-later
// Debug flight owns one player's buttons without silencing another player's script handlers.
#include <cstddef>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gamemodes/player_frame.h"
#include "scripting/binding_args.h"
#include "scripting/lua_vm.h"

TEST_CASE("noclip suppresses only its port's Lua pad handlers", "[player_bindings][noclip][debug]") {
    // Expected: unverified, #243 (Coney-only debug input isolation, synthetic callbacks).
    coney::GameState state;
    coney::script::ScriptSystem scripts(
        [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
            return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
        },
        [](coney::script::ScriptSystem&, coney::script::LuaVm&) {}, {});
    scripts.create();
    int first = 0;
    int second = 0;
    scripts.vm().registerFunction("First",
                                  [&](std::span<const coney::script::Value>) -> coney::script::binding::Results {
                                      ++first;
                                      return std::vector<coney::script::Value>{};
                                  });
    scripts.vm().registerFunction("Second",
                                  [&](std::span<const coney::script::Value>) -> coney::script::binding::Results {
                                      ++second;
                                      return std::vector<coney::script::Value>{};
                                  });
    state.player.pads.set(coney::Pads::recordOfPort(0), coney::pad::kR1, "First");
    state.player.pads.set(coney::Pads::recordOfPort(1), coney::pad::kR1, "Second");
    coney::PortSamples samples{};
    for (auto& sample : samples) {
        sample.connected = true;
        sample.buttons = coney::pad::kR1;
    }
    coney::Pads pads;
    pads.update(samples);
    coney::runPlayerFrame(state, scripts, pads.withoutPort(0), 0);
    CHECK(first == 0);
    CHECK(second == 1);
    CHECK(pads.port(0).pressed() == coney::pad::kR1);

    // Turning the tool off restores the same registered handler on the next press.
    for (auto& sample : samples) {
        sample.buttons = 0;
    }
    pads.update(samples);
    for (auto& sample : samples) {
        sample.buttons = coney::pad::kR1;
    }
    pads.update(samples);
    coney::runPlayerFrame(state, scripts, pads, 0);
    CHECK(first == 1);
    CHECK(second == 2);
}
