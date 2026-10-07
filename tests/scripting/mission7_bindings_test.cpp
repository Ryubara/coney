// SPDX-License-Identifier: GPL-3.0-or-later
// The seventh mission's bindings (scripting/mission7_bindings.h): the two-line exchange, the calls handed to the
// character and AI hosts, the Warrior weapons switch, a car's parts, an object's physics body and the preloaded
// sounds. Synthetic humans, cars and objects.
#include "scripting/mission7_bindings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/error.h"
#include "core/name_hash.h"
#include "effects/level_effects.h"
#include "gui/global_strings.h"
#include "hud/hud.h"
#include "scripting/ai_bindings.h"
#include "scripting/binding_args.h"
#include "scripting/human_bindings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "scripting/sound_bindings.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/cars.h"
#include "world_objects/spawn_records.h"

using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A host with no screens.
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

// A character host that keeps the rage and reachable calls and says who is tagging.
class KeepingHumans final : public coney::script::HumanBindingHost {
  public:
    void setRageMode(double human, bool on) override { calls.push_back(std::format("rage {} {}", human, on)); }
    void markReachable(double human, bool reachable) override {
        calls.push_back(std::format("reachable {} {}", human, reachable));
    }
    [[nodiscard]] bool tagging(double human) const override { return taggers.contains(human); }
    std::vector<std::string> calls;
    std::set<double> taggers;
};

// An AI host that keeps the turns it is given; its character host is KeepingHumans.
class KeepingAi final : public coney::script::AiBindingHost {
  public:
    void goalMoveToFlag(const coney::script::MoveToFlagCall& /*call*/) override {}
    void actLookAt(const coney::script::LookAtCall& /*call*/) override {}
    void actTurnTo(const coney::script::TurnToCall& call) override { turns.push_back(call); }
    coney::script::HumanBindingHost* humans() override { return &keeping; }
    std::vector<coney::script::TurnToCall> turns;
    KeepingHumans keeping;
};

// A sound host that keeps the lines and positional sounds it is asked for; every other call does nothing.
class RecordingSound final : public coney::script::SoundHost {
  public:
    std::vector<std::string> calls;
    bool speaks = true;

    void configureMusicTrack(std::uint32_t /*track*/, float /*barMs*/, float /*volume*/) override {}
    void setInterfaceSound(int /*cue*/, std::uint32_t /*sound*/) override {}
    void allocateCharacterVoices(int /*count*/) override {}
    void setCommandSoundPercent(int /*voiceSet*/, std::uint32_t /*command*/, std::uint32_t /*percent*/) override {}
    void loadSoundBank(std::string_view /*name*/) override {}
    void setNonDuckableDuck(float /*factor*/) override {}
    void setPitchFactor(float /*factor*/) override {}
    void addAmbientSound(int /*index*/, std::uint32_t /*sound*/) override {}
    double addAmbientEmitter(const coney::script::AmbientEmitterCall& /*call*/) override { return 0.0; }
    void setAmbientEmitterPositions(std::string_view /*name*/,
                                    std::span<const std::array<float, 3>> /*points*/) override {}
    void playAmbientTrack(std::uint32_t /*sound*/) override {}
    void pauseSound(bool /*on*/) override {}
    double play2D(std::uint32_t /*sound*/) override { return 0.0; }
    double play3D(std::uint32_t sound, const std::array<float, 3>& position) override {
        calls.push_back(std::format("3d {:#x} {} {} {}", sound, position[0], position[1], position[2]));
        return 9.0;
    }
    void stopAmbientTrack() override {}
    void setAmbientTrackVolume(float /*volume*/) override {}
    void playMusic(std::uint32_t /*track*/, bool /*loop*/, std::string_view /*callback*/) override {}
    void stopMusic() override {}
    void setMusicVolume(float /*volume*/) override {}
    void setListener(int /*listener*/) override {}
    bool speak(const coney::script::SpeechCall& call, std::string_view callback,
               std::optional<double> /*callbackArg*/) override {
        calls.push_back(std::format("speak {} {} {} {} {}", call.human, call.line, call.interrupt, call.lookAt,
                                    callback.empty() ? "-" : callback));
        return speaks;
    }
    void shutUp(double /*human*/, bool /*force*/) override {}
    std::optional<double> sayCommand(const coney::script::CommandCall& /*call*/,
                                     std::string_view /*callback*/) override {
        return std::nullopt;
    }
    void gameplayEntered() override {}
    void levelLoadStarted(int /*levelNumber*/) override {}
    void levelLoaded() override {}
    void gameplayLeft() override {}
};

