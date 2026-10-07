// SPDX-License-Identifier: GPL-3.0-or-later
// The game's sound as gameplay drives it (docs/research/sound.md): the voice table, the ambient emitters, the humans'
// lines through HuSpeak, HuSpeakNI, HuShutUp and SoundPlayCommand, the listener at the camera and the level's load
// screen and bank. Every sound here is made up: synthetic tables, no game data.
#include "audio/game_sound.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "audio/ambient_emitters.h"
#include "audio/sound_engine.h"
#include "audio/sound_player.h"
#include "audio/voice_table.h"
#include "audio_fixtures.h"
#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "core/error.h"
#include "core/name_hash.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using Catch::Approx;
using coney::audio::AmbientEmitters;
using coney::audio::AmbientEmitterSetup;
using coney::audio::GameSound;
using coney::audio::SoundEngine;
using coney::audio::SoundHandle;
using coney::audio::SoundTables;
using coney::audio::SoundVec;
using coney::audio::VoiceTable;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// The made-up sounds, by name.
constexpr std::string_view kLine = "vags/speeches/l99/l99_test_001";
constexpr std::string_view kOtherLine = "vags/speeches/l99/l99_test_002";
constexpr std::string_view kGull = "vags/ambient/test/gull";
constexpr std::string_view kCrow = "vags/ambient/test/crow";

// The classes: a speech line (streamed, positional, priority 4), a voice line (streamed, positional, directional,
// priority 9), an ambient sound (a positional bank sample, far 100 m).
enum : std::uint8_t { kLineClass, kVoiceClass, kAmbientClass };

// Voice set 3's lines: two `attack` lines and one `pain` line.
std::vector<std::string> voiceLines() {
    return {coney::audio::voiceLineName(3, 1, 1), coney::audio::voiceLineName(3, 1, 2),
            coney::audio::voiceLineName(3, 12, 1)};
}

// The test tables. Every sound is 16,000 bytes at 8 kHz: 3 s by the game's whole-second reckoning, which is how long it
// lasts when it plays virtually (no stream files, no bank: every sound here does).
SoundTables testTables() {
    std::vector<coney::test::TestSound> sounds;
    const auto add = [&sounds](std::string_view name, std::uint8_t soundClass) {
        sounds.push_back(coney::test::TestSound{
            .hash = coney::crc32(name), .size = 16'000, .soundClass = soundClass, .rateIndex = 11});
    };
    add(kLine, kLineClass);
    add(kOtherLine, kLineClass);
    add(kGull, kAmbientClass);
    add(kCrow, kAmbientClass);
    for (const std::string& name : voiceLines()) {
        add(name, kVoiceClass);
    }
    std::ranges::sort(sounds, {}, &coney::test::TestSound::hash);
    auto list = SoundTables::parseSoundList(coney::test::soundListChunk(sounds));
    REQUIRE(list.has_value());
    return SoundTables(std::move(*list),
                       {coney::test::testClass(1, 30, 0x06, 4), coney::test::testClass(1, 30, 0x1c, 9),
                        coney::test::testClass(1, 100, 0x02, 10)},
                       {}, {});
}

// A recording host: the bindings ask nothing of it here.
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

// The game's sound over an engine with the test tables (no stream files; banks are empty and recorded), with a script
// system whose bindings drive it, two humans (player 1, handle 10, and Ash, handle 20, of type 40 with voice set 3)
// and a callback `OnLine` that records its argument (-1 for none).
struct Rig {
    coney::audio::Mixer mixer;
    coney::audio::SoundPlayer player{mixer};
    std::vector<std::string> banks;
    GameSound sound{player};
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    QuietHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans, &flags};
    ScriptSystem scripts;
    std::vector<double> callbacks;

    explicit Rig(bool withSound = true)
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        auto loader = [this](std::string_view name,
                             const SoundTables& tables) -> std::expected<coney::audio::SoundBank, coney::Error> {
            banks.emplace_back(name);
            return coney::audio::SoundBank::decode(std::string(name), {}, {}, tables);
        };
        player.attach(
            std::make_unique<SoundEngine>(mixer, testTables(), nullptr, std::move(loader), SoundEngine::RandomRange{}));
        if (withSound) {
            context.sound = &sound;
            sound.connect(&scripts, &context);
        }
        REQUIRE(humans.add(coney::HumanCreation{.name = "Cleon",
                                                .type = 1,
                                                .position = std::array<float, 3>{0.0F, 0.0F, 0.0F},
                                                .playerIndex = 1,
                                                .handle = 10.0}));
        REQUIRE(humans.add(coney::HumanCreation{
            .name = "Ash", .type = 40, .position = std::array<float, 3>{5.0F, 0.0F, 0.0F}, .handle = 20.0}));
        scripts.create();
        scripts.vm().registerFunction(
            "OnLine", [this](std::span<const Value> args) -> std::expected<std::vector<Value>, coney::Error> {
                callbacks.push_back(args.empty() ? -1.0 : args[0].number().value_or(-2.0));
                return std::vector<Value>{};
            });
    }

    [[nodiscard]] SoundEngine& engine() { return *player.engine(); }

    // Calls the binding `name` with `args`, REQUIRing success; its first result, nil when there is none.
    Value call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
    }

    // Runs `count` frames of 1/30 s: the game's sound, then the engine.
    void frames(int count) {
        for (int i = 0; i < count; ++i) {
            sound.update();
            player.update(1000.0F / 30.0F);
        }
    }
};

