// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/mixer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "audio/audio_format.h"
#include "audio/offline_device.h"
#include "audio/pcm_sound.h"
#include "audio/pcm_stream.h"
#include "audio/sound_player.h"
#include "audio/spsc_queue.h"
#include "audio/test_tone.h"
#include "core/name_hash.h"

using coney::audio::Bus;
using coney::audio::kOutputRate;
using coney::audio::LoopPoints;
using coney::audio::Mixer;
using coney::audio::PcmSound;
using coney::audio::PcmStream;
using coney::audio::VoiceParams;

// Every signal here is synthesised in the test: constants, ramps and sines. No game data.

namespace {

// A sound from `samples` at the output rate (so a voice plays it one frame per output frame), shared as the mixer
// takes it.
std::shared_ptr<const PcmSound> soundOf(std::vector<std::int16_t> samples, int channels = 1, int rate = kOutputRate,
                                        std::optional<LoopPoints> loop = std::nullopt) {
    auto sound = PcmSound::create(std::move(samples), channels, rate, loop);
    REQUIRE(sound.has_value());
    return std::make_shared<const PcmSound>(std::move(*sound));
}

// A mono sound of `frames` copies of `value`.
std::shared_ptr<const PcmSound> constant(std::int16_t value, std::size_t frames = 64) {
    return soundOf(std::vector<std::int16_t>(frames, value));
}

// Mixes `frames` frames and returns them, interleaved.
std::vector<std::int16_t> mixFrames(Mixer& mixer, std::size_t frames) {
    std::vector<std::int16_t> out(frames * 2);
    mixer.mix(out);
    return out;
}

} // namespace

TEST_CASE("a sound's shape is checked when it is made", "[audio]") {
    CHECK_FALSE(PcmSound::create({}, 1, 48000).has_value());
    CHECK_FALSE(PcmSound::create({1, 2, 3}, 2, 48000).has_value());
    CHECK_FALSE(PcmSound::create({1, 2}, 3, 48000).has_value());
    CHECK_FALSE(PcmSound::create({1, 2}, 1, 0).has_value());
    CHECK_FALSE(PcmSound::create({1, 2}, 1, 48000, LoopPoints{.start = 1, .end = 1}).has_value());
    CHECK_FALSE(PcmSound::create({1, 2}, 1, 48000, LoopPoints{.start = 0, .end = 3}).has_value());
    auto stereo = PcmSound::create({1, 2, 3, 4}, 2, 22050, LoopPoints{.start = 0, .end = 2});
    REQUIRE(stereo.has_value());
    CHECK(stereo->frames() == 2);
    CHECK(stereo->sampleRate() == 22050);
}

TEST_CASE("a voice at unity plays its sound sample for sample, then ends", "[audio]") {
    Mixer mixer;
    const auto voice = mixer.play(soundOf({100, -200, 300, -400}));
    CHECK(mixer.isPlaying(voice)); // queued
    const std::vector<std::int16_t> out = mixFrames(mixer, 6);
    CHECK(out == std::vector<std::int16_t>{100, 100, -200, -200, 300, 300, -400, -400, 0, 0, 0, 0});
    CHECK_FALSE(mixer.isPlaying(voice));
    CHECK(mixer.voicesPlaying() == 0);
    CHECK(mixer.held() == 1);
    mixer.collect();
    CHECK(mixer.held() == 0);
}

TEST_CASE("voices sum, and a loud sum clips at 16 bits instead of wrapping", "[audio]") {
    Mixer mixer;
    mixer.play(constant(1000));
    mixer.play(constant(-300));
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{700, 700});
    mixer.stopAll();
    mixer.play(constant(30000));
    mixer.play(constant(30000));
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{32767, 32767});
    mixer.stopAll();
    mixer.play(constant(-30000));
    mixer.play(constant(-30000));
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{-32768, -32768});
}

