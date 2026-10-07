// SPDX-License-Identifier: GPL-3.0-or-later
// The scene bindings (docs/references/bindings/scene.md): global.lua's SuperRunScene path, as its three helpers call
// them, plays a synthetic scene through the scene system and calls back into Lua
// (docs/research/scenes.md#superrunscene).
#include "scripting/scene_bindings.h"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gui/global_strings.h"
#include "scenes/scene_player.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/scene_fixtures.h"
#include "warriors/game_state.h"
#include "world_objects/spawn_records.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;
namespace scenes = coney::scenes;

namespace {

// A host that ignores every request.
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

// A scene host that keeps who joined and was released.
class JoinHost final : public scenes::SceneHost {
  public:
    std::vector<std::string> log;
    void humanJoin(double human, std::uint32_t scene, std::size_t role, const scenes::ScenePose& /*start*/,
                   int gait) override {
        log.push_back(std::format("join {} {} {} {}", human, scene, role, gait));
    }
    void humanRelease(double human, const std::optional<scenes::ScenePose>& /*endPose*/) override {
        log.push_back(std::format("release {}", human));
    }
    void suspendBrains(bool suspended) override { log.push_back(std::format("brains {}", suspended)); }
    void screenEffect(scenes::ScreenEffect type, float seconds) override {
        log.push_back(std::format("screen {} {}", static_cast<int>(type), seconds));
    }
};

// A script system with Coney's bindings and a scene system over one synthetic scene, `tst_c1`, and its segment.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    JoinHost sceneHost;
    scenes::SceneList list{
        std::vector<scenes::SceneListEntry>{{0, 0, "tst_first"}, {1, 0, "tst_c1"}, {2, 0, "tst_c1aa"}}};
    scenes::SceneSystem system;
    coney::script::BindingContext context;
    ScriptSystem scripts;
    std::vector<std::string> calls; // the Lua functions the scenes called back

    Harness()
        : system(
              list,
              [](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
                  if (name == "tst_c1") {
                      return coney::test::sceneHeaderRecord(coney::test::twoPartSpec()).data();
                  }
                  return coney::test::sceneSegmentRecord("tst_c1aa", "", coney::test::segmentPart()).data();
              },
              [this](std::string_view function, std::span<const double> args) {
                  std::vector<Value> values;
                  for (const double arg : args) {
                      values.emplace_back(arg);
                  }
                  scripts.call(function, values);
              }),
          scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& script, LuaVm& vm) {
                  coney::script::installBindings(script, vm, context);
                  // Stand-ins for global.lua's helpers: each writes down its call.
                  for (const std::string_view name : {"gPlayCutScene", "PreCashTheWorld"}) {
                      vm.registerFunction(name, [this, name](std::span<const Value> args) {
                          calls.push_back(std::format("{}({})", name, args.empty() ? -1.0 : *args[0].number()));
                          return coney::script::binding::none();
                      });
                  }
              },
              {}) {
        context.state = &state;
        context.strings = &strings;
        context.host = &host;
        context.scenes = &system;
        system.setHost(&sceneHost);
        scripts.create();
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns its first result (nil for none).
    Value value(std::string_view name, const std::vector<Value>& args) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
    }
};

} // namespace

TEST_CASE("SuperRunScene's bindings preload, join, play and end a scene with callbacks", "[scripting][scenes]") {
    Harness h;
    // SuperRunScene: ScenePreload(t.SceneId, "gPlayCutScene").
    const Value id = h.value("ScenePreload", {Value("tst_c1"), Value("gPlayCutScene")});
    REQUIRE(id.number() == 1.0);
    CHECK(h.value("SceneIsPreloaded", {Value("tst_c1")}).isNil());
    h.system.update(33, 0);
    CHECK(h.calls == std::vector<std::string>{"gPlayCutScene(1)"});
    CHECK(h.value("SceneIsPreloaded", {Value("tst_c1")}).number() == 1.0);
    CHECK(h.value("SceneLength", {id}).number() == 1000.0);

    // gPlayCutScene: each human joins its role, then ScenePlayCinematic with the ten arguments global.lua passes
    // (Looping, Freeze, Final and Chain nil).
    h.value("GoalJoinCinematic", {Value(12.0), id, Value(0.0), Value(0.0), Value(1.0)});
    h.value("GoalJoinCinematic", {Value(13.0), id, Value(1.0), Value(0.0), Value(1.0)});
    h.value("SceneAddObject", {id, Value(20.0), Value(0.0)});
    CHECK(h.sceneHost.log == std::vector<std::string>{"join 12 1 0 0", "join 13 1 1 0"});
    const Value started = h.value("ScenePlayCinematic", {id, Value(0.0), Value("PreCashTheWorld"), Value(1.0),
                                                         Value(1.0), Value(), Value(), Value(-1.0), Value(), Value()});
    CHECK(started.number() == 1.0);
    CHECK(h.value("SceneDone", {id}).isNil());

    // The scene plays its 45 frames after its four starting updates; then PreCashTheWorld(id).
    std::uint64_t now = 33;
    for (int i = 0; i < 60 && h.calls.size() < 2; ++i) {
        now += 33;
        h.system.update(now, 0);
    }
    CHECK(h.calls == std::vector<std::string>{"gPlayCutScene(1)", "PreCashTheWorld(1)"});
    CHECK(h.value("SceneDone", {id}).number() == 1.0);
    CHECK(std::ranges::find(h.sceneHost.log, "release 13") != h.sceneHost.log.end());
    CHECK(h.system.stats().ended == 1);
}

