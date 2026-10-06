// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_engine.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/sound_player.h"
#include "audio_fixtures.h"
#include "core/name_hash.h"

using Catch::Approx;
using coney::audio::Listener;
using coney::audio::Mixer;
using coney::audio::SoundBank;
using coney::audio::SoundEngine;
using coney::audio::SoundHandle;
using coney::audio::SoundPlay;
using coney::audio::SoundTables;
using coney::audio::SoundVec;
using coney::test::Bytes;
using coney::test::TestSound;

// Every sound here is made up: constant ADPCM, synthetic tables. No game data.

namespace {

// The classes of the test tables, by index.
enum : std::uint8_t {
    kBank2D,        // a bank sample without position, priority 10
    kBank3D,        // a positional bank sample, near 2 m, far 20 m, priority 10
    kStream2D,      // a streamed mono sound, priority 6
    kSmallLoop,     // a streamed small loop (channels 10-12), priority 6
    kLowest,        // a bank sample of priority 21
    kStereo,        // a streamed stereo bed, priority 3
    kVoice,         // a streamed, positional, directional voice line, near 1 m, far 30 m, priority 9
    kImportantBank, // a bank sample of priority 6
};

constexpr std::uint32_t kBankSound = 0x100;
constexpr std::uint32_t kBank3DSound = 0x200;
constexpr std::uint32_t kStreamSound = 0x300;
constexpr std::uint32_t kLoopSound = 0x400;
constexpr std::uint32_t kLowestSound = 0x500;
constexpr std::uint32_t kStereoSound = 0x600;
constexpr std::uint32_t kVoiceSound = 0x700;
constexpr std::uint32_t kImportantSound = 0x800;
constexpr std::uint32_t kAbsentSample = 0x900; // a bank sound the bank does not hold
constexpr std::uint32_t kShortSound = 0xa00;   // 16,000 bytes at 8 kHz: a 3 s sound by the game's reckoning

// The test tables: one sound per class, plus the absent sample and the short sound.
SoundTables testTables() {
    // Ascending hashes, as the sound list is sorted.
    const std::vector<TestSound> sounds{
        TestSound{.hash = kBankSound, .size = 160, .soundClass = kBank2D},
        TestSound{.hash = kBank3DSound, .size = 160, .soundClass = kBank3D},
        TestSound{.hash = kStreamSound, .size = 16'000, .offset = 0, .soundClass = kStream2D},
        TestSound{.hash = kLoopSound, .size = 160, .offset = 0, .soundClass = kSmallLoop},
        TestSound{.hash = kLowestSound, .size = 160, .soundClass = kLowest},
        TestSound{.hash = kStereoSound, .size = 64, .offset = 0, .soundClass = kStereo},
        TestSound{.hash = kVoiceSound, .size = 16'000, .offset = 0, .soundClass = kVoice},
        TestSound{.hash = kImportantSound, .size = 160, .soundClass = kImportantBank},
        TestSound{.hash = kAbsentSample, .size = 160, .soundClass = kBank2D},
        TestSound{.hash = kShortSound, .size = 16'000, .soundClass = kBank2D, .rateIndex = 11},
    };
    auto list = SoundTables::parseSoundList(coney::test::soundListChunk(sounds));
    REQUIRE(list.has_value());
    std::vector<coney::audio::SoundClass> classes{
        coney::test::testClass(0, 0, 0x00, 10), coney::test::testClass(2, 20, 0x02, 10),
        coney::test::testClass(0, 0, 0x04, 6),  coney::test::testClass(0, 0, 0x25, 6),
        coney::test::testClass(0, 0, 0x00, 21), coney::test::testClass(0, 0, 0x04, 3, 2),
        coney::test::testClass(1, 30, 0x1c, 9), coney::test::testClass(0, 0, 0x00, 6),
    };
    return SoundTables(
        std::move(*list), std::move(classes),
        {coney::audio::StereoLayout{.hash = kStereoSound, .interleave = 32, .lastBlock = 32, .blocks = 4}}, {});
}

// A bank `name` holding a looping constant sample for every bank sound but the absent one; each load is recorded.
SoundEngine::BankLoader testBanks(std::vector<std::string>& loads) {
    return [&loads](std::string_view name, const SoundTables& tables) -> std::expected<SoundBank, coney::Error> {
        loads.emplace_back(name);
        Bytes msd;
        Bytes msb;
        for (const std::uint32_t hash : {kBankSound, kBank3DSound, kLowestSound, kImportantSound, kShortSound}) {
            msd.u32(hash).u32(static_cast<std::uint32_t>(msb.data().size()));
            const auto* record = tables.find(hash);
            msb.append(coney::test::constantAdpcm(1, record->size / 16, 4, coney::audio::kAdpcmEnd));
        }
        msd.u32(0).u32(0);
        return SoundBank::decode(std::string(name), msd.span(), msb.span(), tables);
    };
}

// An engine over a mixer with the test tables, synthetic sound files and the test banks, bank `sound` loaded.
struct Rig {
    Mixer mixer;
    std::vector<std::string> loads;
    coney::test::MemorySoundFiles* files = nullptr;
    std::unique_ptr<SoundEngine> engine;
    std::array<Listener, 1> listeners{};

    Rig() {
        auto memory = std::make_unique<coney::test::MemorySoundFiles>();
        files = memory.get();
        // BFW.SND: a constant sound for every stream at offset 0 (16,000 bytes, more than any stream reads).
        memory->sounds = coney::test::constantAdpcm(2, 1000, 4);
        engine = std::make_unique<SoundEngine>(mixer, testTables(), std::move(memory), testBanks(loads),
                                               SoundEngine::RandomRange{});
        REQUIRE(engine->loadBank("sound").has_value());
    }

    // The `side` volume (0 left, 1 right) last sent for `sound`, or -1 when it has ended.
    [[nodiscard]] float sent(SoundHandle sound, std::size_t side) const {
        return engine->sentVolumes(sound).value_or(std::array<float, 2>{-1.0F, -1.0F}).at(side);
    }

    // Runs `steps` updates of 1/30 s.
    void steps(int count) {
        for (int i = 0; i < count; ++i) {
            engine->update(1000.0F / 30.0F, listeners);
        }
    }
};

} // namespace

TEST_CASE("a 2D bank sound takes the first SPU2 voice, at the record's volume times the sound volume", "[audio]") {
    Rig rig;
    const SoundHandle sound = rig.engine->play(kBankSound);
    REQUIRE(sound.valid());
    CHECK(rig.engine->voiceOf(sound) == 13);
    CHECK_FALSE(rig.engine->isVirtual(sound));
    CHECK(rig.sent(sound, 0) == Approx(0.9F));
    CHECK(rig.sent(sound, 1) == Approx(0.9F));
    // The sample decodes to 256 (nibble 1, shift 4); the device level is int(0.9 x 16383) = 14744 of 16383.
    const auto out = coney::test::pull(rig.mixer, 4);
    CHECK(out[2] == Approx(256.0 * 14744.0 / 16383.0).margin(1.0));
    CHECK(rig.engine->play(kBankSound).valid());
    CHECK(rig.engine->voiceOf(rig.engine->play(kBankSound)) == 15);
}

TEST_CASE("an unknown sound plays nothing, and a bank sound the bank lacks plays virtually", "[audio]") {
    Rig rig;
    CHECK_FALSE(rig.engine->play(0x12345).valid());
    CHECK(rig.engine->stats().unknown == 1);
    const SoundHandle absent = rig.engine->play(kAbsentSample);
    REQUIRE(absent.valid());
    CHECK(rig.engine->isVirtual(absent));
    CHECK(rig.engine->stats().missingSamples == 1);
}

TEST_CASE("a virtual sound lasts the game's whole-second length, then is freed", "[audio]") {
    Rig rig;
    // 16,000 bytes at 8 kHz: floor(56,000 / 16,000) = 3 s.
    REQUIRE(rig.engine->loadBank("none").has_value());
    const SoundHandle sound = rig.engine->play(kShortSound);
    REQUIRE(rig.engine->isVirtual(sound));
    rig.steps(89);
    CHECK(rig.engine->isPlaying(sound));
    rig.steps(2);
    CHECK_FALSE(rig.engine->isPlaying(sound));
}

TEST_CASE("only five priority-21 sounds start in one update", "[audio]") {
    Rig rig;
    for (int i = 0; i < 5; ++i) {
        CHECK(rig.engine->play(kLowestSound).valid());
    }
    CHECK_FALSE(rig.engine->play(kLowestSound).valid());
    CHECK(rig.engine->stats().refused == 1);
    rig.steps(1);
    CHECK(rig.engine->play(kLowestSound).valid());
}

TEST_CASE("a positional sound fades with distance squared and pans between two ears", "[audio]") {
    Rig rig;
    // To the listener's right at 11 m: a = (1 - 9/18)^2 = 0.25; the right ear is nearer, the left gets
    // 0.75 a + 1 - |dL - dR| = 0.1875.
    const SoundHandle side = rig.engine->play(kBank3DSound, SoundPlay{.position = SoundVec{11.0F, 0.0F, 0.0F}});
    CHECK(rig.sent(side, 1) == Approx(0.25F * 0.9F));
    CHECK(rig.sent(side, 0) == Approx(0.25F * 0.1875F * 0.9F));
    // Within near: full volume on both ears when straight ahead.
    const SoundHandle close = rig.engine->play(kBank3DSound, SoundPlay{.position = SoundVec{0.0F, 0.0F, 1.0F}});
    CHECK(rig.sent(close, 0) == Approx(0.9F));
    CHECK(rig.sent(close, 1) == Approx(0.9F));
    // Moving it beyond far silences it.
    rig.engine->setPosition(close, SoundVec{0.0F, 0.0F, 25.0F});
    rig.steps(1);
    CHECK(rig.sent(close, 0) == 0.0F);
    // A one-shot beyond far + 10 m plays virtually.
    CHECK(rig.engine->isVirtual(rig.engine->play(kBank3DSound, SoundPlay{.position = SoundVec{0.0F, 0.0F, 31.0F}})));
}

TEST_CASE("streams take channels 5-9, small loops 10-12, a stereo bed a pair", "[audio]") {
    Rig rig;
    for (int channel = 5; channel <= 9; ++channel) {
        CHECK(rig.engine->voiceOf(rig.engine->play(kStreamSound)) == channel);
    }
    // No free channel, and the others are as important: virtual.
    CHECK(rig.engine->isVirtual(rig.engine->play(kStreamSound)));
    CHECK(rig.engine->voiceOf(rig.engine->play(kLoopSound)) == 10);

    Rig fresh;
    const SoundHandle bed = fresh.engine->play(kStereoSound);
    CHECK(fresh.engine->voiceOf(bed) == 5);
    CHECK(fresh.engine->voiceOf(fresh.engine->play(kStreamSound)) == 7);
}

TEST_CASE("with every SPU2 voice busy a more important sound steals the quietest of the least important", "[audio]") {
    Rig rig;
    std::vector<SoundHandle> sounds;
    sounds.reserve(35);
    for (int i = 0; i < 35; ++i) {
        // The tenth is the quietest.
        sounds.push_back(rig.engine->play(kBankSound, SoundPlay{.volume = i == 9 ? 0.1F : 1.0F}));
    }
    CHECK(rig.engine->voiceOf(sounds.back()) == 47);
    // As important: no victim.
    CHECK(rig.engine->isVirtual(rig.engine->play(kBankSound)));
    // More important: takes the tenth's voice.
    const SoundHandle important = rig.engine->play(kImportantSound);
    CHECK(rig.engine->voiceOf(important) == 22);
    CHECK_FALSE(rig.engine->isPlaying(sounds[9]));
    CHECK(rig.engine->stats().stolen == 1);
}

TEST_CASE("fades run over the engine's clock, and a fade out ends the sound", "[audio]") {
    Rig rig;
    const SoundHandle sound = rig.engine->play(kBankSound, SoundPlay{.fadeInMs = 1000.0F});
    CHECK(rig.sent(sound, 0) == 0.0F);
    rig.steps(15);
    CHECK(rig.sent(sound, 0) == Approx(0.45F).margin(0.001));
    rig.steps(15);
    CHECK(rig.sent(sound, 0) == Approx(0.9F));
    rig.engine->stop(sound, 500.0F);
    rig.steps(14);
    CHECK(rig.engine->isPlaying(sound));
    rig.steps(2);
    CHECK_FALSE(rig.engine->isPlaying(sound));
}

TEST_CASE("a non-duckable sound ducks the directional sounds of others", "[audio]") {
    Rig rig;
    const SoundHandle voice = rig.engine->play(kVoiceSound, SoundPlay{.position = SoundVec{0.0F, 0.0F, 0.5F}});
    rig.steps(1);
    CHECK(rig.sent(voice, 0) == Approx(0.9F));
    const SoundHandle loud = rig.engine->play(kBankSound, SoundPlay{.duckable = false});
    rig.steps(1);
    CHECK(rig.sent(voice, 0) == Approx(0.9F * 0.2F));
    // A player's own lines are not ducked.
    rig.engine->setPlayerOwners({7});
    const SoundHandle own =
        rig.engine->play(kVoiceSound, SoundPlay{.owner = 7, .position = SoundVec{0.0F, 0.0F, 0.5F}});
    rig.steps(1);
    CHECK(rig.sent(own, 0) == Approx(0.9F));
    rig.engine->stop(loud);
    rig.steps(1);
    CHECK(rig.sent(voice, 0) == Approx(0.9F));
}

TEST_CASE("the load screen loads load_NN in turn and its two halves, and plays positional sounds virtually",
          "[audio]") {
    Rig rig;
    rig.engine->startLoadScreen(false);
    REQUIRE(rig.loads.size() == 2);
    const std::string first = rig.loads.back();
    CHECK(first.starts_with("load_0"));
    CHECK(rig.engine->bankName() == first);
    CHECK(rig.engine->isVirtual(rig.engine->play(kBank3DSound, SoundPlay{.position = SoundVec{0.0F, 0.0F, 1.0F}})));
    CHECK_FALSE(rig.engine->isVirtual(rig.engine->play(kBankSound)));
    rig.engine->endLoadScreen();
    CHECK(rig.engine->bankName() == "sound");
    rig.engine->startLoadScreen(false);
    const int number = first.back() - '0';
    CHECK(rig.loads.back() == std::format("load_0{}", (number + 1) % 7));
    // A bank a script asked for while loads were deferred loads at the end instead of `sound`.
    rig.engine->setDeferBankLoads(true);
    REQUIRE(rig.engine->loadBank("armies").has_value());
    CHECK(rig.engine->pendingBank() == "armies");
    rig.engine->endLoadScreen();
    CHECK(rig.engine->bankName() == "armies");
    CHECK(rig.engine->pendingBank() == "none");
}

TEST_CASE("pausing holds the sounds playing, the pause menu's own play, and resuming frees them", "[audio]") {
    Rig rig;
    rig.engine->play(kBankSound);
    CHECK(coney::test::pull(rig.mixer, 1)[0] != 0);
    rig.engine->pause();
    CHECK(coney::test::pull(rig.mixer, 1)[0] == 0);
    const SoundHandle cue = rig.engine->play(kImportantSound);
    const auto alone = coney::test::pull(rig.mixer, 1)[0];
    CHECK(alone != 0);
    rig.engine->resume();
    CHECK(coney::test::pull(rig.mixer, 1)[0] > alone);
    CHECK(rig.engine->isPlaying(cue));
}

TEST_CASE("a scene soundtrack is prepared silent, starts on its event and ducks the music", "[audio]") {
    Rig rig;
    const SoundHandle scene = rig.engine->preloadSceneSound(kStereoSound);
    REQUIRE(scene.valid());
    rig.steps(1);
    CHECK(coney::test::pull(rig.mixer, 8)[0] == 0);
    rig.engine->startSceneSound();
    CHECK(coney::test::pull(rig.mixer, 8)[0] != 0);
    rig.engine->stopSceneSound();
    CHECK_FALSE(rig.engine->isPlaying(scene));
}

TEST_CASE("the ambient bed fades in, and a new one replaces it", "[audio]") {
    Rig rig;
    rig.engine->playAmbientTrack(kLoopSound);
    rig.steps(30);
    rig.engine->playAmbientTrack(kLoopSound); // the same: kept
    CHECK(rig.engine->stats().tasks == 1);
    rig.engine->playAmbientTrack(kStreamSound);
    CHECK(rig.engine->stats().tasks == 2); // the old one fades out over 2 s
    rig.steps(61);
    CHECK(rig.engine->stats().tasks == 1);
}

TEST_CASE("the sound player plays the game's sounds through the engine and registered sounds first", "[audio]") {
    Mixer mixer;
    coney::audio::SoundPlayer player(mixer);
    std::vector<std::string> loads;
    player.attach(
        std::make_unique<SoundEngine>(mixer, testTables(), nullptr, testBanks(loads), SoundEngine::RandomRange{}));
    REQUIRE(player.engine()->loadBank("menu").has_value());
    CHECK(player.has(kBankSound));
    const auto sound = player.play(kBankSound);
    REQUIRE(sound.valid());
    CHECK(player.isPlaying(sound));
    CHECK_FALSE(player.play(0x4242).valid());
    CHECK(player.missing() == 1);
    // Without sound files a streamed sound plays virtually but is still a sound.
    CHECK(player.play(kStreamSound).valid());
    player.stop(sound);
    player.update(1000.0F / 30.0F);
    CHECK_FALSE(player.isPlaying(sound));
    // A positional sound.
    const auto there = player.play3D(kBank3DSound, SoundVec{3.0F, 0.0F, 0.0F});
    CHECK(player.isPlaying(there));
}
