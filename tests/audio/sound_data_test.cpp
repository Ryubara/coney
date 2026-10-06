// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_data.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "audio/sound_bank.h"
#include "audio/sound_stream.h"
#include "audio_fixtures.h"

using coney::ErrorCode;
using coney::audio::SoundBank;
using coney::audio::SoundTables;
using coney::test::Bytes;
using coney::test::TestSound;

TEST_CASE("the rate table steps by 750 Hz with the common rates slotted in", "[audio]") {
    CHECK(coney::audio::sampleRateOf(1) == 750);
    CHECK(coney::audio::sampleRateOf(11) == 8000);
    CHECK(coney::audio::sampleRateOf(17) == 11025);
    CHECK(coney::audio::sampleRateOf(25) == 16000);
    CHECK(coney::audio::sampleRateOf(34) == 22050);
    CHECK(coney::audio::sampleRateOf(36) == 22500);
    CHECK(coney::audio::sampleRateOf(65) == 44100);
    CHECK(coney::audio::sampleRateOf(71) == 48000);
    CHECK(coney::audio::sampleRateOf(73) == 0);
}

TEST_CASE("the sound list parses sorted records and is searched by hash", "[audio]") {
    const std::array<TestSound, 3> sounds{TestSound{.hash = 10, .size = 32, .offset = 2048, .soundClass = 1},
                                          TestSound{.hash = 20, .volume = 40, .pitchVariation = 5},
                                          TestSound{.hash = 30, .rateIndex = 36}};
    auto list = SoundTables::parseSoundList(coney::test::soundListChunk(sounds));
    REQUIRE(list.has_value());
    REQUIRE(list->size() == 3);
    CHECK((*list)[0].offset == 2048);
    CHECK((*list)[1].volume == 40);
    CHECK((*list)[1].pitchVariation == 5);
    CHECK((*list)[2].sampleRate() == 22500);
    const SoundTables tables(std::move(*list),
                             {coney::test::testClass(0, 0, 0, 2), coney::test::testClass(1, 10, 6, 4)}, {}, {});
    REQUIRE(tables.find(20) != nullptr);
    CHECK(tables.find(20)->volume == 40);
    CHECK(tables.find(25) == nullptr);
    const auto* first = tables.find(10);
    REQUIRE(first != nullptr);
    REQUIRE(tables.classOf(*first) != nullptr);
    CHECK(tables.classOf(*first)->streamed());

    const std::array<TestSound, 2> unsorted{TestSound{.hash = 5}, TestSound{.hash = 4}};
    auto bad = SoundTables::parseSoundList(coney::test::soundListChunk(unsorted));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().code == ErrorCode::Invalid);
    auto cut = Bytes().u32(2).fill(16, 0);
    CHECK(SoundTables::parseSoundList(cut.span()).error().code == ErrorCode::Truncated);
}

TEST_CASE("sound classes are five bytes with far in fives, ended by padding", "[audio]") {
    auto chunk = Bytes().u8(2).u8(20).u8(0x1c).u8(9).u8(1).u8(0).u8(0).u8(0x05).u8(21).u8(2).fill(10, 0);
    const auto classes = SoundTables::parseSoundClasses(chunk.span());
    REQUIRE(classes.size() == 2);
    CHECK(classes[0].near == 2);
    CHECK(classes[0].far == 100);
    CHECK(classes[0].directional());
    CHECK(classes[0].positional());
    CHECK(classes[0].streamed());
    CHECK(classes[1].loops());
    CHECK_FALSE(classes[1].positional());
    CHECK(classes[1].stereo());
}

TEST_CASE("the stereo table and the music list parse", "[audio]") {
    auto stereo = Bytes().u32(1).fill(12, 0).u32(77).u32(0x8000).u32(0x1000).u32(3);
    auto layouts = SoundTables::parseStereoTable(stereo.span());
    REQUIRE(layouts.has_value());
    REQUIRE(layouts->size() == 1);
    CHECK((*layouts)[0].hash == 77);
    CHECK((*layouts)[0].interleave == 0x8000);
    CHECK((*layouts)[0].lastBlock == 0x1000);
    CHECK((*layouts)[0].blocks == 3);

    Bytes music;
    music.u32(1);
    music.u32(0x3f800000); // 1.0
    music.u32(32000).u32(64).u32(32).u32(4096).u32(2).u32(0x800).u32(5).u32(0x400).u32(99);
    music.text("music/test_track");
    music.padTo(4 + 104);
    auto tracks = SoundTables::parseMusicList(music.span());
    REQUIRE(tracks.has_value());
    REQUIRE(tracks->size() == 1);
    const auto& track = (*tracks)[0];
    CHECK(track.volume == 1.0F);
    CHECK(track.sampleRate == 32000);
    CHECK(track.offset == 4096);
    CHECK(track.interleave == 0x800);
    CHECK(track.blocks == 5);
    CHECK(track.lastBlock == 0x400);
    CHECK(track.hash == 99);
    CHECK(track.name == "music/test_track");
    CHECK(track.size() == 2ULL * 0x800 * 5);
    CHECK(SoundTables::parseMusicList(Bytes().u32(2).fill(104, 0).span()).error().code == ErrorCode::Truncated);
}

