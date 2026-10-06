// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

namespace coney::io {
class Wad;
}

namespace coney::audio {

/// Chunk types of `warriors.glr` that hold the sound tables (docs/research/formats/audio.md).
inline constexpr std::uint32_t kSoundListChunk = 0x29;
inline constexpr std::uint32_t kMusicListChunk = 0x31;
inline constexpr std::uint32_t kSoundClassChunk = 0x48;
inline constexpr std::uint32_t kStereoTableChunk = 0x49;

/// The disc files that hold the streamed sounds and the music, in the disc's `IOP` folder.
inline constexpr std::string_view kSoundFile = "IOP/BFW.SND";
inline constexpr std::string_view kMusicFile = "IOP/MUSIC.SND";

/// The sample rate a sound record's rate index stands for, from the game's table of 73 rates (steps of 750 Hz with
/// the common rates slotted in); 0 for an index past the table.
/// Research: docs/research/formats/audio.md#sound-list
[[nodiscard]] int sampleRateOf(std::uint8_t rateIndex);

/// The flags of a sound class (docs/research/formats/audio.md#sound-classes).
enum SoundClassFlag : std::uint8_t {
    kClassLoops = 0x01,       ///< Loops until stopped.
    kClassPositional = 0x0a,  ///< Either bit: the sound has a position in the world.
    kClassStreamed = 0x04,    ///< Streams from BFW.SND; clear, plays from the bank in sound RAM.
    kClassDirectional = 0x10, ///< Louder in front of the source (character voices).
    kClassSmallLoop = 0x20,   ///< Streams on the reserved channels 10-12 (the `small_loops` ambience).
};

/// One sound class (chunk 0x48, 5 bytes): how the sounds of the class are placed, ranked and played.
struct SoundClass {
    std::uint8_t near = 0;     ///< Metres: full volume within it.
    std::uint16_t far = 0;     ///< Metres: silent beyond it (the record stores far / 5).
    std::uint8_t flags = 0;    ///< SoundClassFlag bits.
    std::uint8_t priority = 0; ///< Lower is more important (2-21).
    std::uint8_t channels = 1; ///< 1, or 2 for the stereo beds.

    [[nodiscard]] bool loops() const { return (flags & kClassLoops) != 0; }
    [[nodiscard]] bool positional() const { return (flags & kClassPositional) != 0; }
    [[nodiscard]] bool streamed() const { return (flags & kClassStreamed) != 0; }
    [[nodiscard]] bool directional() const { return (flags & kClassDirectional) != 0; }
    [[nodiscard]] bool smallLoop() const { return (flags & kClassSmallLoop) != 0; }
    [[nodiscard]] bool stereo() const { return channels == 2; }
};

/// One sound of the sound list (chunk 0x29, 16 bytes).
struct SoundRecord {
    std::uint32_t size = 0;          ///< Bytes of ADPCM (a placeholder for a stereo sound; see StereoLayout).
    std::uint32_t offset = 0;        ///< In BFW.SND for a streamed sound, in its bank for a bank sound.
    std::uint32_t hash = 0;          ///< CRC-32 of the name as written.
    std::uint8_t pitchVariation = 0; ///< Percent: each play picks a pitch factor in 1 +- v / 100.
    std::uint8_t volume = 100;       ///< Percent, 100 as recorded (0-250).
    std::uint8_t rateIndex = 0;      ///< Into the rate table (sampleRateOf()).
    std::uint8_t soundClass = 0;     ///< Into the sound classes.

    [[nodiscard]] int sampleRate() const { return sampleRateOf(rateIndex); }
};

/// A stereo sound's layout in BFW.SND (chunk 0x49, 16 bytes): `blocks` blocks of `interleave` bytes of the left
/// channel then `interleave` of the right, the stream ending `lastBlock` bytes into the last block.
struct StereoLayout {
    std::uint32_t hash = 0;
    std::uint32_t interleave = 0;
    std::uint32_t lastBlock = 0;
    std::uint32_t blocks = 0;
};

/// One track of the music list (chunk 0x31, 104 bytes), in MUSIC.SND as `blocks` blocks of `interleave` bytes per
/// channel, left block then right block.
struct MusicRecord {
    std::string name{};     ///< `music/<track>`.
    std::uint32_t hash = 0; ///< CRC-32 of the name.
    float volume = 1.0F;    ///< 1 on the disc; the scripts' SndCfgMusicInfo sets it.
    std::uint32_t sampleRate = 0;
    std::uint32_t offset = 0; ///< In MUSIC.SND.
    std::uint32_t channels = 2;
    std::uint32_t interleave = 0;
    std::uint32_t blocks = 0;
    std::uint32_t lastBlock = 0; ///< Bytes of each channel in the last block.

