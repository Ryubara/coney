// SPDX-License-Identifier: GPL-3.0-or-later
// The game's sound with its events (docs/guides/multiplayer.md#events): every call reaches the sound unchanged, with
// what it returns, and the calls a player hears are raised as game events while recording; with no sound nothing
// plays and the defaults come back.
#include "gamemodes/sound_events.h"

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_events.h"
#include "gamemodes/sound_event_player.h"
#include "hud/hud_audio.h"
#include "human/human_sounds.h"
#include "scripting/sound_bindings.h"

using coney::GameEvent;
using coney::GameEventKind;

namespace {

// A sound that writes down the calls a player hears and answers with fixed handles (or, with `counting`, a new handle
// from 21 for each sound started).
class RecordingSound final : public coney::script::SoundHost {
  public:
    std::vector<std::string> calls;
    bool speaks = true;    ///< whether a line or command asked for plays
    bool counting = false; ///< each sound started takes the next handle from 21
    double next = 21.0;    ///< the next handle when counting

    void configureMusicTrack(std::uint32_t /*track*/, float /*barMs*/, float /*volume*/) override {}
    void setInterfaceSound(int /*cue*/, std::uint32_t /*sound*/) override {}
    void allocateCharacterVoices(int /*count*/) override {}
    void setCommandSoundPercent(int /*voiceSet*/, std::uint32_t /*command*/, std::uint32_t /*percent*/) override {}
    void loadSoundBank(std::string_view name) override { calls.push_back(std::format("bank {}", name)); }
    void setNonDuckableDuck(float /*factor*/) override {}
    void setPitchFactor(float /*factor*/) override {}
    void humanSound(const coney::script::HumanSoundCall& call) override {
        calls.push_back(std::format("human {}", call.human));
    }
    void addAmbientSound(int /*index*/, std::uint32_t /*sound*/) override {}
    double addAmbientEmitter(const coney::script::AmbientEmitterCall& /*call*/) override { return 3.0; }
    void setAmbientEmitterPositions(std::string_view /*name*/,
                                    std::span<const std::array<float, 3>> /*points*/) override {}
    void playAmbientTrack(std::uint32_t sound) override { calls.push_back(std::format("ambient {:#x}", sound)); }
    void pauseSound(bool /*on*/) override {}
    double play2D(std::uint32_t sound) override {
        calls.push_back(std::format("2d {:#x}", sound));
        return counting ? next++ : 11.0;
    }
    double play3D(std::uint32_t sound, const std::array<float, 3>& position) override {
        calls.push_back(counting ? std::format("3d {:#x} at {} -> {}", sound, position[0], next)
                                 : std::format("3d {:#x}", sound));
        return counting ? next++ : 12.0;
    }
    void stopSound(double handle) override { calls.push_back(std::format("stop {}", handle)); }
    void moveSound(double handle, const std::array<float, 3>& position, float volume) override {
        calls.push_back(std::format("move {} to {} at {}", handle, position[0], volume));
    }
    void stopAmbientTrack() override { calls.emplace_back("ambient stop"); }
    void setAmbientTrackVolume(float /*volume*/) override {}
    void playMusic(std::uint32_t track, bool loop, std::string_view callback) override {
        calls.push_back(std::format("music {:#x} {} {}", track, loop, callback));
    }
    void playSystemMusic(std::uint32_t track, int fadeBars) override {
        calls.push_back(std::format("system {:#x} {}", track, fadeBars));
    }
    void stopMusic() override { calls.emplace_back("music stop"); }
    void setMusicVolume(float /*volume*/) override {}
    void setListener(int /*listener*/) override {}
    bool speak(const coney::script::SpeechCall& call, std::string_view /*callback*/,
               std::optional<double> /*callbackArg*/) override {
        calls.push_back(std::format("speak {}", call.line));
        return speaks;
    }
    void shutUp(double human, bool force) override { calls.push_back(std::format("shut up {} {}", human, force)); }
    std::optional<double> sayCommand(const coney::script::CommandCall& call, std::string_view /*callback*/) override {
        calls.push_back(std::format("command {}", call.command));
        return speaks ? std::optional<double>(4.0) : std::nullopt;
    }
    [[nodiscard]] bool soundPlaying(double handle) const override { return handle == 11.0; }
    void gameplayEntered() override {}
    void levelLoadStarted(int /*levelNumber*/) override {}
    void levelLoaded() override {}
    void gameplayLeft() override {}
};

// A human's sound call of `kind` from human 5 at his feet (1, 2, 0).
coney::script::HumanSoundCall humanCall(coney::human::HumanSound::Kind kind) {
    coney::script::HumanSoundCall call;
    call.human = 5;
    call.position = {1.0F, 2.0F, 0.0F};
    call.sound.kind = kind;
    call.sound.animSound = 0x77;
    call.sound.material1 = 14;
    call.sound.material2 = 3;
    call.sound.at = coney::anim::Vec3{1.5F, 2.5F, 0.0F};
    return call;
}

} // namespace

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("sound events: the calls a player hears are raised and played, the rest only played", "[gamemodes][events]") {
    RecordingSound sound;
    coney::GameEvents events;
    events.setRecording(true);
    coney::SoundEvents seam(&sound, &events);

    CHECK(seam.play2D(0x10) == 11.0);
    CHECK(seam.play3D(0x20, {1.0F, 2.0F, 3.0F}) == 12.0);
    seam.playAmbientTrack(0x30);
    seam.stopAmbientTrack();
    seam.playMusic(0x40, false, "OnEnd");
    seam.playSystemMusic(0x41, 2);
    seam.stopMusic();
    seam.humanSound(humanCall(coney::human::HumanSound::Kind::Anim));
    seam.humanSound(humanCall(coney::human::HumanSound::Kind::Impact));
    seam.humanSound(humanCall(coney::human::HumanSound::Kind::RageStart));
    // Speech: a line by name, a command and a line stopped, each raised when it plays.
    CHECK(seam.speak(coney::script::SpeechCall{.human = 6, .line = "l99_hi", .interrupt = true, .lookAt = 0}, {}, {}));
    CHECK(seam.sayCommand(coney::script::CommandCall{.human = 6, .voiceSet = 2, .command = 75, .cutting = true}, {}) ==
          4.0);
    seam.shutUp(6, true);
    // A line or a command that does not play raises nothing.
    sound.speaks = false;
    CHECK_FALSE(seam.speak(coney::script::SpeechCall{.human = 6, .line = "l99_no"}, {}, {}));
    CHECK_FALSE(seam.sayCommand(coney::script::CommandCall{.human = 6, .command = 76}, {}).has_value());
    // Not heard by a player: passed on, nothing raised.
    seam.loadSoundBank("level99");
    CHECK(seam.addAmbientEmitter({}) == 3.0);
    CHECK(seam.soundPlaying(11.0));

    CHECK(sound.calls == std::vector<std::string>{"2d 0x10", "3d 0x20", "ambient 0x30", "ambient stop",
                                                  "music 0x40 false OnEnd", "system 0x41 2", "music stop", "human 5",
                                                  "human 5", "human 5", "speak l99_hi", "command 75", "shut up 6 true",
                                                  "speak l99_no", "command 76", "bank level99"});
    const std::vector<GameEvent> raised = events.take();
    REQUIRE(raised.size() == 13);
    // Each sound started names its instance, from 1.
    CHECK(raised[0] == GameEvent{.kind = GameEventKind::Sound2D, .id = 0x10, .value = 1});
    CHECK(raised[1] ==
          GameEvent{.kind = GameEventKind::Sound3D, .id = 0x20, .value = 2, .position = {1.0F, 2.0F, 3.0F}});
    CHECK(raised[2] == GameEvent{.kind = GameEventKind::LoopStart, .id = 0x30});
    CHECK(raised[3] == GameEvent{.kind = GameEventKind::LoopStop});
    CHECK(raised[4] == GameEvent{.kind = GameEventKind::MusicStart, .id = 0x40, .value = 0});
    CHECK(raised[5] == GameEvent{.kind = GameEventKind::MusicStart, .id = 0x41, .value = 3, .extra = 2});
    CHECK(raised[6] == GameEvent{.kind = GameEventKind::MusicStop});
    CHECK(raised[7] ==
          GameEvent{
              .kind = GameEventKind::HumanSound, .subject = 5, .id = 0x77, .value = 0, .position = {1.0F, 2.0F, 0.0F}});
    CHECK(raised[8] ==
          GameEvent{.kind = GameEventKind::Hit, .subject = 5, .id = 14, .value = 3, .position = {1.5F, 2.5F, 0.0F}});
    CHECK(raised[9] ==
          GameEvent{
              .kind = GameEventKind::HumanSound, .subject = 5, .id = 0, .value = 2, .position = {1.0F, 2.0F, 0.0F}});
    CHECK(raised[10] == GameEvent{.kind = GameEventKind::Speech, .subject = 6, .value = 1, .text = "l99_hi"});
    // Voice set 2 travels as 3; bit 0 interrupting (the default), bit 1 cutting.
    CHECK(raised[11] == GameEvent{.kind = GameEventKind::Command, .subject = 6, .id = 75, .value = 3, .extra = 3});
    CHECK(raised[12] == GameEvent{.kind = GameEventKind::SpeechStop, .subject = 6, .value = 1});
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("sound events: offline the sound plays the same and nothing is kept", "[gamemodes][events]") {
    RecordingSound sound;
    coney::GameEvents events;
    coney::SoundEvents seam(&sound, &events);
    CHECK(seam.play2D(0x10) == 11.0);
    seam.stopMusic();
    seam.moveSound(11.0, {1.0F, 0.0F, 0.0F}, 0.5F);
    seam.stopSound(11.0);
    CHECK(sound.calls == std::vector<std::string>{"2d 0x10", "music stop", "move 11 to 1 at 0.5", "stop 11"});
    CHECK(events.size() == 0);
    // With no sound at all, nothing plays and the defaults come back.
    coney::SoundEvents silent(nullptr, nullptr);
    CHECK(silent.play2D(0x10) == 0.0);
    CHECK_FALSE(silent.soundPlaying(11.0));
    CHECK_FALSE(silent.sayCommand({}, {}).has_value());
    CHECK(silent.loadSoundMatrix("x"));
}

// Expected: unverified, #1473 (Coney's own network play; the original has none)
TEST_CASE("sound events: a sound's stop and move name the instance its start gave it", "[gamemodes][events]") {
    RecordingSound sound;
    sound.counting = true;
    coney::GameEvents events;
    events.setRecording(true);
    coney::SoundEvents seam(&sound, &events);

    const double radio = seam.play3D(0x20, {1.0F, 2.0F, 3.0F}); // instance 1, handle 21
    const double other = seam.play2D(0x10);                     // instance 2, handle 22
    // A move to where it plays, at the full volume it started at, changes nothing a guest hears: none raised.
    seam.moveSound(radio, {1.0F, 2.0F, 3.0F}, 1.0F);
    // A scene halves the volume; the radio moves; a repeat of the same changes nothing.
    seam.moveSound(radio, {1.0F, 2.0F, 3.0F}, 0.5F);
    seam.moveSound(radio, {4.0F, 2.0F, 3.0F}, 0.5F);
    seam.moveSound(radio, {4.0F, 2.0F, 3.0F}, 0.5F);
    seam.stopSound(radio);
    // A handle stopped already, or one no start of this seam gave, names no instance: passed on, nothing raised.
    seam.stopSound(radio);
    seam.moveSound(99.0, {0.0F, 0.0F, 0.0F}, 1.0F);
    seam.stopSound(other);

    CHECK(sound.calls == std::vector<std::string>{"3d 0x20 at 1 -> 21", "2d 0x10", "move 21 to 1 at 1",
                                                  "move 21 to 1 at 0.5", "move 21 to 4 at 0.5", "move 21 to 4 at 0.5",
                                                  "stop 21", "stop 21", "move 99 to 0 at 1", "stop 22"});
    const std::vector<GameEvent> raised = events.take();
    REQUIRE(raised.size() == 6);
    CHECK(raised[0] ==
          GameEvent{.kind = GameEventKind::Sound3D, .id = 0x20, .value = 1, .position = {1.0F, 2.0F, 3.0F}});
    CHECK(raised[1] == GameEvent{.kind = GameEventKind::Sound2D, .id = 0x10, .value = 2});
    // The volume travels in thousandths.
    CHECK(raised[2] ==
          GameEvent{.kind = GameEventKind::SoundMove, .value = 1, .extra = 500, .position = {1.0F, 2.0F, 3.0F}});
    CHECK(raised[3] ==
          GameEvent{.kind = GameEventKind::SoundMove, .value = 1, .extra = 500, .position = {4.0F, 2.0F, 3.0F}});
    CHECK(raised[4] == GameEvent{.kind = GameEventKind::SoundStop, .value = 1});
    CHECK(raised[5] == GameEvent{.kind = GameEventKind::SoundStop, .value = 2});
}

// Expected: unverified, #1473 (Coney's own network play; the original has none)
TEST_CASE("sound events: a sound started before recording, or refused, can raise no stop", "[gamemodes][events]") {
    RecordingSound sound;
    sound.counting = true;
    coney::GameEvents events;
    coney::SoundEvents seam(&sound, &events);
    const double before = seam.play3D(0x20, {0.0F, 0.0F, 0.0F}); // offline: no instance kept
    events.setRecording(true);
    seam.stopSound(before);
    CHECK(events.take().empty());
    // A start the sound refused (handle 0) still raises its start, with an instance nothing can stop.
    coney::SoundEvents silent(nullptr, &events);
    CHECK(silent.play3D(0x30, {0.0F, 0.0F, 0.0F}) == 0.0);
    silent.stopSound(0.0);
    const std::vector<GameEvent> raised = events.take();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0].kind == GameEventKind::Sound3D);
    CHECK(raised[0].value == 1);
}