TEST_CASE("volume scales linearly in the device's steps and pan is a balance with full volume at the centre",
          "[audio]") {
    Mixer mixer;
    // A volume reaches a voice as the game sends it to its device, int(v x 16383) of 16383: 0.5 is 8191, a hair under.
    mixer.play(constant(10000), VoiceParams{.volume = 0.5F});
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{4999, 4999});
    mixer.stopAll();
    mixer.play(constant(10000), VoiceParams{.pan = -1.0F});
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{10000, 0});
    mixer.stopAll();
    mixer.play(constant(10000), VoiceParams{.pan = 0.5F});
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{4999, 10000});
    mixer.stopAll();
    // Out-of-range and NaN values are clamped, never passed to the integer path.
    const auto loud = mixer.play(constant(10000), VoiceParams{.volume = 4.0F, .pan = 7.0F});
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{0, 10000});
    mixer.setVolume(loud, std::nanf(""));
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{0, 0});
}

TEST_CASE("a stereo sound keeps its sides, each scaled by the pan", "[audio]") {
    Mixer mixer;
    mixer.play(soundOf({1000, -2000, 1000, -2000}, 2), VoiceParams{.pan = 0.5F});
    CHECK(mixFrames(mixer, 2) == std::vector<std::int16_t>{499, -2000, 499, -2000});
}

TEST_CASE("bus volumes and the master volume scale the voices under them", "[audio]") {
    Mixer mixer;
    mixer.play(constant(8000), VoiceParams{.bus = Bus::Sfx});
    mixer.play(constant(4000), VoiceParams{.bus = Bus::Music});
    mixer.setBusVolume(Bus::Music, 0.5F);
    CHECK(mixer.busVolume(Bus::Music) == 0.5F);
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{10000, 10000});
    mixer.setMasterVolume(0.25F);
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{2500, 2500});
    mixer.setBusVolume(Bus::Sfx, 0.0F);
    mixer.setMasterVolume(1.0F);
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{2000, 2000});
}

TEST_CASE("the resampler interpolates linearly between source frames at any rate and pitch", "[audio]") {
    Mixer mixer;
    // Half the output rate: each source frame lasts two output frames, with the midpoint between.
    mixer.play(soundOf({0, 1000, 2000, 3000}, 1, kOutputRate / 2));
    std::vector<std::int16_t> out = mixFrames(mixer, 6);
    CHECK(out == std::vector<std::int16_t>{0, 0, 500, 500, 1000, 1000, 1500, 1500, 2000, 2000, 2500, 2500});
    mixer.stopAll();
    // Pitch 2 at the output rate skips every other frame.
    mixer.play(soundOf({0, 10, 20, 30, 40, 50}), VoiceParams{.pitch = 2.0F});
    out = mixFrames(mixer, 4);
    CHECK(out == std::vector<std::int16_t>{0, 0, 20, 20, 40, 40, 0, 0});
}

TEST_CASE("a resampled sine keeps its frequency and level", "[audio]") {
    // 1 kHz at 22,050 Hz, resampled to 48 kHz: the output must match the ideal sine closely.
    constexpr int kRate = 22050;
    constexpr double kHz = 1000.0;
    std::vector<std::int16_t> sine(kRate / 10);
    for (std::size_t i = 0; i < sine.size(); ++i) {
        sine[i] = static_cast<std::int16_t>(
            std::lround(16000.0 * std::sin(2.0 * std::numbers::pi * kHz * static_cast<double>(i) / kRate)));
    }
    Mixer mixer;
    mixer.play(soundOf(std::move(sine), 1, kRate));
    const std::vector<std::int16_t> out = mixFrames(mixer, 4000);
    double worst = 0.0;
    for (std::size_t f = 0; f < 4000; ++f) {
        const double ideal = 16000.0 * std::sin(2.0 * std::numbers::pi * kHz * static_cast<double>(f) / kOutputRate);
        worst = std::max(worst, std::abs(static_cast<double>(out[f * 2]) - ideal));
    }
    // Linear interpolation's error for 1 kHz at 22 kHz is under 1.1% of the peak.
    CHECK(worst < 200.0);
}

