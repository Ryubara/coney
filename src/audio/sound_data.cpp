// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_data.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <utility>

#include "core/chunk_system.h"
#include "fileio/reader.h"
#include "fileio/wad.h"
#include "gamemodes/load_entry_mode.h"

namespace coney::audio {

namespace {

// The game's 73 sample rates (docs/research/formats/audio.md#sound-list): steps of 750 Hz with 8000, 11000, 11025,
// 16000, 16500, 22050, 22250, 44100 and 48000 slotted in, and an unused 9999 at the end.
constexpr std::array<std::uint16_t, 73> kSampleRates{
    0,     750,   1500,  2250,  3000,  3750,  4500,  5250,  6000,  6750,  7500,  8000,  8250,  9000,  9750,
    10500, 11000, 11025, 11250, 12000, 12750, 13500, 14250, 15000, 15750, 16000, 16500, 17250, 18000, 18750,
    19500, 20250, 21000, 21750, 22050, 22250, 22500, 23250, 24000, 24750, 25500, 26250, 27000, 27750, 28500,
    29250, 30000, 30750, 31500, 32250, 33000, 33750, 34500, 35250, 36000, 36750, 37500, 38250, 39000, 39750,
    40500, 41250, 42000, 42750, 43500, 44100, 44250, 45000, 45750, 46500, 47250, 48000, 9999};

constexpr std::size_t kSoundRecordBytes = 16;
constexpr std::size_t kClassRecordBytes = 5;
constexpr std::size_t kStereoRecordBytes = 16;
constexpr std::size_t kStereoHeaderBytes = 16;
constexpr std::size_t kMusicRecordBytes = 104;
constexpr std::size_t kMusicNameOffset = 40;
constexpr std::uint32_t kFarUnit = 5;

// The count at the start of a counted chunk, checked to fit `recordBytes` records after `headerBytes`.
std::expected<std::uint32_t, Error> countedRecords(std::span<const std::byte> data, std::size_t headerBytes,
                                                   std::size_t recordBytes, std::string_view what) {
    if (data.size() < 4) {
        return fail(ErrorCode::Truncated, std::format("the {} is too short for its count", what));
    }
    const std::uint32_t count = io::loadU32Le(data);
    if (headerBytes + std::size_t{count} * recordBytes > data.size()) {
        return fail(ErrorCode::Truncated, std::format("{} {} records of {} bytes do not fit in {} bytes", count, what,
                                                      recordBytes, data.size()));
    }
    return count;
}

// The byte at `data[at]`.
std::uint8_t byteAt(std::span<const std::byte> data, std::size_t at) { return static_cast<std::uint8_t>(data[at]); }

} // namespace

int sampleRateOf(std::uint8_t rateIndex) { return rateIndex < kSampleRates.size() ? kSampleRates.at(rateIndex) : 0; }

std::expected<std::vector<SoundRecord>, Error> SoundTables::parseSoundList(std::span<const std::byte> data) {
    auto count = countedRecords(data, 4, kSoundRecordBytes, "sound list");
    if (!count) {
        return std::unexpected(std::move(count.error()));
    }
    std::vector<SoundRecord> sounds;
    sounds.reserve(*count);
    for (std::size_t i = 0; i < *count; ++i) {
        const auto record = data.subspan(4 + i * kSoundRecordBytes, kSoundRecordBytes);
        sounds.push_back(SoundRecord{.size = io::loadU32Le(record),
                                     .offset = io::loadU32Le(record.subspan(4)),
                                     .hash = io::loadU32Le(record.subspan(8)),
                                     .pitchVariation = byteAt(record, 12),
                                     .volume = byteAt(record, 13),
                                     .rateIndex = byteAt(record, 14),
                                     .soundClass = byteAt(record, 15)});
        if (i > 0 && sounds[i - 1].hash > sounds[i].hash) {
            return fail(ErrorCode::Invalid, std::format("the sound list is not sorted by hash at record {}", i));
        }
    }
    return sounds;
}

std::vector<SoundClass> SoundTables::parseSoundClasses(std::span<const std::byte> data) {
    std::vector<SoundClass> classes;
    for (std::size_t at = 0; at + kClassRecordBytes <= data.size(); at += kClassRecordBytes) {
        const std::uint8_t channels = byteAt(data, at + 4);
        if (channels == 0) {
            break; // the chunk's padding
        }
        classes.push_back(SoundClass{.near = byteAt(data, at),
                                     .far = static_cast<std::uint16_t>(byteAt(data, at + 1) * kFarUnit),
                                     .flags = byteAt(data, at + 2),
                                     .priority = byteAt(data, at + 3),
                                     .channels = channels});
    }
    return classes;
}

std::expected<std::vector<StereoLayout>, Error> SoundTables::parseStereoTable(std::span<const std::byte> data) {
    auto count = countedRecords(data, kStereoHeaderBytes, kStereoRecordBytes, "stereo table");
    if (!count) {
        return std::unexpected(std::move(count.error()));
    }
    std::vector<StereoLayout> layouts;
    layouts.reserve(*count);
    for (std::size_t i = 0; i < *count; ++i) {
        const auto record = data.subspan(kStereoHeaderBytes + i * kStereoRecordBytes, kStereoRecordBytes);
        layouts.push_back(StereoLayout{.hash = io::loadU32Le(record),
                                       .interleave = io::loadU32Le(record.subspan(4)),
                                       .lastBlock = io::loadU32Le(record.subspan(8)),
                                       .blocks = io::loadU32Le(record.subspan(12))});
    }
    return layouts;
}

std::expected<std::vector<MusicRecord>, Error> SoundTables::parseMusicList(std::span<const std::byte> data) {
    auto count = countedRecords(data, 4, kMusicRecordBytes, "music list");
    if (!count) {
        return std::unexpected(std::move(count.error()));
    }
    std::vector<MusicRecord> tracks;
    tracks.reserve(*count);
    for (std::size_t i = 0; i < *count; ++i) {
        const auto record = data.subspan(4 + i * kMusicRecordBytes, kMusicRecordBytes);
        // The words at +0x08 and +0x0c (64 and 32 in every record) have no reader and are skipped.
        MusicRecord track;
        track.volume = std::bit_cast<float>(io::loadU32Le(record));
        track.sampleRate = io::loadU32Le(record.subspan(0x04));
        track.offset = io::loadU32Le(record.subspan(0x10));
        track.channels = io::loadU32Le(record.subspan(0x14));
        track.interleave = io::loadU32Le(record.subspan(0x18));
        track.blocks = io::loadU32Le(record.subspan(0x1c));
        track.lastBlock = io::loadU32Le(record.subspan(0x20));
        track.hash = io::loadU32Le(record.subspan(0x24));
        for (const std::byte b : record.subspan(kMusicNameOffset)) {
            if (b == std::byte{0}) {
                break;
            }
            track.name.push_back(static_cast<char>(b));
        }
        tracks.push_back(std::move(track));
    }
    return tracks;
}

SoundTables::SoundTables(std::vector<SoundRecord> sounds, std::vector<SoundClass> classes,
                         std::vector<StereoLayout> stereo, std::vector<MusicRecord> music)
    : m_sounds(std::move(sounds)), m_classes(std::move(classes)), m_stereo(std::move(stereo)),
      m_music(std::move(music)) {}

const SoundRecord* SoundTables::find(std::uint32_t hash) const {
    const auto it = std::ranges::lower_bound(m_sounds, hash, {}, &SoundRecord::hash);
    return it != m_sounds.end() && it->hash == hash ? &*it : nullptr;
}

const SoundClass* SoundTables::classOf(const SoundRecord& record) const {
    return record.soundClass < m_classes.size() ? &m_classes[record.soundClass] : nullptr;
}

const StereoLayout* SoundTables::stereoLayout(std::uint32_t hash) const {
    const auto it = std::ranges::find(m_stereo, hash, &StereoLayout::hash);
    return it == m_stereo.end() ? nullptr : &*it;
}

const MusicRecord* SoundTables::findMusic(std::uint32_t hash) const {
    const auto it = std::ranges::find(m_music, hash, &MusicRecord::hash);
    return it == m_music.end() ? nullptr : &*it;
}

MusicRecord* SoundTables::findMusic(std::uint32_t hash) {
    const auto it = std::ranges::find(m_music, hash, &MusicRecord::hash);
    return it == m_music.end() ? nullptr : &*it;
}

std::expected<SoundTables, Error> loadSoundTables(const io::Wad& wad) {
    auto entry = wad.lookup("warriors.glr");
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    // No handlers: the tables are raw chunks among the global resource's game-wide lists.
    const chunk::ChunkHandlerTable table;
    auto load = loadWadEntry(wad, **entry, table);
    if (!load) {
        return std::unexpected(std::move(load.error()));
    }
    // The first chunk of a type, or no bytes.
    const auto bytesOf = [&load](std::uint32_t type) {
        std::vector<chunk::ChunkData> chunks = load->stacks.takeChunks(type);
        return chunks.empty() ? std::vector<std::byte>{} : std::move(chunks.front().bytes);
    };
    const std::vector<std::byte> soundBytes = bytesOf(kSoundListChunk);
    if (soundBytes.empty()) {
        return fail(ErrorCode::NotFound, "warriors.glr holds no sound list (chunk 0x29)");
    }
    auto sounds = SoundTables::parseSoundList(soundBytes);
    if (!sounds) {
        return std::unexpected(std::move(sounds.error()));
    }
    const std::vector<std::byte> stereoBytes = bytesOf(kStereoTableChunk);
    auto stereo = stereoBytes.empty() ? std::expected<std::vector<StereoLayout>, Error>{}
                                      : SoundTables::parseStereoTable(stereoBytes);
    if (!stereo) {
        return std::unexpected(std::move(stereo.error()));
    }
    const std::vector<std::byte> musicBytes = bytesOf(kMusicListChunk);
    auto music =
        musicBytes.empty() ? std::expected<std::vector<MusicRecord>, Error>{} : SoundTables::parseMusicList(musicBytes);
    if (!music) {
        return std::unexpected(std::move(music.error()));
    }
    return SoundTables(std::move(*sounds), SoundTables::parseSoundClasses(bytesOf(kSoundClassChunk)),
                       std::move(*stereo), std::move(*music));
}

} // namespace coney::audio