// Expected: unverified, #1473 (Coney's own network play; the original has none)
TEST_CASE("sound event player: a guest starts, moves and stops its own sound by the host's instance",
          "[gamemodes][events]") {
    RecordingSound sound;
    sound.counting = true;
    coney::SoundEventPlayer player(&sound);
    player.apply(GameEvent{.kind = GameEventKind::Sound3D, .id = 0x20, .value = 7, .position = {1.0F, 2.0F, 3.0F}});
    player.apply(GameEvent{.kind = GameEventKind::Sound2D, .id = 0x10, .value = 8});
    player.apply(GameEvent{.kind = GameEventKind::Sound2D, .id = 0x11, .value = 0}); // no instance: played, not kept
    CHECK(player.instances() == 2);
    player.apply(GameEvent{.kind = GameEventKind::SoundMove, .value = 7, .extra = 500, .position = {4.0F, 2.0F, 3.0F}});
    player.apply(GameEvent{.kind = GameEventKind::SoundStop, .value = 7});
    // An instance this guest never started (its start was lost, or came before it joined) is nothing to stop.
    player.apply(GameEvent{.kind = GameEventKind::SoundStop, .value = 7});
    player.apply(GameEvent{.kind = GameEventKind::SoundMove, .value = 9, .extra = 1000});
    // The other kinds are the client game mode's to play: left alone here.
    player.apply(GameEvent{.kind = GameEventKind::MusicStop});
    CHECK(sound.calls ==
          std::vector<std::string>{"3d 0x20 at 1 -> 21", "2d 0x10", "2d 0x11", "move 21 to 4 at 0.5", "stop 21"});
    CHECK(player.instances() == 1);
    // Leaving the session stops what still plays.
    player.stopAll();
    CHECK(sound.calls.back() == "stop 22");
    CHECK(player.instances() == 0);
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("scene changes: ends then starts, compared a step apart", "[gamemodes][events]") {
    coney::GameEvents events;
    events.setRecording(true);
    std::vector<std::uint32_t> playing;
    coney::raiseSceneChanges(&events, playing, {3, 7});
    coney::raiseSceneChanges(&events, playing, {7, 9});
    coney::raiseSceneChanges(&events, playing, {7, 9});
    const std::vector<GameEvent> raised = events.take();
    REQUIRE(raised.size() == 4);
    CHECK(raised[0] == GameEvent{.kind = GameEventKind::SceneStart, .id = 3});
    CHECK(raised[1] == GameEvent{.kind = GameEventKind::SceneStart, .id = 7});
    CHECK(raised[2] == GameEvent{.kind = GameEventKind::SceneEnd, .id = 3});
    CHECK(raised[3] == GameEvent{.kind = GameEventKind::SceneStart, .id = 9});
    CHECK(playing == std::vector<std::uint32_t>{7, 9});
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("a cue gameplay plays on the HUD is raised for that player's HUD, then played", "[gamemodes][events]") {
    std::vector<std::string> played;
    coney::hud::HudSound sound;
    sound.play = [&played](std::string_view name) {
        played.emplace_back(name);
        return coney::hud::HudSoundHandle{1};
    };
    sound.cueName = [](int cue) { return cue == 23 ? std::string("flash") : std::string(); };
    coney::GameEvents events;
    // Offline it plays the same and nothing is kept.
    coney::playHudCue(sound, &events, 23, 4);
    CHECK(events.size() == 0);
    events.setRecording(true);
    coney::playHudCue(sound, &events, 23, 4);
    coney::playHudCue(sound, nullptr, 23, 4);
    CHECK(played == std::vector<std::string>{"flash", "flash", "flash"});
    const std::vector<GameEvent> raised = events.take();
    REQUIRE(raised.size() == 1);
    CHECK(raised[0] == GameEvent{.kind = GameEventKind::Cue, .subject = 4, .id = 23, .value = 0});
}