TEST_CASE("a sound with loop points plays its start once, then repeats the loop", "[audio]") {
    Mixer mixer;
    const auto voice = mixer.play(soundOf({1, 2, 3, 4, 5}, 1, kOutputRate, LoopPoints{.start = 2, .end = 4}));
    std::vector<std::int16_t> left;
    for (std::size_t i = 0; i < 10; i += 2) {
        left.push_back(mixFrames(mixer, 1)[0]);
        left.push_back(mixFrames(mixer, 1)[0]);
    }
    CHECK(left == std::vector<std::int16_t>{1, 2, 3, 4, 3, 4, 3, 4, 3, 4});
    CHECK(mixer.isPlaying(voice));
    mixer.stop(voice);
    mixFrames(mixer, 1);
    CHECK_FALSE(mixer.isPlaying(voice));
}

TEST_CASE("a paused voice holds its place and resumes from it", "[audio]") {
    Mixer mixer;
    const auto voice = mixer.play(soundOf({1, 2, 3, 4}), VoiceParams{.paused = true});
    CHECK(mixFrames(mixer, 2) == std::vector<std::int16_t>{0, 0, 0, 0});
    CHECK(mixer.isPlaying(voice));
    mixer.setPaused(voice, false);
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{1, 1});
    mixer.setPaused(voice, true);
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{0, 0});
    mixer.setPaused(voice, false);
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{2, 2});
}

TEST_CASE("pauseAll holds the voices playing then, plays later ones, and resumeAll frees only those", "[audio]") {
    Mixer mixer;
    const auto before = mixer.play(soundOf({1, 2, 3, 4}));
    const auto pausedAlone = mixer.play(constant(1000), VoiceParams{.paused = true});
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{1, 1});
    // The pause menu's cue, played while everything else is held.
    mixer.pauseAll();
    const auto cue = mixer.play(soundOf({10, 20, 30, 40}));
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{10, 10});
    CHECK(mixer.isPlaying(before));
    mixer.resumeAll();
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{22, 22});
    CHECK(mixer.isPlaying(cue));
    CHECK(mixer.isPlaying(pausedAlone));
}

TEST_CASE("with every voice busy, a play steals the lowest priority, oldest first, or is dropped", "[audio]") {
    Mixer mixer;
    const auto looping = soundOf({100, 100}, 1, kOutputRate, LoopPoints{.start = 0, .end = 2});
    std::vector<coney::audio::VoiceHandle> voices;
    voices.reserve(coney::audio::kVoiceCount);
    for (std::size_t i = 0; i < coney::audio::kVoiceCount; ++i) {
        // The first voice has priority 1, the rest 2.
        voices.push_back(mixer.play(looping, VoiceParams{.priority = static_cast<std::uint8_t>(i == 0 ? 1 : 2)}));
    }
    mixFrames(mixer, 1);
    CHECK(mixer.voicesPlaying() == coney::audio::kVoiceCount);
    // Priority 0 finds nothing it may take.
    const auto low = mixer.play(looping, VoiceParams{.priority = 0});
    mixFrames(mixer, 1);
    CHECK_FALSE(mixer.isPlaying(low));
    CHECK(mixer.stats().playsDropped == 1);
    // Priority 2 takes the one priority-1 voice, then the oldest of the priority-2 ones.
    const auto first = mixer.play(looping, VoiceParams{.priority = 2});
    mixFrames(mixer, 1);
    CHECK(mixer.isPlaying(first));
    CHECK_FALSE(mixer.isPlaying(voices[0]));
    const auto second = mixer.play(looping, VoiceParams{.priority = 2});
    mixFrames(mixer, 1);
    CHECK(mixer.isPlaying(second));
    CHECK_FALSE(mixer.isPlaying(voices[1]));
    CHECK(mixer.isPlaying(voices[2]));
    CHECK(mixer.stats().voicesStolen == 2);
    mixer.collect();
    CHECK(mixer.held() == coney::audio::kVoiceCount);
}

