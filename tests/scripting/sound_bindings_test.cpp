// SPDX-License-Identifier: GPL-3.0-or-later
// The sound bindings' arguments as tolua reads them (docs/references/bindings/sound.md), handed to a recording sound
// host, and the music bindings' fall-back to the binding host without one.
#include "scripting/sound_bindings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/name_hash.h"
#include "gamemodes/system_music.h"
#include "gui/global_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

using Catch::Approx;
using coney::script::LuaVm;
using coney::script::ScriptSystem;
using coney::script::Value;

namespace {

// A binding host that records the music requests it gets.
class MusicHost final : public coney::script::BindingHost {
  public:
    std::vector<std::string> music;
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view track) override { music.emplace_back(track); }
    void stopMusic() override { music.emplace_back("stop"); }
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// A matrix call's three columns as text: a hash in hex, `-` for a column left as it is.
std::string columnsText(const std::array<std::optional<std::uint32_t>, 3>& sounds) {
    std::string text;
    for (const std::optional<std::uint32_t>& sound : sounds) {
        text += sound ? std::format("{:#x} ", *sound) : std::string("- ");
    }
    return text;
}

// A sound host that keeps what it is asked.
class RecordingSound final : public coney::script::SoundHost {
  public:
    std::vector<std::string> calls;
    std::vector<int> fades; // playSystemMusic()'s fades
    std::optional<coney::script::AmbientEmitterCall> emitter;
    std::vector<std::array<float, 3>> positions;
    bool speaks = true;