// A script system with Coney's bindings over two created humans (handles 5 and 6), cars, spawn records, the keeping
// hosts and the recording sound; `Done()` counts the callbacks it gets.
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::SpawnRecords records;
    coney::world_objects::Cars cars;
    KeepingAi ai;
    RecordingSound sound;
    coney::script::BindingContext context{&state, &strings, &host, &recorded};
    ScriptSystem scripts;
    int done = 0;

    Harness()
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        context.humans = &humans;
        context.spawnRecords = &records;
        context.cars = &cars;
        context.ai = &ai;
        context.sound = &sound;
        for (const double handle : {5.0, 6.0}) {
            coney::HumanCreation human;
            human.handle = handle;
            REQUIRE(humans.add(human));
        }
        scripts.create();
        scripts.vm().registerFunction("Done", [this](std::span<const Value> /*args*/) {
            ++done;
            return coney::script::binding::Results{};
        });
    }

    // Calls the binding `name` with `args`; REQUIREs success and returns its results.
    std::vector<Value> call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return *result;
    }
};

// A Lua position table {x, y, z}.
Value point(float x, float y, float z) {
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(static_cast<double>(x))));
    REQUIRE(table->set(Value(2.0), Value(static_cast<double>(y))));
    REQUIRE(table->set(Value(3.0), Value(static_cast<double>(z))));
    return Value(std::move(table));
}

// A Lua string.
Value text(std::string_view s) { return Value(std::string(s)); }

} // namespace

TEST_CASE("HuActionDialog has both humans say their lines, the second with the callback", "[mission7_bindings]") {
    Harness h;
    h.call("HuActionDialog", {Value(5.0), text("a_line"), Value(6.0), text("b_line"), text("Done")});
    CHECK(h.sound.calls == std::vector<std::string>{"speak 5 a_line true 0 -", "speak 6 b_line true 0 Done"});
    // The callback waits for the second line's end.
    CHECK(h.done == 0);
}

TEST_CASE("HuActionDialog skips a bad handle's line, and the callback with the second", "[mission7_bindings]") {
    Harness h;
    h.call("HuActionDialog", {Value(99.0), text("a_line"), Value(6.0), text("b_line"), text("Done")});
    CHECK(h.sound.calls == std::vector<std::string>{"speak 6 b_line true 0 Done"});
    h.sound.calls.clear();
    h.call("HuActionDialog", {Value(5.0), text("a_line"), Value(98.0), text("b_line"), text("Done")});
    CHECK(h.sound.calls == std::vector<std::string>{"speak 5 a_line true 0 -"});
    CHECK(h.done == 0);
    // A second line that cannot play calls back at once, as HuSpeakNI does.
    h.sound.speaks = false;
    h.call("HuActionDialog", {Value(5.0), text("a_line"), Value(6.0), text("b_line"), text("Done")});
    CHECK(h.done == 1);
}

TEST_CASE("The rage, reachable and tagging calls reach the character host", "[mission7_bindings]") {
    Harness h;
    h.call("HuSetRageMode", {Value(5.0), Value(1.0)});
    h.call("HuSetRageMode", {Value(5.0)});
    h.call("HuMarkReachable", {Value(6.0), Value(1.0)});
    CHECK(h.ai.keeping.calls == std::vector<std::string>{"rage 5 true", "rage 5 false", "reachable 6 true"});
    h.ai.keeping.taggers.insert(6.0);
    CHECK_FALSE(h.call("HuIsTagging", {Value(6.0)}).front().isNil());
    CHECK(h.call("HuIsTagging", {Value(5.0)}).front().isNil());
}

TEST_CASE("ActTurnTo queues a turn to the point, its delay -1 when left out", "[mission7_bindings]") {
    Harness h;
    h.call("ActTurnTo", {Value(5.0), point(1.0F, 2.0F, 3.0F), Value(0.1)});
    h.call("ActTurnTo", {Value(5.0), point(4.0F, 5.0F, 6.0F), Value(0.1), Value(250.0)});
    // With no point nothing is queued.
    h.call("ActTurnTo", {Value(5.0)});
    REQUIRE(h.ai.turns.size() == 2);
    CHECK(h.ai.turns[0].human == 5.0);
    CHECK(h.ai.turns[0].point == std::array<float, 3>{1.0F, 2.0F, 3.0F});
    CHECK(h.ai.turns[0].delayMs == -1);
    CHECK(h.ai.turns[1].delayMs == 250);
}

TEST_CASE("CfgWarriorWeapons switches the AI Warriors' weapons, on again at a level's reset", "[mission7_bindings]") {
    Harness h;
    CHECK(h.state.story.warriorWeapons);
    h.call("CfgWarriorWeapons", {Value()});
    CHECK_FALSE(h.state.story.warriorWeapons);
    h.call("CfgWarriorWeapons", {Value(1.0)});
    CHECK(h.state.story.warriorWeapons);
    h.call("CfgWarriorWeapons", {Value(0.0)});
    h.state.story.resetForLevel();
    CHECK(h.state.story.warriorWeapons);
}