// A string value.
Value str(std::string_view text) { return Value(std::string(text)); }

// A position table {x, y, z}.
Value position(float x, float y, float z) {
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(static_cast<double>(x))).has_value());
    REQUIRE(table->set(Value(2.0), Value(static_cast<double>(y))).has_value());
    REQUIRE(table->set(Value(3.0), Value(static_cast<double>(z))).has_value());
    return Value(std::move(table));
}

// The emitters over a rig's engine with one player at `listener`, on covered ground or not.
struct EmitterRig {
    Rig rig;
    AmbientEmitters emitters;
    SoundVec listener{};
    bool covered = false;

    // Runs `seconds` of 1/30 s frames: the emitters, then the engine.
    void run(double seconds) {
        const auto frames = static_cast<int>(std::lround(seconds * 30.0));
        for (int i = 0; i < frames; ++i) {
            const std::array<SoundVec, 1> listeners{listener};
            const std::array<bool, 1> players{covered};
            emitters.update(rig.engine(),
                            coney::audio::AmbientWorld{.listeners = listeners, .playersCovered = players});
            rig.player.update(1000.0F / 30.0F);
        }
    }
};

// An emitter of the gull in mode `mode`: measured from the origin, range 50 m, a 2 s delay.
AmbientEmitterSetup gulls(std::uint8_t mode, int plays = -1) {
    return AmbientEmitterSetup{.name = "tGulls01",
                               .to = SoundVec{10.0F, 0.0F, 0.0F},
                               .sound = coney::crc32(kGull),
                               .range = 50.0F,
                               .plays = plays,
                               .minDelay = 2,
                               .maxDelay = 2,
                               .mode = mode};
}

} // namespace

TEST_CASE("the voice table counts each set's lines and says them in turn", "[audio][speech]") {
    const std::vector<std::string> names = voiceLines();
    VoiceTable table;
    table.build(5, [&names](std::uint32_t hash) {
        return std::ranges::any_of(names, [hash](const std::string& name) { return coney::crc32(name) == hash; });
    });
    CHECK(table.sets() == 5);
    CHECK(table.lines(3, 1) == 2);
    CHECK(table.lines(3, 12) == 1);
    CHECK(table.lines(2, 1) == 0);
    CHECK(table.percent(3, 1) == 100);
    CHECK(coney::audio::speechCommandName(1) == "attack");
    CHECK(coney::audio::speechCommandName(206) == "hide_response");
    CHECK(coney::audio::voiceLineName(65, 11, 3) == "vags/character/voices/65/block_03");

    // A chance of 100 rolls nothing: a roll would count here.
    int rolls = 0;
    const auto never = [&rolls](std::int32_t, std::int32_t) {
        ++rolls;
        return 0;
    };
    // In turn, wrapping at the count.
    CHECK(table.nextLine(3, 1, never) == coney::crc32(names[0]));
    CHECK(table.nextLine(3, 1, never) == coney::crc32(names[1]));
    CHECK(table.nextLine(3, 1, never) == coney::crc32(names[0]));
    CHECK_FALSE(table.nextLine(2, 1, never).has_value());
    CHECK_FALSE(table.nextLine(9, 1, never).has_value());
    CHECK(rolls == 0);

    // A chance below 100: a roll above it says nothing (and the line does not move on).
    table.setPercent(-1, 12, 40);
    CHECK(table.percent(0, 12) == 40);
    const auto roll = [](std::int32_t value) {
        return [value](std::int32_t low, std::int32_t high) {
            CHECK(low == 0);
            CHECK(high == 99);
            return value;
        };
    };
    CHECK_FALSE(table.nextLine(3, 12, roll(41)).has_value());
    CHECK(table.nextLine(3, 12, roll(40)) == coney::crc32(names[2]));
    table.setPercent(3, 12, 100);
    CHECK(table.percent(3, 12) == 100);
    CHECK(table.percent(0, 12) == 40);
}

