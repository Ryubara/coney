// SPDX-License-Identifier: GPL-3.0-or-later
// What a guest presents of the host's game events (docs/guides/multiplayer.md#events): each kind the client game plays
// reaches this machine's sound or HUD once, the kinds it cannot present yet are counted and nothing else changes.
#include "gamemodes/client_event_player.h"

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_events.h"
#include "hud/hud.h"
#include "scripting/sound_bindings.h"

using coney::ClientEventPlayer;
using coney::GameEvent;
using coney::GameEventKind;

namespace {

// A sound that writes down the calls it is given; each sound started takes the next handle from 21.
class RecordingSound final : public coney::script::SoundHost {
  public:
    std::vector<std::string> calls;
    double next = 21.0;

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
    void playAmbientTrack(std::uint32_t sound) override { calls.push_back(std::format("loop {:#x}", sound)); }
    void pauseSound(bool /*on*/) override {}
    double play2D(std::uint32_t sound) override {
        calls.push_back(std::format("2d {:#x}", sound));
        return next++;
    }
    void stopSound(double handle) override { calls.push_back(std::format("stop {}", handle)); }
    void stopAmbientTrack() override { calls.emplace_back("loop stop"); }
    void setAmbientTrackVolume(float /*volume*/) override {}
    void playMusic(std::uint32_t track, bool loop, std::string_view callback) override {
        calls.push_back(std::format("music {:#x} loop={} callback={}", track, loop, callback.size()));
    }
    void playSystemMusic(std::uint32_t track, int fadeBars) override {
        calls.push_back(std::format("system music {:#x} fade={}", track, fadeBars));
    }
    void stopMusic() override { calls.emplace_back("music stop"); }
    void setMusicVolume(float /*volume*/) override {}
    void setListener(int /*listener*/) override {}
    bool speak(const coney::script::SpeechCall& /*call*/, std::string_view /*callback*/,
               std::optional<double> /*callbackArg*/) override {
        return false;
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

} // namespace

// Expected: unverified, #1614 (Coney's own network play; the original has none)
TEST_CASE("client event player: the host's music and ambient loop play on this machine's sound",
          "[gamemodes][events][client_events]") {
    RecordingSound sound;
    ClientEventPlayer player(&sound, nullptr);
    player.apply(GameEvent{.kind = GameEventKind::MusicStart, .id = 0x30, .value = 1});
    player.apply(GameEvent{.kind = GameEventKind::MusicStart, .id = 0x31, .value = 0});
    // Bit 1 is the system music, which fades in over `extra` bars.
    player.apply(GameEvent{.kind = GameEventKind::MusicStart, .id = 0x32, .value = 3, .extra = 2});
    player.apply(GameEvent{.kind = GameEventKind::MusicStop});
    player.apply(GameEvent{.kind = GameEventKind::LoopStart, .id = 0x40});
    player.apply(GameEvent{.kind = GameEventKind::LoopStop});
    CHECK(sound.calls == std::vector<std::string>{"music 0x30 loop=true callback=0", "music 0x31 loop=false callback=0",
                                                  "system music 0x32 fade=2", "music stop", "loop 0x40", "loop stop"});
    CHECK(player.presented() == 6);
    CHECK(player.unpresented() == 0);
}

// Expected: unverified, #1614 (Coney's own network play; the original has none)
TEST_CASE("client event player: the host's sound starts and stops are played by instance, and end with the session",
          "[gamemodes][events][client_events]") {
    RecordingSound sound;
    ClientEventPlayer player(&sound, nullptr);
    player.apply(GameEvent{.kind = GameEventKind::Sound2D, .id = 0x10, .value = 5});
    player.apply(GameEvent{.kind = GameEventKind::SoundStop, .value = 5});
    player.apply(GameEvent{.kind = GameEventKind::Sound2D, .id = 0x11, .value = 6});
    CHECK(sound.calls == std::vector<std::string>{"2d 0x10", "stop 21", "2d 0x11"});
    CHECK(player.presented() == 3);
    // Stopping the session stops the sound still kept.
    player.stopAll();
    CHECK(sound.calls.back() == "stop 22");
}

// Expected: unverified, #1614 (Coney's own network play; the original has none)
TEST_CASE("client event player: the host's messages, objectives and hints reach this machine's HUD",
          "[gamemodes][events][client_events]") {
    coney::hud::Hud hud;
    ClientEventPlayer player(nullptr, &hud);
    // An announcement (kind 5, centred text).
    player.apply(GameEvent{.kind = GameEventKind::HudMessage, .id = 5, .value = 0, .text = "Run!"});
    REQUIRE(hud.centredAnnouncement().has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    CHECK(hud.centredAnnouncement()->text == "Run!");
    // An objective set in slot 1 (mode 0 sets it, shifted left by 1 with bit 0 silent), shown for 3 s.
    player.apply(GameEvent{.kind = GameEventKind::Objective, .id = 1, .value = 0, .extra = 3000, .text = "Find Cyrus"});
    REQUIRE(hud.checklist().slots.at(1).has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): the REQUIRE above ends the test when empty
    CHECK(hud.checklist().slots.at(1)->text == "Find Cyrus");
    // A hint queued at priority 2, then cleared, then another queued and flushed at its priority.
    player.apply(GameEvent{.kind = GameEventKind::Hint, .id = coney::kHintQueued, .value = 2, .text = "Press X"});
    CHECK(hud.hints().contains("Press X"));
    player.apply(GameEvent{.kind = GameEventKind::Hint, .id = coney::kHintCleared});
    player.apply(GameEvent{.kind = GameEventKind::Hint, .id = coney::kHintQueued, .value = 3, .text = "Hold B"});
    CHECK(hud.hints().contains("Hold B"));
    player.apply(GameEvent{.kind = GameEventKind::Hint, .id = coney::kHintFlushed, .value = 3});
    CHECK_FALSE(hud.hints().contains("Hold B"));
    player.apply(GameEvent{.kind = GameEventKind::Hint, .id = coney::kHintsSwitched, .value = 1});
    CHECK(hud.gameTutorialText());
    CHECK(player.presented() == 7);
}

// Expected: unverified, #1614 (Coney's own network play; the original has none)
TEST_CASE("client event player: what it cannot present yet is counted, never half-played",
          "[gamemodes][events][client_events]") {
    RecordingSound sound;
    coney::hud::Hud hud;
    ClientEventPlayer player(&sound, &hud);
    // A human's sound needs the character the human is (#1612); a particle needs the scene's effects.
    player.apply(GameEvent{.kind = GameEventKind::HumanSound, .id = 4, .value = 0});
    player.apply(GameEvent{.kind = GameEventKind::ParticleStart, .text = "dust"});
    player.apply(GameEvent{.kind = GameEventKind::Speech, .text = "line"});
    CHECK(sound.calls.empty());
    CHECK(player.presented() == 0);
    CHECK(player.unpresented() == 3);
    // With no sound and no HUD an event of a kind it plays has nowhere to go: counted the same way.
    ClientEventPlayer silent(nullptr, nullptr);
    silent.apply(GameEvent{.kind = GameEventKind::MusicStop});
    silent.apply(GameEvent{.kind = GameEventKind::HudMessage, .id = 5, .text = "Run!"});
    CHECK(silent.presented() == 0);
    CHECK(silent.unpresented() == 2);
}

// Expected: unverified, #1614 (Coney's own network play; the original has none)
TEST_CASE("client event player: an interface cue plays on the HUD's sound once", "[gamemodes][events][client_events]") {
    std::vector<std::string> played;
    coney::hud::Hud hud;
    hud.setSoundOutput([&played](std::string_view name) {
        played.emplace_back(name);
        return coney::hud::HudSoundHandle{1};
    });
    coney::hud::HudServices services = hud.services();
    services.sound.cueName = [](int cue) { return cue == 23 ? std::string("flash") : std::string(); };
    hud.setServices(std::move(services));
    ClientEventPlayer player(nullptr, &hud);
    // A 2D cue plays; one a world object raises at a place is positional, which the HUD's sound is not.
    player.apply(GameEvent{.kind = GameEventKind::Cue, .id = 23});
    player.apply(GameEvent{.kind = GameEventKind::Cue, .id = 23, .value = 1, .position = {1.0F, 2.0F, 3.0F}});
    CHECK(played == std::vector<std::string>{"flash"});
    CHECK(player.presented() == 1);
    CHECK(player.unpresented() == 1);
}
