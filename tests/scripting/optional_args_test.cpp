// SPDX-License-Identifier: GPL-3.0-or-later
// Optional binding arguments read as tolua reads them: the default only when the script leaves the argument out, 0
// for an explicit nil (docs/research/script-arguments.md). One case per binding the orig sweep found
// reading nil as missing (#686). Synthetic calls.
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "hud/hud.h"
#include "scripting/ai_bindings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/trigger_spheres.h"

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

// One turn-to-heading request as the AI host got it.
struct TurnToDir {
    double human = 0;
    float heading = 0.0F;
    std::int16_t delayMs = 0;
};

// One follow-slot count request as the AI host got it.
struct FollowSlots {
    double leader = 0;
    int count = 0;
    int allowed = 0;
};

// One domination tactic as the AI host got it.
struct Domination {
    int gang = 0;
    double flag = 0;
    float range = 0.0F;
    std::string callback;
};

// An AI host that keeps the calls these tests look at.
class KeepingAi final : public coney::script::AiBindingHost {
  public:
    void goalMoveToFlag(const coney::script::MoveToFlagCall& /*call*/) override {}
    void actLookAt(const coney::script::LookAtCall& call) override { looks.push_back(call); }
    void actTurnToDir(double human, float heading, std::int16_t delayMs) override {
        turns.push_back(TurnToDir{human, heading, delayMs});
    }
    void goalAddressPerson(const coney::script::AddressPersonCall& call) override { addresses.push_back(call); }
    void goalDealer(const coney::script::DealerCall& call) override { dealers.push_back(call); }
    void brSetNumFollowSlots(double leader, int count, int allowed) override {
        slots.push_back(FollowSlots{leader, count, allowed});
    }
    void tacticDomination(int gang, double flag, float range, std::string_view callback) override {
        dominations.push_back(Domination{gang, flag, range, std::string(callback)});
    }
    std::vector<coney::script::LookAtCall> looks;
    std::vector<TurnToDir> turns;
    std::vector<coney::script::AddressPersonCall> addresses;
    std::vector<coney::script::DealerCall> dealers;
    std::vector<FollowSlots> slots;
    std::vector<Domination> dominations;
};

// A script system with Coney's bindings over an AI host, a HUD and the trigger spheres.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    KeepingAi ai;
    coney::hud::Hud hud;
    coney::world_objects::TriggerSpheres spheres;
    coney::script::BindingContext context{&state, &strings, &host, &recorded};
    ScriptSystem scripts;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.ai = &ai;
        context.hud = &hud;
        context.spheres = &spheres;
        scripts.create();
    }

    // Calls the binding `name` with `args`. REQUIREs success.
    void call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
    }
};

} // namespace

TEST_CASE("GoalAddressPerson reads an explicit nil speech as 0, and -1 only when it is left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("GoalAddressPerson", {Value(4.0), Value(1.0), Value(2.0), Value(5.0), Value(), Value("Done")});
    h.call("GoalAddressPerson", {Value(4.0), Value(1.0), Value(2.0), Value(5.0)});
    REQUIRE(h.ai.addresses.size() == 2);
    CHECK(h.ai.addresses[0].speech == 0);
    CHECK(h.ai.addresses[0].callback == "Done");
    CHECK(h.ai.addresses[1].speech == -1);
}

TEST_CASE("GoalDealer reads an explicit nil range and run chance as 0, and 10 and 50 only when left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("GoalDealer", {Value(6.0), Value(1.0), Value(), Value(), Value(20.0), Value()});
    h.call("GoalDealer", {Value(6.0), Value(1.0)});
    REQUIRE(h.ai.dealers.size() == 2);
    CHECK(h.ai.dealers[0].range == 0.0F);
    CHECK(h.ai.dealers[0].runChance == 0);
    CHECK(h.ai.dealers[0].dirtyChance == 20);
    CHECK_FALSE(h.ai.dealers[0].option);
    CHECK(h.ai.dealers[1].range == 10.0F);
    CHECK(h.ai.dealers[1].runChance == 50);
    CHECK(h.ai.dealers[1].option);
}

TEST_CASE("BrSetNumFollowSlots reads an explicit nil second count as 0, and -1 only when left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("BrSetNumFollowSlots", {Value(3.0), Value(4.0), Value()});
    h.call("BrSetNumFollowSlots", {Value(3.0), Value(4.0)});
    REQUIRE(h.ai.slots.size() == 2);
    CHECK(h.ai.slots[0].count == 4);
    CHECK(h.ai.slots[0].allowed == 0);
    CHECK(h.ai.slots[1].allowed == -1);
}

