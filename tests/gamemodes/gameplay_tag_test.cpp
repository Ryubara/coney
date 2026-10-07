// SPDX-License-Identifier: GPL-3.0-or-later
// Gameplay's tagging as the scripts see it (docs/research/crimes.md#tag-callbacks): HuTag for player 1 with paint
// starts the stick game and calls CfgTagStartCallback's function at once with (tagger, tag, flag); without paint the
// player himself gets event 14 with the tag and 0; the stick game's finish gives him event 14 with the tag and 1.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/game_timer.h"
#include "core/pad.h"
#include "core/pads.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "scripting/binding_args.h"
#include "scripting/lua_value.h"
#include "scripting/message_handlers.h"
#include "scripting/script_system.h"
#include "support/lua_fixtures.h"
#include "support/recording_device.h"
#include "warriors/tag_game.h"

using coney::script::Value;

namespace {

// The scripts: empty preloads, level1 listed, and level1.lua making Rembrandt player 1 at (1, 2, 0).
std::map<std::string, std::vector<std::byte>, std::less<>> tagScripts() {
    using coney::test::LuaAsm;
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    for (const char* empty : {"enum_preload.lua", "config_preload.lua", "config_preload2.lua", "global.lua"}) {
        files[empty] = coney::test::luaChunk(LuaAsm().end());
    }
    LuaAsm levels;
    levels.getGlobal("CfgLevelName").pushInt(1).pushString("level1").pushString("").pushString("level1");
    levels.pushString("").pushInt(1).call(6);
    files["config_preload3.lua"] = coney::test::luaChunk(levels.end());
    LuaAsm level;
    level.newTable().setGlobal("pos");
    level.getGlobal("pos").pushInt(1).pushInt(1).setTable();
    level.getGlobal("pos").pushInt(2).pushInt(2).setTable();
    level.getGlobal("pos").pushInt(3).pushInt(0).setTable();
    level.getGlobal("HuCreate").pushString("Rembrandt").pushInt(32).getGlobal("pos").pushInt(90).pushString("warr_sw");
    level.pushInt(1).call(6);
    files["level1.lua"] = coney::test::luaChunk(level.end());
    return files;
}

// A loaded level that does nothing.
class EmptyLevel final : public coney::GameMode {
  public:
    [[nodiscard]] std::uint32_t id() const override { return 0x1ff; }
    coney::ModeResult update(coney::GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        return coney::ModeResult::Stay;
    }
};

// Pad 1 with the left stick the test sets, in [-1, 1] with y up.
class StickPad final : public coney::InputSource {
  public:
    float x = 0.0F;
    float y = 0.0F;

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        samples[0].connected = true;
        samples[0].sticks[2] = raw(x);
        samples[0].sticks[3] = raw(-y);
        return samples;
    }

  private:
    // The raw byte libpad gives for `value` (pad::stickValue()'s inverse outside its dead zone).
    static std::uint8_t raw(float value) {
        constexpr float kSpan = 95.0F;
        if (value > 0.0F) {
            return static_cast<std::uint8_t>(std::lround(160.0F + (std::min(value, 1.0F) * kSpan)));
        }
        if (value < 0.0F) {
            return static_cast<std::uint8_t>(std::lround(95.0F + (std::max(value, -1.0F) * kSpan)));
        }
        return coney::pad::kStickCentre;
    }
};

// Gameplay entered on level1 by a stack over a stick pad, with natives `TagStarted` and `OnTagEnd` recording their
// arguments, the start callback named and the player's event 14 handled.
struct TagRun {
    std::map<std::string, std::vector<std::byte>, std::less<>> files = tagScripts();
    coney::LevelScripts scripts;
    coney::test::RecordingDevice device;
    std::unique_ptr<coney::GameplayMode> gameplay;
    std::vector<std::vector<double>> started; // each TagStarted call's arguments
    std::vector<std::vector<double>> ended;   // each OnTagEnd call's arguments
    bool sessionAtStart = false;              // whether the stick game was live when TagStarted ran
    double player = 0;
    StickPad pad;
    coney::GameModeStack stack;
    coney::GameTimer timer;