    void configureMusicTrack(std::uint32_t track, float barMs, float volume) override {
        calls.push_back(std::format("music info {:#x} {} {}", track, barMs, volume));
    }
    void setInterfaceSound(int cue, std::uint32_t sound) override {
        calls.push_back(std::format("cue {} {:#x}", cue, sound));
    }
    void allocateCharacterVoices(int count) override { calls.push_back(std::format("voices {}", count)); }
    void setCommandSoundPercent(int voiceSet, std::uint32_t command, std::uint32_t percent) override {
        calls.push_back(std::format("percent {} {} {}", voiceSet, command, percent));
    }
    void loadSoundBank(std::string_view name) override { calls.push_back(std::format("bank {}", name)); }
    void setNonDuckableDuck(float factor) override { calls.push_back(std::format("niduck {}", factor)); }
    void setPitchFactor(float factor) override { calls.push_back(std::format("pitch {}", factor)); }
    void playSystemMusic(std::uint32_t track, int fadeBars) override {
        fades.push_back(fadeBars);
        playMusic(track, true, {});
    }
    void addAmbientSound(int index, std::uint32_t sound) override {
        calls.push_back(std::format("ambient {} {:#x}", index, sound));
    }
    double addAmbientEmitter(const coney::script::AmbientEmitterCall& call) override {
        emitter = call;
        return 4.0;
    }
    void setAmbientEmitterPositions(std::string_view name, std::span<const std::array<float, 3>> points) override {
        calls.push_back(std::format("positions {}", name));
        positions.assign(points.begin(), points.end());
    }
    void playAmbientTrack(std::uint32_t sound) override { calls.push_back(std::format("track {:#x}", sound)); }
    void pauseSound(bool on) override { calls.push_back(std::format("pause {}", on)); }
    double play2D(std::uint32_t sound) override {
        calls.push_back(std::format("2d {:#x}", sound));
        return 7.0;
    }
    void stopAmbientTrack() override { calls.emplace_back("track stop"); }
    void setAmbientTrackVolume(float volume) override { calls.push_back(std::format("track volume {}", volume)); }
    void playMusic(std::uint32_t track, bool loop, std::string_view callback) override {
        calls.push_back(std::format("music {:#x} {} {}", track, loop, callback));
    }
    void stopMusic() override { calls.emplace_back("music stop"); }
    void setMusicVolume(float volume) override { calls.push_back(std::format("music volume {}", volume)); }
    void setListener(int listener) override { calls.push_back(std::format("listener {}", listener)); }
    bool speak(const coney::script::SpeechCall& call, std::string_view callback,
               std::optional<double> callbackArg) override {
        calls.push_back(std::format("speak {} {} {} {} {}", call.human, call.line, call.interrupt, callback,
                                    callbackArg.value_or(-1.0)));
        return speaks;
    }
    void shutUp(double human, bool force) override { calls.push_back(std::format("shut up {} {}", human, force)); }
    std::optional<double> sayCommand(const coney::script::CommandCall& call, std::string_view callback) override {
        calls.push_back(
            std::format("command {} {} {} {} {}", call.human, call.voiceSet, call.command, call.interrupt, callback));
        return 77.0;
    }
    void newMaterialSlots(std::uint32_t m1, std::uint32_t m2, std::uint32_t count, std::uint32_t columns,
                          const std::array<float, 3>& volumes) override {
        calls.push_back(
            std::format("slots {} {} {} {} {} {} {}", m1, m2, count, columns, volumes[0], volumes[1], volumes[2]));
    }
    void newMaterialSound(std::uint32_t index, std::uint32_t m1, std::uint32_t m2,
                          const std::array<std::optional<std::uint32_t>, 3>& sounds) override {
        calls.push_back(std::format("material sound {} {} {} {}", index, m1, m2, columnsText(sounds)));
    }
    void setMaterialSlotCount(std::uint32_t m1, std::uint32_t m2, std::uint32_t count) override {
        calls.push_back(std::format("slot count {} {} {}", m1, m2, count));
    }
    void duplicateSoundMaterials(std::uint32_t a, std::uint32_t b) override {
        calls.push_back(std::format("duplicate {} {}", a, b));
    }
    void newAnimSlots(std::uint32_t event, std::uint32_t count, std::uint32_t columns,
                      const std::array<float, 3>& volumes) override {
        calls.push_back(std::format("anim slots {} {} {} {}", event, count, columns, volumes[0]));
    }
    void newAnimSound(std::uint32_t index, std::uint32_t event,
                      const std::array<std::optional<std::uint32_t>, 3>& sounds) override {
        calls.push_back(std::format("anim sound {} {} {}", index, event, columnsText(sounds)));
    }
    void gameplayEntered() override {}
    void levelLoadStarted(int /*levelNumber*/) override {}
    void levelLoaded() override {}
    void gameplayLeft() override {}
};

// A script system whose bindings drive a recording sound host (or none).
struct Harness {
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    MusicHost host;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    RecordingSound sound;
    coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans, &flags};
    ScriptSystem scripts;

    explicit Harness(bool withSound = true)
        : scripts(
              [](std::string_view) -> std::expected<std::vector<std::byte>, coney::Error> {
                  return coney::fail(coney::ErrorCode::NotFound, "no scripts in this test");
              },
              [this](ScriptSystem& system, LuaVm& vm) { coney::script::installBindings(system, vm, context); }, {}) {
        if (withSound) {
            context.sound = &sound;
        }
        scripts.create();
    }

    // Calls the binding `name` with `args`; its first result, nil when there is none.
    Value call(std::string_view name, const std::vector<Value>& args = {}) {
        auto result = scripts.vm().call(scripts.vm().global(name), args);
        REQUIRE(result.has_value());
        return result->empty() ? Value() : result->front();
    }
};

Value str(std::string_view text) { return Value(std::string(text)); }

// A position table {x, y, z}.
Value position(double x, double y, double z) {
    auto table = std::make_shared<coney::script::Table>();
    REQUIRE(table->set(Value(1.0), Value(x)).has_value());
    REQUIRE(table->set(Value(2.0), Value(y)).has_value());
    REQUIRE(table->set(Value(3.0), Value(z)).has_value());
    return Value(std::move(table));
}

} // namespace