TEST_CASE("a stream plays what is written, silence on an underrun, and ends once finished", "[audio]") {
    auto created = PcmStream::create(1, kOutputRate, 8);
    REQUIRE(created.has_value());
    const std::shared_ptr<PcmStream>& stream = *created;
    const std::array<std::int16_t, 3> first{10, 20, 30};
    CHECK(stream->write(first) == 3);
    Mixer mixer;
    const auto voice = mixer.play(stream);
    std::vector<std::int16_t> out = mixFrames(mixer, 4);
    CHECK(out == std::vector<std::int16_t>{10, 10, 20, 20, 30, 30, 0, 0});
    CHECK(stream->underruns() > 0);
    CHECK(mixer.isPlaying(voice));
    const std::array<std::int16_t, 2> second{40, 50};
    stream->write(second);
    stream->finish();
    out = mixFrames(mixer, 4);
    CHECK(out[4] == 40); // after the two silent frames the underrun left between the play position and the ring
    CHECK(out[6] == 50);
    CHECK_FALSE(mixer.isPlaying(voice));
    // The ring holds at most its capacity.
    const std::vector<std::int16_t> many(20, 1);
    auto small = PcmStream::create(1, kOutputRate, 8);
    REQUIRE(small.has_value());
    CHECK((*small)->write(many) == 8);
    CHECK((*small)->freeFrames() == 0);
}

TEST_CASE("offline pulls are deterministic: the same commands give the same samples", "[audio]") {
    // Two mixers, the same tone sweep, the same pulls in different block sizes: identical output and hash.
    const auto sweep = std::make_shared<const PcmSound>(coney::audio::makeToneSweep());
    Mixer a;
    Mixer b;
    coney::audio::OfflineDevice deviceA(a);
    coney::audio::OfflineDevice deviceB(b);
    a.play(sweep, VoiceParams{.pan = -0.25F, .pitch = 1.5F});
    b.play(sweep, VoiceParams{.pan = -0.25F, .pitch = 1.5F});
    for (int step = 0; step < 90; ++step) {
        deviceA.pullStep();
    }
    deviceB.pull(1000);
    deviceB.pull(coney::audio::kFramesPerStep * 90 - 1000);
    CHECK(deviceA.frames() == deviceB.frames());
    CHECK(deviceA.hash() == deviceB.hash());
    CHECK(deviceA.peak() > 10000);
    CHECK(deviceA.peak() <= 32768);
    // The sweep loops, so it still plays after three seconds' worth at pitch 1.5.
    CHECK(a.voicesPlaying() == 1);
}

TEST_CASE("commands cross from a game thread to a mix thread without loss", "[audio]") {
    // One thread plays and stops voices while another mixes, as SDL's audio thread would; the counts must agree.
    Mixer mixer;
    const auto sound = constant(1, 4);
    constexpr int kPlays = 5000;
    std::atomic<bool> done{false};
    std::thread mixThread([&mixer, &done] {
        std::array<std::int16_t, 64> block{};
        while (!done.load(std::memory_order_acquire)) {
            mixer.mix(block);
            std::this_thread::yield();
        }
        mixer.mix(block); // whatever was queued last
    });
    int accepted = 0;
    for (int i = 0; i < kPlays; ++i) {
        const auto voice = mixer.play(sound, VoiceParams{.priority = 255});
        if (voice.valid()) {
            ++accepted;
            mixer.setVolume(voice, 0.5F);
        }
        if (i % 64 == 0) {
            mixer.collect();
            std::this_thread::yield();
        }
    }
    done.store(true, std::memory_order_release);
    mixThread.join();
    mixer.collect();
    const coney::audio::MixerStats stats = mixer.stats();
    CHECK(accepted + static_cast<int>(stats.commandsDropped) >= kPlays);
    CHECK(mixer.voicesPlaying() == 0);
    CHECK(mixer.held() == 0);
}

