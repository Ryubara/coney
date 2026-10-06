// SPDX-License-Identifier: GPL-3.0-or-later
// The AI and gang bindings (docs/references/bindings/ai.md, gang.md): their arguments as tolua reads them, handed to
// the context's host; with no host they do nothing, and GangCreate gives out the stubs' handles.
#include "scripting/ai_bindings.h"
#include "scripting/gang_bindings.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <memory>
#include <string>
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
    void goalFight(double human, double target) override { log.push_back(std::format("fight {} {}", human, target)); }
    void brDead(double human, bool dead) override { log.push_back(std::format("dead {} {}", human, dead)); }
    void goalPlayDynAnimation(const coney::script::DynAnimationCall& call) override { dyns.push_back(call); }
    void goalAddressPerson(const coney::script::AddressPersonCall& call) override { addresses.push_back(call); }
    void goalDealer(const coney::script::DealerCall& call) override { dealers.push_back(call); }
    void brSetFollowSlot(double leader, int slot, float x, float y, int set) override {
        log.push_back(std::format("slot {} {} {} {} {}", leader, slot, x, y, set));
    }
    void tacticTrigger(int gang, int what, bool on) override {
        log.push_back(std::format("trigger {} {} {}", gang, what, on));
    }
    [[nodiscard]] int gangCreate(int kind, std::string_view name) override {
        log.push_back(std::format("create {} {}", kind, name));
        return 5;
    }
    void gangAddMember(int gang, double human) override { log.push_back(std::format("add {} {}", gang, human)); }
    void gangSetMsgHandler(int gang, int message, std::string_view handler) override {
        log.push_back(std::format("handler {} {} '{}'", gang, message, handler));
    }
    [[nodiscard]] int gangHeadCount(int gang, bool living) override { return gang * 10 + (living ? 1 : 0); }
    [[nodiscard]] int gangStandingCount(int gang) override { return gang + 1; }
    std::vector<coney::script::MoveToFlagCall> moves;
    std::vector<coney::script::LookAtCall> looks;
    std::vector<coney::script::DynAnimationCall> dyns;
    std::vector<coney::script::AddressPersonCall> addresses;
    std::vector<coney::script::DealerCall> dealers;
    std::vector<std::string> log;
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

    // Calls the binding `name` with `args`; REQUIREs success and returns its first result (nil for none).
    Value value(std::string_view name, const std::vector<Value>& args) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
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

TEST_CASE("the brain and goal bindings read their arguments and defaults as tolua does", "[scripting][ai]") {
    KeepingAi ai;
    Harness harness(&ai);
    harness.call("GoalFight", {Value(4.0), Value(1.0), Value(0.0)});
    harness.call("BrDead", {Value(4.0)});
    harness.call("BrDead", {Value(4.0), Value(0.0)});
    harness.call("GoalPlayDynAnimation", {Value(4.0), Value(std::string("bow")), Value()});
    harness.call("GoalPlayDynAnimation",
                 {Value(4.0), Value(std::string("bow")), Value(std::string("P1.Done")), Value(0.0)});
    harness.call("GoalAddressPerson", {Value(4.0), Value(1.0), Value(2.0), Value(5.0)});
    harness.call("GoalDealer", {Value(6.0), Value(0.0), Value(17.0), Value(0.0), Value(0.0), Value(0.0)});
    harness.call("GoalDealer", {Value(6.0), Value(2.0)});
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(1.5)).has_value());
    REQUIRE(table->set(Value(2.0), Value(-2.0)).has_value());
    harness.call("BrSetFollowSlot", {Value(1.0), Value(2.0), Value(table), Value(1.0)});
    harness.call("TacticTrigger", {Value(3.0), Value(1.0), Value(1.0)});

    CHECK(ai.log == std::vector<std::string>{"fight 4 1", "dead 4 true", "dead 4 false", "slot 1 2 1.5 -2 1",
                                             "trigger 3 1 true"});
    REQUIRE(ai.dyns.size() == 2);
    CHECK(ai.dyns[0].anim == "bow");
    CHECK(ai.dyns[0].callback.empty());
    CHECK(ai.dyns[0].option);
    CHECK(ai.dyns[1].callback == "P1.Done");
    CHECK_FALSE(ai.dyns[1].option);
    REQUIRE(ai.addresses.size() == 1);
    CHECK(ai.addresses[0].target == 1.0);
    CHECK(ai.addresses[0].approach == 2.0F);
    CHECK(ai.addresses[0].range == 5.0F);
    CHECK(ai.addresses[0].speech == -1);
    REQUIRE(ai.dealers.size() == 2);
    CHECK(ai.dealers[0].range == 17.0F);
    CHECK(ai.dealers[0].runChance == 0);
    CHECK_FALSE(ai.dealers[0].option);
    CHECK(ai.dealers[1].type == 2);
    CHECK(ai.dealers[1].range == 10.0F);
    CHECK(ai.dealers[1].runChance == 50);
    CHECK(ai.dealers[1].option);
}

TEST_CASE("the gang bindings hand their calls to the host and return its counts", "[scripting][ai]") {
    KeepingAi ai;
    Harness harness(&ai);
    CHECK(harness.value("GangCreate", {Value(19.0), Value(std::string("CombatEnemy"))}) == Value(5.0));
    harness.call("GangAddMember", {Value(5.0), Value(0.0), Value(12.0)});
    harness.call("GangSetMsgHandler", {Value(5.0), Value(18.0), Value(std::string("P1.BumDied"))});
    harness.call("GangSetMsgHandler", {Value(5.0), Value(18.0), Value()});
    CHECK(harness.value("GangGetHeadCount", {Value(5.0)}) == Value(50.0));
    CHECK(harness.value("GangGetHeadCount", {Value(5.0), Value(1.0)}) == Value(51.0));
    CHECK(harness.value("GangGetStandingCount", {Value(5.0)}) == Value(6.0));
    CHECK(ai.log == std::vector<std::string>{"create 19 CombatEnemy", "add 5 12", "handler 5 18 'P1.BumDied'",
                                             "handler 5 18 ''"});
}

TEST_CASE("the gang bindings are real; without a host GangCreate gives the stubs' handles and the counts are 0",
          "[scripting][ai]") {
    const auto& table = coney::script::bindingTable();
    for (const std::string_view name : coney::script::kGangBindings) {
        const auto info = std::ranges::find(table, name, &coney::script::BindingInfo::name);
        REQUIRE(info != table.end());
        CHECK(info->kind == coney::script::BindingKind::Real);
    }
    Harness harness(nullptr);
    CHECK(harness.value("GangCreate", {Value(0.0), Value(std::string("Warriors"))}) == Value(1.0));
    CHECK(harness.value("GangCreate", {Value(19.0), Value(std::string("Enemy"))}) == Value(2.0));
    CHECK(harness.value("GangGetHeadCount", {Value(1.0)}) == Value(0.0));
    harness.call("GangBrDead", {Value(1.0), Value(1.0)});
}