TEST_CASE("the configuration and music bindings read their arguments as tolua does", "[sound_bindings]") {
    Harness h;
    const std::uint32_t track = coney::crc32("music/test_118");
    h.call("SndCfgMusicInfo", {str("music/test_118"), Value(2034.9), Value(1.5)});
    h.call("SoundCfgInterfaceSound", {Value(9.0), str("menu/accept")});
    h.call("SndAllocateCharacterVoices", {Value(350.0)});
    h.call("SndSetCommandSoundPercent", {Value(-1.0), Value(11.0), Value(20.0)});
    h.call("SoundLoopMusicTrack", {str("music/test_118")});
    h.call("SoundPlayMusicTrack", {Value(static_cast<double>(track)), str("Level.onMusicEnd")});
    h.call("SoundSetMusicVolume", {Value(100.0)});
    h.call("SoundStopMusicTrack");
    h.call("SetAmbientTrackVolume", {Value(-0.5)});
    h.call("SndSetListener", {Value(1.0)});
    h.call("SndLoadBank", {str("boss")});
    CHECK(h.sound.calls ==
          std::vector<std::string>{std::format("music info {:#x} 2034 1", track),
                                   std::format("cue 9 {:#x}", coney::crc32("menu/accept")), "voices 350",
                                   "percent -1 11 20", std::format("music {:#x} true ", track),
                                   std::format("music {:#x} false Level.onMusicEnd", track), "music volume 1",
                                   "music stop", "track volume 0", "listener 1", "bank boss"});
    CHECK(h.host.music.empty());
}

TEST_CASE("without a sound host the music bindings go to the binding host", "[sound_bindings]") {
    Harness h(false);
    h.call("SoundLoopMusicTrack", {str("music/wonderwheel_132b")});
    h.call("SoundStopMusicTrack");
    h.call("HuShutUp", {Value(5.0)});
    CHECK(h.host.music == std::vector<std::string>{"music/wonderwheel_132b", "stop"});
    // An emitter is no one's without the sound: id 0.
    CHECK(h.call("AddAmbientSoundEmitter2", {str("tGulls01")}).number() == 0.0);
}

TEST_CASE("SoundPlay2D plays a sound by its name's hash and answers its handle; NilSoundHandle without sound",
          "[sound_bindings]") {
    Harness h;
    CHECK(h.call("SoundPlay2D", {str("vags/test/cue_21")}).number() == 7.0);
    CHECK(h.sound.calls == std::vector<std::string>{std::format("2d {:#x}", coney::crc32("vags/test/cue_21"))});
    Harness quiet(false);
    CHECK(quiet.call("SoundPlay2D", {str("vags/test/cue_21")}).number() == 0.0);
}

TEST_CASE("SoundPauseSound pauses the sound by default and resumes it with false", "[sound_bindings]") {
    Harness h;
    h.call("SoundPauseSound");
    h.call("SoundPauseSound", {Value(0.0)});
    CHECK(h.sound.calls == std::vector<std::string>{"pause true", "pause false"});
}