TEST_CASE("the single-producer queue keeps order and refuses past its capacity", "[audio]") {
    coney::audio::SpscQueue<int, 4> queue;
    CHECK(queue.push(1));
    CHECK(queue.push(2));
    CHECK(queue.push(3));
    CHECK_FALSE(queue.push(4));
    CHECK(queue.pop() == 1);
    CHECK(queue.push(4));
    CHECK(queue.pop() == 2);
    CHECK(queue.pop() == 3);
    CHECK(queue.pop() == 4);
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("the sound player plays sounds by name hash and counts unknown ids", "[audio]") {
    Mixer mixer;
    coney::audio::SoundPlayer player(mixer);
    CHECK(coney::audio::soundIdOf("vags/test/beep") == coney::crc32("vags/test/beep"));
    const auto unknown = player.play("vags/test/beep");
    CHECK_FALSE(unknown.valid());
    CHECK(player.missing() == 1);
    player.add(coney::audio::soundIdOf("vags/test/beep"), constant(500, 4));
    CHECK(player.has(coney::audio::soundIdOf("vags/test/beep")));
    const auto voice = player.play("vags/test/beep", VoiceParams{.bus = Bus::Speech});
    REQUIRE(voice.valid());
    player.setBusVolume(Bus::Speech, 0.5F);
    CHECK(mixFrames(mixer, 1) == std::vector<std::int16_t>{250, 250});
    CHECK(player.isPlaying(voice));
    player.stop(voice);
    mixFrames(mixer, 1);
    CHECK_FALSE(player.isPlaying(voice));
}

TEST_CASE("the tone sweep loops over its whole length without a jump", "[audio]") {
    const PcmSound sweep = coney::audio::makeToneSweep();
    const std::optional<LoopPoints> loop = sweep.loop();
    REQUIRE(loop.has_value());
    CHECK(loop.value_or(LoopPoints{}).start == 0);
    CHECK(loop.value_or(LoopPoints{}).end == sweep.frames());
    const auto samples = sweep.samples();
    // The last sample leads smoothly into the first: no step larger than the sweep's largest within it.
    int largest = 0;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    }
    CHECK(std::abs(samples.front() - samples.back()) <= largest);
    CHECK(samples.front() == 0);
}

TEST_CASE("a voice takes two side volumes and a rate as the game's device does", "[audio]") {
    Mixer mixer;
    const auto voice = mixer.play(constant(16383));
    mixer.setStereoVolume(voice, 1.0F, 0.25F);
    const auto sides = mixFrames(mixer, 1);
    CHECK(sides[0] == 16383);
    CHECK(std::abs(sides[1] - 4095) <= 1); // int(0.25 x 16383) = 4095 of 16383

    // The rate goes through the SPU2 pitch word: 24,000 Hz is 2048 of 4096, half the output rate.
    CHECK(coney::audio::rateToPitch(48000.0F) == 4096);
    CHECK(coney::audio::rateToPitch(22050.0F) == 1881);
    CHECK(coney::audio::rateToPitch(1.0e6F) == 0x3fff);
    CHECK(coney::audio::rateToPitch(0.0F) == 1);
    Mixer slow;
    std::vector<std::int16_t> ramp(64);
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        ramp[i] = static_cast<std::int16_t>(i * 100);
    }
    const auto half = slow.play(soundOf(ramp));
    slow.setRate(half, 24000.0F);
    const auto out = mixFrames(slow, 4);
    CHECK(out == std::vector<std::int16_t>{0, 0, 50, 50, 100, 100, 150, 150});
}