TEST_CASE("the scene bindings follow the context's scene system at each call, the stand-in without one",
          "[scripting][scenes]") {
    Harness h;
    // No scene system: the stand-in's preload gives a handle of its own and calls back at the scripts' next update.
    h.context.scenes = nullptr;
    const double standIn = h.value("ScenePreload", {Value("tst_c1"), Value("gPlayCutScene")}).number().value_or(0.0);
    CHECK(standIn >= 1.0);
    CHECK(h.value("SceneLength", {Value(standIn)}).isNil());
    h.scripts.update(10, 1.0 / 30.0);
    CHECK(h.calls == std::vector<std::string>{std::format("gPlayCutScene({})", standIn)});
    CHECK(h.system.stats().preloads == 0);

    // The scene system back (as gameplay sets it for a level): the same bindings now load the scene.
    h.context.scenes = &h.system;
    CHECK(h.value("ScenePreload", {Value("tst_c1"), Value()}).number() == 1.0);
    CHECK(h.system.stats().preloads == 1);
}

TEST_CASE("ScreenQueueEffect reaches the scenes' host, which owns the screen's effects in play",
          "[scripting][scenes]") {
    Harness h;
    h.value("ScreenQueueEffect", {Value(0.0), Value(0.5)});
    h.value("ScreenQueueEffect", {Value(2.0), Value(1.0)});
    h.value("ScreenQueueEffect", {Value(4.0), Value(1.0)}); // not one a host is given
    CHECK(h.sceneHost.log == std::vector<std::string>{"screen 0 0.5", "screen 2 1"});
}

TEST_CASE("a host attached late is told of the humans already joined", "[scripting][scenes]") {
    Harness h;
    // A level's start callback binds the roles before the level, the scenes' host, exists.
    h.system.setHost(nullptr);
    const Value id = h.value("ScenePreload", {Value("tst_c1"), Value()});
    h.system.update(33, 0);
    h.value("GoalJoinCinematic", {Value(12.0), id, Value(1.0), Value(0.0), Value(1.0)});
    CHECK(h.sceneHost.log.empty());
    h.system.setHost(&h.sceneHost);
    CHECK(h.sceneHost.log == std::vector<std::string>{"join 12 1 1 0"});
}

TEST_CASE("ScenePlayCinematic's freeze is true only when the argument is absent", "[scripting][scenes]") {
    for (const bool absent : {true, false}) {
        Harness h;
        const Value id = h.value("ScenePreload", {Value("tst_c1"), Value()});
        h.system.update(33, 0);
        std::vector<Value> args{id, Value(0.0), Value(), Value(1.0), Value(1.0), Value()};
        if (!absent) {
            args.emplace_back(); // an explicit nil
        }
        CHECK(h.value("ScenePlayCinematic", args).number() == 1.0);
        for (std::uint64_t now = 66; now < 400; now += 33) {
            h.system.update(now, 0);
        }
        CHECK((std::ranges::find(h.sceneHost.log, "brains true") != h.sceneHost.log.end()) == absent);
        h.value("SceneStop", {id, Value(1.0)});
        h.system.update(500, 0);
        CHECK(h.value("SceneDone", {id}).number() == 1.0);
    }
}

TEST_CASE("SceneAddObject resolves the object's spawn record, which goes live and pinned", "[scripting][scenes]") {
    Harness h;
    coney::world_objects::SpawnRecords records;
    h.context.spawnRecords = &records;
    coney::world_objects::SpawnRecord record;
    record.handle = 20.0;
    record.typeName = "dyn_test";
    REQUIRE(records.add(record) != nullptr);
    const Value id = h.value("ScenePreload", {Value("tst_c1"), Value()});
    h.system.update(33, 0);
    CHECK_FALSE(records.find(20.0)->live);
    h.value("SceneAddObject", {id, Value(20.0), Value(0.0)});
    CHECK(records.find(20.0)->live);
    CHECK(records.find(20.0)->pinned);
    // A handle no record has is bound all the same, with nothing to resolve.
    h.value("SceneAddObject", {id, Value(21.0), Value(0.0)});
    CHECK(records.find(21.0) == nullptr);
}
