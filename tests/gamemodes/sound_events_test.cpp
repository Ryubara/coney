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
#include "human/human_sounds.h"
#include "scripting/sound_bindings.h"

using coney::GameEvent;
using coney::GameEventKind;

namespace {

// A sound that writes down the calls a player hears and answers with fixed handles.
class RecordingSound final : public coney::script::SoundHost {
  public:
    std::vector<std::string> calls;

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
        return 11.0;
    }
    double play3D(std::uint32_t sound, const std::array<float, 3>& /*position*/) override {
        calls.push_back(std::format("3d {:#x}", sound));
        return 12.0;
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
    bool speak(const coney::script::SpeechCall& /*call*/, std::string_view /*callback*/,
               std::optional<double> /*callbackArg*/) override {
        return true;
    }
    void shutUp(double /*human*/, bool /*force*/) override {}
    std::optional<double> sayCommand(const coney::script::CommandCall& /*call*/,
                                     std::string_view /*callback*/) override {
        return 4.0;
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
    // Not heard by a player: passed on, nothing raised.
    seam.loadSoundBank("level99");
    CHECK(seam.addAmbientEmitter({}) == 3.0);
    CHECK(seam.sayCommand({}, {}) == 4.0);
    CHECK(seam.soundPlaying(11.0));

    CHECK(sound.calls == std::vector<std::string>{"2d 0x10", "3d 0x20", "ambient 0x30", "ambient stop",
                                                  "music 0x40 false OnEnd", "system 0x41 2", "music stop", "human 5",
                                                  "human 5", "human 5", "bank level99"});
    const std::vector<GameEvent> raised = events.take();
    REQUIRE(raised.size() == 10);
    CHECK(raised[0] == GameEvent{.kind = GameEventKind::Sound2D, .id = 0x10});
    CHECK(raised[1] == GameEvent{.kind = GameEventKind::Sound3D, .id = 0x20, .position = {1.0F, 2.0F, 3.0F}});
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
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("sound events: offline the sound plays the same and nothing is kept", "[gamemodes][events]") {
    RecordingSound sound;
    coney::GameEvents events;
    coney::SoundEvents seam(&sound, &events);
    CHECK(seam.play2D(0x10) == 11.0);
    seam.stopMusic();
    CHECK(sound.calls.size() == 2);
    CHECK(events.size() == 0);
    // With no sound at all, nothing plays and the defaults come back.
    coney::SoundEvents silent(nullptr, nullptr);
    CHECK(silent.play2D(0x10) == 0.0);
    CHECK_FALSE(silent.soundPlaying(11.0));
    CHECK_FALSE(silent.sayCommand({}, {}).has_value());
    CHECK(silent.loadSoundMatrix("x"));
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