TEST_CASE("ActLookAt reads an explicit nil delay as 0, and -1 only when left out", "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("ActLookAt", {Value(4.0), Value(9.0), Value(0.5), Value()});
    h.call("ActLookAt", {Value(4.0), Value(9.0), Value(0.5)});
    REQUIRE(h.ai.looks.size() == 2);
    CHECK(h.ai.looks[0].delayMs == 0);
    CHECK(h.ai.looks[1].delayMs == -1);
}

TEST_CASE("ActTurnToDir reads an explicit nil delay as 0, and -1 only when left out", "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("ActTurnToDir", {Value(4.0), Value(90.0), Value(1.0), Value()});
    h.call("ActTurnToDir", {Value(4.0), Value(90.0), Value(1.0)});
    REQUIRE(h.ai.turns.size() == 2);
    CHECK(h.ai.turns[0].heading == 90.0F);
    CHECK(h.ai.turns[0].delayMs == 0);
    CHECK(h.ai.turns[1].delayMs == -1);
}

TEST_CASE("TacticDomination reads an explicit nil range as 0, and 3 only when left out", "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("TacticDomination", {Value(2.0), Value(30.0), Value(), Value("Held")});
    h.call("TacticDomination", {Value(2.0), Value(30.0)});
    REQUIRE(h.ai.dominations.size() == 2);
    CHECK(h.ai.dominations[0].range == 0.0F);
    CHECK(h.ai.dominations[0].callback == "Held");
    CHECK(h.ai.dominations[1].range == 3.0F);
    CHECK(h.ai.dominations[1].callback.empty());
}

TEST_CASE("HUDSetObjective reads an explicit nil time as 0 ms, and 8000 only when left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("HUDSetObjective", {Value(0.0), Value("Follow Ash"), Value(0.0), Value(), Value()});
    h.call("HUDSetObjective", {Value(1.0), Value("Win"), Value(0.0)});
    REQUIRE(h.hud.scrollIn().messages().size() == 2);
    CHECK(h.hud.scrollIn().messages()[0].ms == 0);
    CHECK(h.hud.scrollIn().messages()[1].ms == 8000);
}

TEST_CASE("HUDFlushTutorialText reads an explicit nil priority as 0, and all only when left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("HUDSetTutorialText", {Value("urgent"), Value(0.0)});
    h.call("HUDSetTutorialText", {Value("later"), Value(2.0)});
    h.call("HUDFlushTutorialText", {Value()});
    CHECK_FALSE(h.hud.hints().contains("urgent"));
    CHECK(h.hud.hints().contains("later"));
    h.call("HUDFlushTutorialText");
    CHECK_FALSE(h.hud.hints().contains("later"));
}

TEST_CASE("HUDTurnOnRadar reads an explicit nil player as 0, and both only when left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("HUDTurnOffRadar", {Value(2.0)});
    h.call("HUDTurnOnRadar", {Value()});
    CHECK(h.hud.radar().on[0]);
    CHECK_FALSE(h.hud.radar().on[1]);
    h.call("HUDTurnOnRadar");
    CHECK(h.hud.radar().on[1]);
}

TEST_CASE("HUDTurnOffRadar reads an explicit nil player as 0, and both only when left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("HUDTurnOnRadar", {Value(2.0)});
    h.call("HUDTurnOffRadar", {Value()});
    CHECK_FALSE(h.hud.radar().on[0]);
    CHECK(h.hud.radar().on[1]);
    h.call("HUDTurnOffRadar");
    CHECK_FALSE(h.hud.radar().on[1]);
}

TEST_CASE("HUDSetNumIndicator reads an explicit nil gang as 0, and -1 only when left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("HUDSetNumIndicator", {Value(2.0), Value(1.0), Value()});
    CHECK(h.hud.numIndicator(2).gang == 0);
    CHECK(h.hud.numIndicator(2).on);
    h.call("HUDSetNumIndicator", {Value(2.0), Value(1.0)});
    CHECK(h.hud.numIndicator(2).gang == -1);
    CHECK_FALSE(h.hud.numIndicator(2).on);
}

TEST_CASE("HUDSetRadarZoomScale with no argument sets the scale to 0", "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("HUDSetRadarZoomScale", {Value(0.5)});
    CHECK(h.hud.radar().zoomScale == 0.5F);
    h.call("HUDSetRadarZoomScale");
    CHECK(h.hud.radar().zoomScale == 0.0F);
}

TEST_CASE("TriggerSphereCfg leaves the sphere unarmed when the enable argument is left out",
          "[scripting][optional_args]") {
    // Expected: script-arguments.md#readers, confirmed (code)
    Harness h;
    h.call("TriggerSphereCfg", {Value(12.0)});
    const coney::world_objects::TriggerSphere* sphere = h.spheres.find(12);
    REQUIRE(sphere != nullptr);
    CHECK_FALSE(sphere->armed);
}