    /// The bytes the track spans in MUSIC.SND (whole blocks, as the game asks for them).
    [[nodiscard]] std::uint64_t size() const { return std::uint64_t{channels} * interleave * blocks; }
};

/// The game's sound tables: the sound list, the classes, the stereo layouts and the music list, all from
/// `warriors.glr`.
class SoundTables {
  public:
    /// Parses chunk 0x29: `u32 count`, then `count` records sorted by hash. Fails with Truncated when the records do
    /// not fit and Invalid when they are out of order (the lookup is a binary search; equal hashes occur on the disc,
    /// and find() gives the first).
    /// @orig 0x0010f900 SoundList_Load (unknown)
    /// Research: docs/research/formats/audio.md#sound-list
    [[nodiscard]] static std::expected<std::vector<SoundRecord>, Error> parseSoundList(std::span<const std::byte> data);
    /// Parses chunk 0x48: 5-byte records with no count, ended by the chunk's padding (a record of 0 channels).
    /// @orig 0x0010f3a0 SoundClasses_Load (unknown)
    /// Research: docs/research/formats/audio.md#sound-classes
    [[nodiscard]] static std::vector<SoundClass> parseSoundClasses(std::span<const std::byte> data);
    /// Parses chunk 0x49: `u32 count`, 12 bytes, then `count` records. Fails with Truncated when they do not fit.
    /// @orig 0x0010f988 StereoTable_Load (unknown)
    /// Research: docs/research/formats/audio.md#stereo
    [[nodiscard]] static std::expected<std::vector<StereoLayout>, Error>
    parseStereoTable(std::span<const std::byte> data);
    /// Parses chunk 0x31: `u32 count`, then `count` records of 104 bytes. Fails with Truncated when they do not fit.
    /// @orig 0x0010f9d8 MusicList_Load (unknown)
    /// Research: docs/research/formats/audio.md#music
    [[nodiscard]] static std::expected<std::vector<MusicRecord>, Error> parseMusicList(std::span<const std::byte> data);

    /// Tables from parsed parts. The sound list must be sorted by hash (parseSoundList() checks it).
    SoundTables(std::vector<SoundRecord> sounds, std::vector<SoundClass> classes, std::vector<StereoLayout> stereo,
                std::vector<MusicRecord> music);
    SoundTables() = default;

    /// The sound whose name hashes to `hash`, or nullptr: none plays.
    /// @orig 0x001119d0 SoundList_Find (unknown)
    [[nodiscard]] const SoundRecord* find(std::uint32_t hash) const;
    /// The class of `record`, or nullptr when its index is past the table.
    [[nodiscard]] const SoundClass* classOf(const SoundRecord& record) const;
    /// The stereo layout of the sound `hash`, or nullptr.
    [[nodiscard]] const StereoLayout* stereoLayout(std::uint32_t hash) const;
    /// The music track `hash`, or nullptr.
    [[nodiscard]] const MusicRecord* findMusic(std::uint32_t hash) const;
    /// The music track `hash` to change (its volume, by SndCfgMusicInfo), or nullptr.
    [[nodiscard]] MusicRecord* findMusic(std::uint32_t hash);

    [[nodiscard]] std::span<const SoundRecord> sounds() const { return m_sounds; }
    [[nodiscard]] std::span<const SoundClass> classes() const { return m_classes; }
    [[nodiscard]] std::span<const StereoLayout> stereo() const { return m_stereo; }
    [[nodiscard]] std::span<const MusicRecord> music() const { return m_music; }

  private:
    std::vector<SoundRecord> m_sounds; // sorted by hash
    std::vector<SoundClass> m_classes;
    std::vector<StereoLayout> m_stereo;
    std::vector<MusicRecord> m_music;
};

/// Loads the sound tables from `warriors.glr` in `wad`. Fails with NotFound when the file or the sound list is
/// missing; the other tables are empty when their chunk is.
[[nodiscard]] std::expected<SoundTables, Error> loadSoundTables(const io::Wad& wad);

} // namespace coney::audio