TEST_CASE("a timed ambient emitter plays after its delay, once a second at most, not over its own sound",
          "[audio][ambient]") {
    EmitterRig r;
    r.emitters.setSound(7, coney::crc32(kGull));
    r.emitters.setSound(8, coney::crc32(kCrow));
    CHECK(r.emitters.sound(7) == coney::crc32(kGull));
    CHECK(r.emitters.sound(9) == 0);
    r.emitters.setSound(-1, 1); // ignored
    const int id = r.emitters.add(gulls(2), r.rig.engine());
    CHECK(id == 0);
    // The same name, sound and first point is that emitter; another point is another.
    CHECK(r.emitters.add(gulls(2), r.rig.engine()) == 0);
    AmbientEmitterSetup elsewhere = gulls(2);
    elsewhere.from = SoundVec{1.0F, 0.0F, 0.0F};
    CHECK(r.emitters.add(elsewhere, r.rig.engine()) == 1);
    r.emitters.setEnabled(1, false);
    // -1 takes the sound's far distance + 10.
    AmbientEmitterSetup wide = gulls(6);
    wide.name = "tWide";
    wide.range = -1.0F;
    CHECK(r.emitters.add(wide, r.rig.engine()) == 2);
    CHECK(r.emitters.range(2) == Approx(110.0F));

    // The 2 s delay first, judged once a second.
    r.run(1.9);
    CHECK(r.emitters.plays() == 0);
    r.run(0.4);
    CHECK(r.emitters.plays() == 1);
    const SoundHandle first = r.emitters.playing(0);
    CHECK(r.rig.engine().isPlaying(first));
    // Its delay passes while the sound (3 s) plays: no second play, and the delay is drawn again.
    r.run(3.0);
    CHECK(r.emitters.plays() == 1);
    r.run(2.5);
    CHECK(r.emitters.plays() == 2);

    // Positions given by name; an unknown name is refused.
    const std::array<SoundVec, 2> points{SoundVec{1.0F, 2.0F, 3.0F}, SoundVec{4.0F, 5.0F, 6.0F}};
    CHECK(r.emitters.setPositions("tGulls01", points));
    CHECK_FALSE(r.emitters.setPositions("tNobody", points));
    r.emitters.clearEmitters(&r.rig.engine());
    CHECK(r.emitters.emitterCount() == 0);
    CHECK_FALSE(r.rig.engine().isPlaying(first));
    CHECK(r.emitters.sound(8) == coney::crc32(kCrow));
}