TEST_CASE("a bank decodes each indexed sample to its end flag, by the sound list's size and rate", "[audio]") {
    // Two samples back to back in the .msb: 2 frames (end flag on the second) then 3 frames.
    const auto first = coney::test::constantAdpcm(3, 2, 12, coney::audio::kAdpcmEnd);
    const auto second = coney::test::constantAdpcm(-2, 3, 12, coney::audio::kAdpcmEnd);
    Bytes msb;
    msb.append(first).append(second);
    const auto msd = Bytes().u32(100).u32(0).u32(200).u32(32).u32(300).u32(0).u32(0).u32(0).u32(400).u32(0);
    const std::array<TestSound, 3> sounds{TestSound{.hash = 100, .size = 32, .soundClass = 0, .rateIndex = 34},
                                          TestSound{.hash = 200, .size = 48, .offset = 32, .soundClass = 1},
                                          TestSound{.hash = 300, .size = 4096}};
    auto list = SoundTables::parseSoundList(coney::test::soundListChunk(sounds));
    REQUIRE(list.has_value());
    const SoundTables tables(std::move(*list), {coney::test::testClass(0, 0, 0, 2), coney::test::testClass(0, 0, 1, 2)},
                             {}, {});
    const SoundBank bank = SoundBank::decode("test", msd.span(), msb.span(), tables);
    CHECK(bank.name() == "test");
    CHECK(bank.size() == 2);
    CHECK(bank.skipped() == 1); // 300's size runs past the .msb; 400 is after the zero pair
    const auto one = bank.find(100);
    REQUIRE(one != nullptr);
    CHECK(one->frames() == 56);
    CHECK(one->sampleRate() == 22050);
    CHECK(one->samples()[0] == 3);
    CHECK_FALSE(one->loop().has_value());
    const auto two = bank.find(200);
    REQUIRE(two != nullptr);
    CHECK(two->samples()[0] == -2);
    REQUIRE(two->loop().has_value()); // a looping class loops over the whole sample
    CHECK(two->loop().value_or(coney::audio::LoopPoints{}).end == 84);
    CHECK(bank.find(300) == nullptr);
}

TEST_CASE("a stream feeder decodes a mono sound in parts and a stereo one block by block", "[audio]") {
    coney::test::MemorySoundFiles files;
    // A mono sound of 1,000 frames (16,000 bytes: two 8 KiB parts) at offset 2048.
    files.sounds.resize(2048);
    const auto mono = coney::test::constantAdpcm(5, 1000);
    files.sounds.insert(files.sounds.end(), mono.begin(), mono.end());
    auto feeder = coney::audio::StreamFeeder::create(
        coney::audio::StreamLayout::mono(coney::audio::SoundFile::Sounds, 2048, 16000, false), 22050, 100'000);
    REQUIRE(feeder.has_value());
    REQUIRE((*feeder)->pump(files).has_value());
    CHECK((*feeder)->done());
    CHECK(files.reads == 2);
    auto& stream = *(*feeder)->stream();
    std::size_t frames = 0;
    while (const auto frame = stream.readFrame()) {
        CHECK((*frame)[0] == 5);
        ++frames;
    }
    CHECK(frames == 28'000);
    CHECK(stream.drained());

    // Stereo: 2 blocks of 32 bytes per channel, left block then right; the stream ends 16 bytes into the last block.
    Bytes stereo;
    stereo.append(coney::test::constantAdpcm(1, 2)).append(coney::test::constantAdpcm(-1, 2));
    stereo.append(coney::test::constantAdpcm(2, 2)).append(coney::test::constantAdpcm(-2, 2));
    files.music = stereo.data();
    const coney::audio::StreamLayout layout{.file = coney::audio::SoundFile::Music,
                                            .offset = 0,
                                            .channels = 2,
                                            .interleave = 32,
                                            .blocks = 2,
                                            .lastBlock = 16};
    auto pair = coney::audio::StreamFeeder::create(layout, 32000, 1000);
    REQUIRE(pair.has_value());
    REQUIRE((*pair)->pump(files).has_value());
    std::vector<std::array<std::int16_t, 2>> out;
    while (const auto frame = (*pair)->stream()->readFrame()) {
        out.push_back(*frame);
    }
    REQUIRE(out.size() == 56 + 28);
    CHECK(out.front() == std::array<std::int16_t, 2>{1, -1});
    CHECK(out.back() == std::array<std::int16_t, 2>{2, -2});
}

TEST_CASE("a looping stream starts again, and a full stream waits for its voice", "[audio]") {
    coney::test::MemorySoundFiles files;
    files.sounds = coney::test::constantAdpcm(4, 1);
    auto feeder = coney::audio::StreamFeeder::create(
        coney::audio::StreamLayout::mono(coney::audio::SoundFile::Sounds, 0, 16, true), 8000, 70);
    REQUIRE(feeder.has_value());
    REQUIRE((*feeder)->pump(files).has_value());
    CHECK_FALSE((*feeder)->done());
    CHECK((*feeder)->ready());
    CHECK((*feeder)->stream()->freeFrames() == 0);
    for (int i = 0; i < 40; ++i) {
        CHECK((*feeder)->stream()->readFrame().has_value());
    }
    REQUIRE((*feeder)->pump(files).has_value());
    CHECK((*feeder)->stream()->freeFrames() == 0);

    // A read past the file ends the stream and reports the failure.
    auto broken = coney::audio::StreamFeeder::create(
        coney::audio::StreamLayout::mono(coney::audio::SoundFile::Sounds, 1000, 16, false), 8000, 70);
    REQUIRE(broken.has_value());
    CHECK_FALSE((*broken)->pump(files).has_value());
    CHECK((*broken)->done());
}