TEST_CASE("an emitter's twelve arguments and its positions reach the sound host", "[sound_bindings]") {
    Harness h;
    const Value id = h.call("AddAmbientSoundEmitter2",
                            {str("tGulls01"), position(1, 2, 3), position(4, 5, 6), Value(592.0), str(""), Value(2.0),
                             Value(45.5), Value(-1.0), Value(3.0), Value(9.0), Value(4.0), Value(7.0)});
    CHECK(id.number() == 4.0);
    REQUIRE(h.sound.emitter.has_value());
    if (!h.sound.emitter) {
        return;
    }
    const coney::script::AmbientEmitterCall& e = *h.sound.emitter;
    CHECK(e.name == "tGulls01");
    CHECK(e.from == std::array<float, 3>{1.0F, 2.0F, 3.0F});
    CHECK(e.to == std::array<float, 3>{4.0F, 5.0F, 6.0F});
    CHECK(e.index == 592);
    CHECK(e.count == 2);
    CHECK(e.range == Approx(45.5F));
    CHECK(e.plays == -1);
    CHECK(e.minDelay == 3);
    CHECK(e.maxDelay == 9);
    CHECK(e.mode == 4);
    CHECK(e.filter == 0); // above 2 becomes 0
    h.call("SetAmbientEmitterPositions", {str("tGulls01"), position(1, 1, 1), position(2, 2, 2), position(3, 3, 3),
                                          position(4, 4, 4), position(5, 5, 5), Value(9.0)});
    CHECK(h.sound.positions.size() == 5);
    CHECK(h.sound.positions[4] == std::array<float, 3>{5.0F, 5.0F, 5.0F});

    // The older form: no name (every one is `particle task`), the same arguments after it.
    h.call("AddAmbientSoundEmitter",
           {position(1, 2, 3), position(4, 5, 6), Value(-1.0), str("vags/test/radio"), Value(1.0), Value(-1.0),
            Value(0.0), Value(1.0), Value(2.0), Value(3.0), Value(1.0)});
    REQUIRE(h.sound.emitter.has_value());
    CHECK(h.sound.emitter->name == "particle task");
    CHECK(h.sound.emitter->sound == "vags/test/radio");
    CHECK(h.sound.emitter->index == -1);
    CHECK(h.sound.emitter->plays == 0);
    CHECK(h.sound.emitter->mode == 3);
    CHECK(h.sound.emitter->filter == 1);
}

TEST_CASE("the speech bindings pass the speaker, the line and the callback; a refused line calls back at once",
          "[sound_bindings]") {
    Harness h;
    std::vector<double> called;
    h.scripts.vm().registerFunction(
        "Said", [&called](std::span<const Value> args) -> std::expected<std::vector<Value>, coney::Error> {
            called.push_back(args.empty() ? -1.0 : args[0].number().value_or(-2.0));
            return std::vector<Value>{};
        });
    h.call("HuSpeak", {Value(12.0), str("vags/speeches/l99/a"), str("Said"), Value(3.0)});
    h.call("HuSpeakNI", {Value(12.0), str("vags/speeches/l99/b"), str("Said")});
    h.call("HuShutUp", {Value(12.0), Value(1.0)});
    CHECK(called.empty());
    h.sound.speaks = false;
    h.call("HuSpeak", {Value(12.0), str("vags/speeches/l99/c"), str("Said"), Value(3.0)});
    CHECK(called == std::vector<double>{3.0});
    // The command's voice set comes from the speaker's CfgChar record; unknown here.
    CHECK(h.call("SoundPlayCommand", {Value(12.0), Value(16.0), str("Said"), Value(0.0)}).number() == 77.0);
    CHECK(h.sound.calls == std::vector<std::string>{"speak 12 vags/speeches/l99/a false Said 3",
                                                    "speak 12 vags/speeches/l99/b true Said -1", "shut up 12 true",
                                                    "speak 12 vags/speeches/l99/c false Said 3",
                                                    "command 12 -1 16 false Said"});
}

TEST_CASE("the system music plays a track of the mood and changes with it", "[scripting][sound]") {
    Harness harness;
    coney::StoryState& story = harness.state.story;
    harness.call("SoundSetMusicTrack", {Value(0.0), Value("calm_a"), Value(""), Value("calm_b")});
    harness.call("SoundSetMusicTrack", {Value(1.0), Value("fight_a")});
    CHECK(story.systemMusic);
    CHECK(story.moodTracks[0] == std::vector<std::uint32_t>{coney::crc32("calm_a"), coney::crc32("calm_b")});

    coney::stepSystemMusic(story, &harness.sound, harness.state.random, 1);
    REQUIRE(harness.sound.calls.size() == 1);
    CHECK(harness.sound.calls[0] == std::format("music {:#x} true ", coney::crc32("fight_a")));
    // The same mood again: nothing new.
    coney::stepSystemMusic(story, &harness.sound, harness.state.random, 1);
    CHECK(harness.sound.calls.size() == 1);
    // A mood with no tracks stops the music; switched off, the music stops and nothing more is picked.
    coney::stepSystemMusic(story, &harness.sound, harness.state.random, 2);
    CHECK(harness.sound.calls.back() == "music stop");
    harness.call("SoundEnableSystemMusic", {Value()});
    CHECK_FALSE(story.systemMusic);
    const std::size_t calls = harness.sound.calls.size();
    coney::stepSystemMusic(story, &harness.sound, harness.state.random, 0);
    CHECK(harness.sound.calls.size() == calls);
}