TEST_CASE("CarRemovePart removes a door with its window", "[mission7_bindings]") {
    Harness h;
    REQUIRE(h.cars.spawn("car_wagon", coney::anim::Vec3{}, coney::anim::Quat{}, 3) != nullptr);
    h.call("CarRemovePart", {Value(3.0), Value(18.0), Value(1.0)});
    CHECK(h.cars.find(3)->removedParts == ((1U << 18) | (1U << 19)));
    // Not a car: nothing happens.
    h.call("CarRemovePart", {Value(4.0), Value(18.0), Value(1.0)});
}

TEST_CASE("ObjEnablePhysics gives a live object a body or takes it away", "[mission7_bindings]") {
    Harness h;
    coney::world_objects::SpawnRecord live;
    live.handle = 20.0;
    live.live = true;
    REQUIRE(h.records.add(live) != nullptr);
    coney::world_objects::SpawnRecord stored;
    stored.handle = 21.0;
    REQUIRE(h.records.add(stored) != nullptr);
    h.call("ObjEnablePhysics", {Value(20.0), Value(1.0)});
    h.call("ObjEnablePhysics", {Value(21.0), Value(1.0)});
    CHECK(h.records.find(20.0)->physicsBody == true);
    CHECK_FALSE(h.records.find(21.0)->physicsBody.has_value());
    h.call("ObjEnablePhysics", {Value(20.0), Value()});
    CHECK(h.records.find(20.0)->physicsBody == false);
}

TEST_CASE("SoundPreLoad keeps a sound at a point until SoundStart plays it there", "[mission7_bindings]") {
    Harness h;
    const std::vector<Value> handle = h.call("SoundPreLoad", {text("fx_bang"), point(1.0F, 2.0F, 3.0F)});
    REQUIRE(handle.size() == 1);
    CHECK(h.sound.calls.empty());
    h.call("SoundStart", {handle.front()});
    CHECK(h.sound.calls == std::vector<std::string>{std::format("3d {:#x} 1 2 3", coney::crc32("fx_bang"))});
    // Started once, the handle names nothing more; an unknown handle does nothing.
    h.call("SoundStart", {handle.front()});
    h.call("SoundStart", {Value(12345.0)});
    CHECK(h.sound.calls.size() == 1);
}

TEST_CASE("StartRoomSmoke starts the overlay with the scripts' colour and EndRoomSmoke stops it",
          "[mission7_bindings]") {
    Harness h;
    coney::effects::LevelEffects effects;
    h.context.effects = &effects;
    auto colour = std::make_shared<coney::script::Table>();
    double key = 1.0;
    for (const double value : {120.0, 120.0, 140.0, 220.0, 240.0}) {
        REQUIRE(colour->set(Value(key), Value(value)));
        key += 1.0;
    }
    h.call("StartRoomSmoke", {Value(colour), Value(0.5)});
    REQUIRE(effects.smoke.running());
    CHECK(effects.smoke.settings().tint == std::array<std::uint8_t, 3>{120, 120, 140});
    CHECK(effects.smoke.settings().lowestAlpha == 220);
    CHECK(effects.smoke.settings().highestAlpha == 240);
    CHECK(effects.smoke.settings().amount == 0.5F);
    h.call("EndRoomSmoke");
    CHECK_FALSE(effects.smoke.running());
}

TEST_CASE("The HUD bar bindings make, fill and recolour the scripted bars", "[mission7_bindings]") {
    Harness h;
    coney::hud::Hud hud{coney::hud::HudServices{}};
    h.context.hud = &hud;
    auto labels = std::make_shared<coney::script::Table>();
    REQUIRE(labels->set(Value(1.0), text("Diego")));
    REQUIRE(labels->set(Value(2.0), text("Vargas")));
    h.call("HUDEnableBar", {Value(3.0), Value(1.0), Value(labels), Value(2.0), Value()});
    CHECK(hud.bars().bar(0).label == "Diego");
    CHECK(hud.bars().bar(1).label == "Vargas");
    h.call("HUDSetBarPercentage", {Value(3.0), Value(0.4), Value(1.0), Value(1.0), Value(0.0)});
    CHECK(hud.bars().bar(1).fill == 0.4F);
    h.call("HUDSetBarProperty", {Value(0.0), Value(), point(10.0F, 20.0F, 30.0F), Value(0.3)});
    CHECK(hud.bars().bar(0).colour == coney::graphics::Rgba{10, 20, 30, 255});
    CHECK(hud.bars().bar(0).width == 0.3F);
    // The gauge takes texA and texB from the call, or the chasebar defaults.
    h.call("HUDEnableBar", {Value(2.0), Value(1.0)});
    CHECK(hud.bars().gauge().track == 0x1a0000U);
    CHECK(hud.bars().gauge().endIcon == 0x1a0004U);
}