TEST_CASE("an ambient emitter out of range plays nothing; its plays run out; its filter picks the players",
          "[audio][ambient]") {
    SECTION("out of range") {
        EmitterRig r;
        r.listener = SoundVec{60.0F, 0.0F, 0.0F};
        r.emitters.add(gulls(4), r.rig.engine());
        r.run(4.0);
        CHECK(r.emitters.plays() == 0);
    }
    SECTION("no plays left switches it off without a play") {
        EmitterRig r;
        r.emitters.add(gulls(2, 0), r.rig.engine());
        r.run(4.0);
        CHECK(r.emitters.plays() == 0);
        CHECK_FALSE(r.emitters.enabled(0));
    }
    SECTION("one play") {
        EmitterRig r;
        r.emitters.add(gulls(4, 1), r.rig.engine());
        r.run(12.0);
        CHECK(r.emitters.plays() == 1);
        CHECK_FALSE(r.emitters.enabled(0));
    }
    SECTION("filter 1: only a player on covered ground hears it") {
        EmitterRig r;
        AmbientEmitterSetup room = gulls(4);
        room.filter = 1;
        r.emitters.add(room, r.rig.engine());
        r.run(2.0);
        CHECK(r.emitters.plays() == 0);
        r.covered = true;
        r.run(1.5);
        CHECK(r.emitters.plays() == 1);
        // Back outside, its sound stops.
        r.covered = false;
        r.run(1.5);
        CHECK_FALSE(r.rig.engine().isPlaying(r.emitters.playing(0)));
    }
    SECTION("filter 0 is not heard on covered ground; filter 2 always") {
        EmitterRig r;
        r.covered = true;
        r.emitters.add(gulls(4), r.rig.engine());
        AmbientEmitterSetup all = gulls(4);
        all.name = "tAll";
        all.filter = 2;
        r.emitters.add(all, r.rig.engine());
        r.run(1.5);
        CHECK(r.emitters.plays() == 1);
        CHECK_FALSE(r.emitters.playing(0).valid());
        CHECK(r.emitters.playing(1).valid());
    }
}

TEST_CASE("the ambient modes: 3 loops in range, 4 plays at once then times, 5 cuts its sound", "[audio][ambient]") {
    SECTION("mode 3") {
        EmitterRig r;
        r.emitters.add(gulls(3), r.rig.engine());
        r.run(1.5);
        CHECK(r.emitters.plays() == 1);
        // Started again when it ends, without a delay.
        r.run(3.0);
        CHECK(r.emitters.plays() == 2);
        // Beyond the range it stops at once.
        r.listener = SoundVec{60.0F, 0.0F, 0.0F};
        r.run(1.1);
        CHECK_FALSE(r.rig.engine().isPlaying(r.emitters.playing(0)));
    }
    SECTION("mode 4") {
        EmitterRig r;
        AmbientEmitterSetup soon = gulls(4);
        soon.minDelay = 10;
        soon.maxDelay = 10;
        r.emitters.add(soon, r.rig.engine());
        r.run(1.5);
        CHECK(r.emitters.plays() == 1);
        CHECK(r.emitters.mode(0) == 2);
        r.run(8.0);
        CHECK(r.emitters.plays() == 1);
        r.run(3.0);
        CHECK(r.emitters.plays() == 2);
    }
    SECTION("mode 5") {
        EmitterRig r;
        r.emitters.add(gulls(5), r.rig.engine());
        r.run(2.4);
        CHECK(r.emitters.plays() == 1);
        const SoundHandle sound = r.emitters.playing(0);
        r.run(1.0);
        CHECK_FALSE(r.rig.engine().isPlaying(sound));
    }
    SECTION("at most two new sounds an update") {
        EmitterRig r;
        for (int i = 0; i < 4; ++i) {
            AmbientEmitterSetup each = gulls(4);
            each.name = "tGulls0" + std::to_string(i);
            r.emitters.add(each, r.rig.engine());
        }
        r.run(1.05);
        CHECK(r.emitters.plays() == 2);
        r.run(1.2);
        CHECK(r.emitters.plays() == 4);
    }
}