TEST_CASE("SoundSetSystemMusicState holds a mood over the game's, hands it back, or turns the music off",
          "[scripting][sound]") {
    Harness harness;
    coney::StoryState& story = harness.state.story;
    harness.call("SoundSetMusicTrack", {Value(0.0), Value("calm_a")});
    harness.call("SoundSetMusicTrack", {Value(1.0), Value("fight_a")});
    // Held at the fight, the game's calm changes nothing.
    harness.call("SoundSetSystemMusicState", {Value(1.0)});
    CHECK(story.musicHold == 1);
    coney::stepSystemMusic(story, &harness.sound, harness.state.random, 0);
    REQUIRE(harness.sound.calls.size() == 1);
    CHECK(harness.sound.calls[0] == std::format("music {:#x} true ", coney::crc32("fight_a")));
    // 3 hands the mood back: the game's calm plays.
    CHECK(harness.sound.fades.back() == 0); // a cut into the fight
    harness.call("SoundSetSystemMusicState", {Value(3.0)});
    CHECK(story.musicHold == -1);
    coney::stepSystemMusic(story, &harness.sound, harness.state.random, 0);
    CHECK(harness.sound.calls.back() == std::format("music {:#x} true ", coney::crc32("calm_a")));
    CHECK(harness.sound.fades.back() == 4); // back to calm from the fight
    // 4 turns the system music off and stops it.
    harness.call("SoundSetSystemMusicState", {Value(4.0)});
    CHECK_FALSE(story.systemMusic);
    CHECK(harness.sound.calls.back() == "music stop");
    // SoundEnableSystemMusic ends a hold.
    harness.call("SoundSetSystemMusicState", {Value(0.0)});
    harness.call("SoundEnableSystemMusic", {Value(1.0)});
    CHECK(story.musicHold == -1);
}

TEST_CASE("the reverb's settings are kept", "[scripting][sound]") {
    Harness harness;
    harness.call("SoundSetEffect", {Value(5.0), Value(0.2), Value(50.0), Value(50.0)});
    harness.call("SoundEnableEffects");
    CHECK(harness.state.story.reverbType == 5);
    CHECK(harness.state.story.reverbDepth == Approx(0.2F));
    CHECK(harness.state.story.reverbOn);
    harness.call("SoundEnableEffects", {Value()});
    CHECK_FALSE(harness.state.story.reverbOn);
}

TEST_CASE("the sound matrix bindings pass materials, counts, volumes and sound hashes", "[sound_bindings]") {
    Harness h;
    const std::uint32_t punch = coney::crc32("vags/test/punch_01");
    h.call("NewMaterialSlots", {Value(9.0), Value(10.0), Value(2.0), Value(2.0), Value(0.9)});
    h.call("NewMaterialSound", {Value(1.0), Value(9.0), Value(10.0), str("vags/test/punch_01"), str("none")});
    h.call("DuplicateSoundMaterials", {Value(9.0), Value(10.0)});
    h.call("SetNumberOfMaterialSlots", {Value(9.0), Value(10.0), Value(1.0)});
    h.call("NewAnimSlots", {Value(1.0), Value(4.0)});
    h.call("NewAnimSound", {Value(0.0), Value(1.0), Value(), str("vags/test/punch_01")});
    CHECK(h.sound.calls == std::vector<std::string>{"slots 9 10 2 2 0.9 1 1",
                                                    std::format("material sound 1 9 10 {:#x} 0x0 - ", punch),
                                                    "duplicate 9 10", "slot count 9 10 1", "anim slots 1 4 1 1",
                                                    std::format("anim sound 0 1 - {:#x} - ", punch)});
}