    TagRun()
        : scripts(
              [this](std::string_view name) -> std::expected<std::vector<std::byte>, coney::Error> {
                  const auto found = files.find(name);
                  if (found == files.end()) {
                      return coney::fail(coney::ErrorCode::NotFound, "no such script");
                  }
                  return found->second;
              },
              "level1", 1, [](std::string_view) {}) {
        gameplay = std::make_unique<coney::GameplayMode>(
            device, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(), scripts.flags(),
            scripts.recorded(),
            [](const coney::LevelStart& /*start*/,
               const coney::ScriptedCast& /*cast*/) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
                return std::make_unique<EmptyLevel>();
            },
            [](std::string_view) {});
        gameplay->setLevel("level1");
        // The stack enters gameplay on its first frame.
        stack.setInput(&pad);
        stack.push(*gameplay);
        timer.setFixedStep(true);
        frames(1);
        const coney::HumanCreation* created = scripts.humans().player(1);
        REQUIRE(created != nullptr);
        player = created->handle;
        coney::script::LuaVm& vm = scripts.scripts().vm();
        vm.registerFunction("TagStarted", [this](std::span<const Value> args) -> coney::script::binding::Results {
            started.push_back(numbers(args));
            sessionAtStart = gameplay->tagSession() != nullptr;
            return std::vector<Value>{};
        });
        vm.registerFunction("OnTagEnd", [this](std::span<const Value> args) -> coney::script::binding::Results {
            ended.push_back(numbers(args));
            return std::vector<Value>{};
        });
        call("CfgTagStartCallback", {Value(std::string("TagStarted"))});
        REQUIRE(scripts.context().messages != nullptr);
        scripts.context().messages->set(player, 0xe, "OnTagEnd");
        // A three-point pattern along the grid's x.
        scripts.state().story.tagPattern = {};
        const std::vector<float> points{20.0F, 20.0F, 120.0F, 20.0F, 220.0F, 20.0F};
        std::ranges::copy(points, scripts.state().story.tagPattern.begin());
        scripts.state().story.tagPatternCount = 3;
    }

    // Runs `count` frames of play.
    void frames(std::uint64_t count) { stack.runUntilEmpty(timer, {}, count); }

    // Calls the binding `name` with `args`.
    void call(const std::string& name, std::vector<Value> args) {
        coney::script::LuaVm& vm = scripts.scripts().vm();
        REQUIRE(vm.call(vm.global(name), args).has_value());
    }

    // The arguments as numbers (-1 for anything else).
    static std::vector<double> numbers(std::span<const Value> args) {
        std::vector<double> out;
        for (const Value& arg : args) {
            out.push_back(arg.number().value_or(-1.0));
        }
        return out;
    }
};

constexpr double kTag = 500.0;
constexpr double kFlag = 600.0;

} // namespace

TEST_CASE("HuTag without paint gives the player event 14 unfinished and calls no start callback", "[gameplay][tag]") {
    TagRun run;
    run.scripts.state().player.inventory.setSprayPaint(0, 0);
    run.call("HuTag", {Value(run.player), Value(kTag), Value(kFlag)});
    CHECK(run.gameplay->tagSession() == nullptr);
    CHECK(run.started.empty());
    REQUIRE(run.ended.size() == 1);
    CHECK(run.ended[0] == std::vector<double>{run.player, kTag, 0.0});
}

TEST_CASE("HuTag with paint starts the stick game, calls the start callback with (tagger, tag, flag) and reports the "
          "finish as event 14",
          "[gameplay][tag]") {
    TagRun run;
    run.scripts.state().player.inventory.setSprayPaint(0, 3);
    run.call("HuTag", {Value(run.player), Value(kTag), Value(kFlag)});
    // Once, at once, with the stick game already live.
    REQUIRE(run.gameplay->tagSession() != nullptr);
    REQUIRE(run.started.size() == 1);
    CHECK(run.started[0] == std::vector<double>{run.player, kTag, kFlag});
    CHECK(run.sessionAtStart);
    CHECK(run.ended.empty());

    // The left stick traces the pattern a few points ahead of the progress until the game ends.
    constexpr int kMaxFrames = 30 * 60;
    constexpr std::size_t kLead = 3;
    for (int frame = 0; frame < kMaxFrames && run.gameplay->tagSession() != nullptr; ++frame) {
        const coney::TagGame& game = run.gameplay->tagSession()->game();
        const coney::TagCell target = game.path()[std::min(game.progress() + kLead, game.path().size() - 1)];
        const float dx = static_cast<float>(target.x) - game.cursorX();
        const float dy = static_cast<float>(target.y) - game.cursorY();
        const float length = std::hypot(dx, dy);
        run.pad.x = length > 0.5F ? dx / length : 0.0F;
        run.pad.y = length > 0.5F ? dy / length : 0.0F;
        run.frames(1);
    }
    CHECK(run.gameplay->tagSession() == nullptr);
    REQUIRE(run.ended.size() == 1);
    CHECK(run.ended[0] == std::vector<double>{run.player, kTag, 1.0});
    CHECK(run.started.size() == 1);
    const coney::world_objects::TagSpot* spot = run.gameplay->tagSpots().find(kTag);
    REQUIRE(spot != nullptr);
    CHECK(spot->fraction == 1.0F);
}