TEST_CASE("the ambient bindings fill the table and place a level's emitters", "[audio][ambient]") {
    Rig rig;
    rig.call("AddAmbientSound", {Value(3.0), str(kGull)});
    CHECK(rig.sound.emitters().sound(3) == coney::crc32(kGull));
    const Value id = rig.call("AddAmbientSoundEmitter2",
                              {str("tGulls01"), position(0, 0, 0), position(4, 0, 0), Value(3.0), str(""), Value(1.0),
                               Value(40.0), Value(-1.0), Value(1.0), Value(1.0), Value(4.0), Value(0.0)});
    CHECK(id.number() == 0.0);
    rig.call("SetAmbientEmitterPositions", {str("tGulls01"), position(1, 1, 1), position(2, 2, 2), position(3, 3, 3),
                                            position(0, 0, 0), position(0, 0, 0), Value(3.0)});
    rig.frames(32);
    CHECK(rig.sound.emitters().plays() == 1);
    // A named sound with index -1.
    rig.call("AddAmbientSoundEmitter2",
             {str("clubRadio"), position(0, 0, 0), position(0, 0, 0), Value(-1.0), str(kCrow), Value(1.0), Value(-1.0),
              Value(-1.0), Value(0.0), Value(0.0), Value(3.0), Value(0.0)});
    CHECK(rig.sound.emitters().emitterCount() == 2);
    rig.frames(32);
    CHECK(rig.sound.emitters().plays() == 2);
    // The older form, and an emitter's volume.
    CHECK(rig.call("AddAmbientSoundEmitter", {position(9, 0, 0), position(9, 0, 0), Value(3.0), str(""), Value(1.0),
                                              Value(40.0), Value(-1.0), Value(1.0), Value(1.0), Value(4.0), Value(0.0)})
              .number() == 2.0);
    rig.call("SetAmbientEmitterVolumeMod", {Value(1.0), Value(0.5)});
    // EnableAmbientEmitter switches one off (it plays nothing more) and on again.
    rig.call("EnableAmbientEmitter", {Value(1.0), Value()});
    CHECK_FALSE(rig.sound.emitters().enabled(1));
    rig.call("EnableAmbientEmitter", {Value(1.0)});
    CHECK(rig.sound.emitters().enabled(1));
    // Gameplay's exit forgets the level's emitters.
    rig.sound.gameplayLeft();
    CHECK(rig.sound.emitters().emitterCount() == 0);
}

TEST_CASE("HuSpeak plays a line at the speaker and calls back when it ends", "[audio][speech]") {
    Rig rig;
    rig.call("HuSpeak", {Value(20.0), str(kLine), str("OnLine"), Value(7.0)});
    CHECK(rig.callbacks.empty());
    CHECK(rig.sound.speech().lines() == 1);
    // A second line while he speaks: HuSpeak gives up and calls back at once, with no argument for 0.
    rig.call("HuSpeak", {Value(20.0), str(kOtherLine), str("OnLine"), Value(0.0)});
    CHECK(rig.callbacks == std::vector<double>{-1.0});
    // The line lasts 3 s.
    rig.frames(89);
    CHECK(rig.callbacks.size() == 1);
    rig.frames(3);
    CHECK(rig.callbacks == std::vector<double>{-1.0, 7.0});
    CHECK(rig.sound.speech().lines() == 0);
}

TEST_CASE("HuSpeakNI cuts a line off; HuShutUp stops it; a missing line or human calls back at once",
          "[audio][speech]") {
    Rig rig;
    rig.call("HuSpeak", {Value(20.0), str(kLine), str("OnLine"), Value(1.0)});
    rig.call("HuSpeakNI", {Value(20.0), str(kOtherLine), str("OnLine"), Value(2.0)});
    // The cut-off line's callback is dropped; the new one plays.
    CHECK(rig.callbacks.empty());
    CHECK(rig.sound.speech().lines() == 1);
    rig.call("HuShutUp", {Value(20.0)});
    CHECK(rig.sound.speech().lines() == 0);
    rig.frames(100);
    CHECK(rig.callbacks.empty());

    rig.call("HuSpeak", {Value(20.0), str("vags/speeches/l99/cut"), str("OnLine"), Value(3.0)});
    rig.call("HuSpeakNI", {Value(99.0), str(kLine), str("OnLine"), Value(4.0)});
    rig.call("HuSpeak", {Value(20.0), Value(), str("OnLine"), Value(5.0)});
    CHECK(rig.callbacks == std::vector<double>{3.0, 4.0, 5.0});
}

TEST_CASE("without the game's sound a speech binding calls back at once", "[audio][speech]") {
    Rig rig(false);
    rig.call("HuSpeakNI", {Value(20.0), str(kLine), str("OnLine"), Value(6.0)});
    CHECK(rig.callbacks == std::vector<double>{6.0});
    CHECK(rig.call("SoundPlayCommand", {Value(20.0), Value(1.0), str("OnLine")}).number() == 0.0);
    CHECK(rig.callbacks == std::vector<double>{6.0, 20.0});
}

