// SPDX-License-Identifier: GPL-3.0-or-later
// The AI bindings (docs/references/bindings/ai.md): GoalMoveToFlag's and ActLookAt's arguments as tolua reads them,
// handed to the context's host; with no host they do nothing.
#include "scripting/ai_bindings.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A host that ignores every request: these bindings ask nothing of it.
class QuietHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// An AI host that keeps the calls it gets.
class KeepingAi final : public coney::script::AiBindingHost {
  public:
    void goalMoveToFlag(const coney::script::MoveToFlagCall& call) override { moves.push_back(call); }
    void actLookAt(const coney::script::LookAtCall& call) override { looks.push_back(call); }
    std::vector<coney::script::MoveToFlagCall> moves;
    std::vector<coney::script::LookAtCall> looks;
};

// A script system with Coney's bindings and `ai` as the AI host (null for none).
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::BindingContext context;
    ScriptSystem scripts;

    explicit Harness(coney::script::AiBindingHost* ai)
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.ai = ai;
        scripts.create();
    }

    // Calls the binding `name` with `args`; REQUIREs success and that it returns nothing.
    void call(std::string_view name, const std::vector<Value>& args) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        CHECK((!result.has_value() || result->empty()));
    }
};

} // namespace

TEST_CASE("GoalMoveToFlag hands its nine arguments to the AI host as tolua reads them", "[scripting][ai]") {
    KeepingAi ai;
    Harness harness(&ai);
    harness.call("GoalMoveToFlag", {Value(12.9), Value(40.0), Value(4.7), Value(90.0), Value(2.0), Value(0.5),
                                    Value(1500.0), Value(1.0), Value()});
    REQUIRE(ai.moves.size() == 1);
    const coney::script::MoveToFlagCall& call = ai.moves[0];
    CHECK(call.human == 12.0);
    CHECK(call.flag == 40.0);
    CHECK(call.gait == 4);
    CHECK(call.angle == 90.0F);
    CHECK(call.distance == 2.0F);
    CHECK(call.radius == 0.5F);
    CHECK(call.intervalMs == 1500);
    CHECK(call.faceFlag);
    CHECK_FALSE(call.option);
}

TEST_CASE("ActLookAt's start delay defaults to -1, a random 0-500 ms", "[scripting][ai]") {
    KeepingAi ai;
    Harness harness(&ai);
    harness.call("ActLookAt", {Value(3.0), Value(7.0), Value(0.25)});
    harness.call("ActLookAt", {Value(3.0), Value(7.0), Value(0.25), Value(300.0)});
    REQUIRE(ai.looks.size() == 2);
    CHECK(ai.looks[0].human == 3.0);
    CHECK(ai.looks[0].target == 7.0);
    CHECK(ai.looks[0].turn == 0.25F);
    CHECK(ai.looks[0].delayMs == -1);
    CHECK(ai.looks[1].delayMs == 300);
}

TEST_CASE("the AI bindings are real and do nothing without a host", "[scripting][ai]") {
    for (const std::string_view name : coney::script::kAiBindings) {
        const auto& table = coney::script::bindingTable();
        const auto info = std::ranges::find(table, name, &coney::script::BindingInfo::name);
        REQUIRE(info != table.end());
        CHECK(info->kind == coney::script::BindingKind::Real);
    }
    Harness harness(nullptr);
    harness.call("GoalMoveToFlag", {Value(1.0), Value(2.0)});
    harness.call("ActLookAt", {Value(1.0), Value(2.0)});
}