TEST_CASE("SoundPlayCommand says the speaker's voice set's lines in turn", "[audio][speech]") {
    Rig rig;
    // Ash's type 40 has voice set 3 (CfgChar's twelfth argument).
    std::vector<Value> cfgChar(17, Value(0.0));
    cfgChar[0] = Value(40.0);
    cfgChar[11] = Value(3.0);
    rig.recorded.add("CfgChar", cfgChar);
    CHECK(coney::script::voiceSetOfType(rig.recorded, 40) == 3);
    CHECK(coney::script::voiceSetOfType(rig.recorded, 41) == -1);
    rig.call("SndAllocateCharacterVoices", {Value(10.0)});
    CHECK(rig.sound.voices().lines(3, 1) == 2);

    const double first = rig.call("SoundPlayCommand", {Value(20.0), Value(1.0), str("OnLine")}).number().value_or(0);
    CHECK(first != 0.0);
    // Not interrupting while he speaks: nothing, and the callback at once with the speaker's handle.
    CHECK(rig.call("SoundPlayCommand", {Value(20.0), Value(1.0), str("OnLine"), Value()}).number() == 0.0);
    CHECK(rig.callbacks == std::vector<double>{20.0});
    // Interrupting (the default): the second line.
    CHECK(rig.call("SoundPlayCommand", {Value(20.0), Value(1.0), str("OnLine")}).number().value_or(0) != 0.0);
    rig.frames(95);
    CHECK(rig.callbacks == std::vector<double>{20.0, 20.0});
    // A command the set has no line for says nothing.
    CHECK(rig.call("SoundPlayCommand", {Value(20.0), Value(2.0)}).number() == 0.0);
    // The chance, by command for every set.
    rig.call("SndSetCommandSoundPercent", {Value(-1.0), Value(12.0), Value(30.0)});
    CHECK(rig.sound.voices().percent(3, 12) == 30);
}

TEST_CASE("the listener sits at player 1's camera, or at the player for listener 1", "[audio]") {
    Rig rig;
    coney::camera::FollowCamera follow{coney::anim::Vec3{40.0F, 40.0F, 0.0F}, 0.0F};
    coney::camera::Cameras cameras;
    cameras.attachFollow(&follow);
    rig.context.cameras = &cameras;
    rig.frames(1);
    const coney::audio::Listener& at = rig.sound.listener();
    CHECK(at.position.x == Approx(cameras.view().position.x));
    CHECK(at.position.y == Approx(cameras.view().position.y));
    CHECK(std::hypot(at.right.x, at.right.y, at.right.z) == Approx(1.0F));
    rig.call("SndSetListener", {Value(1.0)});
    CHECK(rig.sound.listenerMode() == 1);
    rig.frames(1);
    // Player 1 (handle 10) stands at the origin: the listener 1.8 m above his feet.
    CHECK(rig.sound.listener().position.x == Approx(0.0F));
    CHECK(rig.sound.listener().position.z == Approx(GameSound::kPlayerEarHeight));
}

TEST_CASE("gameplay's load screen loads load_NN, defers SndLoadBank, then loads the level's bank", "[audio]") {
    Rig rig;
    rig.sound.gameplayEntered();
    rig.sound.levelLoadStarted(1);
    REQUIRE(rig.banks.size() == 1);
    CHECK(rig.banks[0].starts_with("load_0"));
    rig.call("SndLoadBank", {str("boss")});
    CHECK(rig.banks.size() == 1);
    rig.sound.levelLoaded();
    CHECK(rig.banks.back() == "boss");
    rig.call("SndLoadBank", {str("sound")});
    CHECK(rig.banks.back() == "sound");
    // An Armies level's load screen.
    rig.sound.levelLoadStarted(61);
    CHECK(rig.banks.back() == "armload");
    // The front end's bank and cues.
    rig.sound.loadBank("menu");
    CHECK(rig.banks.back() == "menu");
    rig.call("SoundCfgInterfaceSound", {Value(9.0), str(kGull)});
    rig.sound.playCue(9);
    CHECK(rig.engine().stats().started >= 1);
}
