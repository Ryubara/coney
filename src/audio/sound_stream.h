// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "audio/adpcm.h"
#include "audio/pcm_stream.h"
#include "core/error.h"

namespace coney::io {
class Disc;
class FileStream;
} // namespace coney::io

namespace coney::audio {

/// The two disc files sound streams from.
enum class SoundFile : std::uint8_t {
    Sounds, ///< IOP/BFW.SND: every streamed sound.
    Music,  ///< IOP/MUSIC.SND: the music.
};

/// Where streamed sound bytes come from: the disc's IOP files, or a test's synthetic bytes. Reads happen on the game
/// thread, a little each step.
class SoundFiles {
  public:
    SoundFiles() = default;
    virtual ~SoundFiles() = default;
    SoundFiles(const SoundFiles&) = delete;
    SoundFiles& operator=(const SoundFiles&) = delete;
    SoundFiles(SoundFiles&&) = delete;
    SoundFiles& operator=(SoundFiles&&) = delete;

    /// Reads `out.size()` bytes of `file` from `offset`. Fails as the disc's reads do; a read past the end fails with
    /// Truncated.
    [[nodiscard]] virtual std::expected<void, Error> read(SoundFile file, std::uint64_t offset,
                                                          std::span<std::byte> out) = 0;
};

/// The disc's IOP/BFW.SND and IOP/MUSIC.SND, each opened once.
class DiscSoundFiles final : public SoundFiles {
  public:
    /// Opens both files on `disc`. Fails with NotFound when either is missing.
    [[nodiscard]] static std::expected<std::unique_ptr<DiscSoundFiles>, Error> open(const io::Disc& disc);
    ~DiscSoundFiles() override;
    DiscSoundFiles(const DiscSoundFiles&) = delete;
    DiscSoundFiles& operator=(const DiscSoundFiles&) = delete;
    DiscSoundFiles(DiscSoundFiles&&) = delete;
    DiscSoundFiles& operator=(DiscSoundFiles&&) = delete;

    [[nodiscard]] std::expected<void, Error> read(SoundFile file, std::uint64_t offset,
                                                  std::span<std::byte> out) override;

  private:
    DiscSoundFiles();
    std::unique_ptr<io::FileStream> m_sounds;
    std::unique_ptr<io::FileStream> m_music;
};

/// Where a stream's ADPCM is: `blocks` blocks of `interleave` bytes per channel (a mono sound is one block of its whole
/// size), the last block holding `lastBlock` bytes of each channel.
struct StreamLayout {
    SoundFile file = SoundFile::Sounds;
    std::uint64_t offset = 0;
    int channels = 1;
    std::uint32_t interleave = 0;
    std::uint32_t blocks = 1;
    std::uint32_t lastBlock = 0;
    bool loops = false; ///< Start again from the first block at the end (the stream's "once" flag clear).

    /// A mono sound of `size` bytes at `offset`.
    [[nodiscard]] static StreamLayout mono(SoundFile file, std::uint64_t offset, std::uint32_t size, bool loops);
    /// Bytes the game asks the file for: whole blocks of every channel.
    [[nodiscard]] std::uint64_t span() const { return std::uint64_t{blocks} * interleave * channels; }
};

/// Decodes one streamed sound into a PcmStream as the mixer drains it: the work of the game's IOP stream channels,
/// which read the disc's ADPCM into sound RAM for a voice. pump() runs on the game thread each step and keeps the
/// stream about `aheadFrames` frames ahead of the voice; it reads the file one block (or one 8 KiB part of a mono
/// sound) at a time. The PS2's stream stops at the sound's size (the streamed data carries no end flags) or, for a
/// block-interleaved stream, `lastBlock` bytes into the last block.
/// Research: docs/research/formats/audio.md#bfw-snd, docs/research/formats/audio.md#stereo
class StreamFeeder {
  public:
    /// A feeder of `layout` at `sampleRate` into a new PcmStream holding `aheadFrames` frames.
    [[nodiscard]] static std::expected<std::unique_ptr<StreamFeeder>, Error>
    create(const StreamLayout& layout, int sampleRate, std::uint32_t aheadFrames);

    /// Decodes and queues frames until the stream is full or the sound has ended (then finishes the stream). A read
    /// failure finishes the stream too and is returned once.
    std::expected<void, Error> pump(SoundFiles& files);
    /// The stream a voice plays.
    [[nodiscard]] const std::shared_ptr<PcmStream>& stream() const { return m_stream; }
    /// Whether the whole sound has been queued (never, for a loop).
    [[nodiscard]] bool done() const { return m_done; }
    /// Whether the stream has as many frames queued as it can hold, or the sound is all queued: ready to start.
    [[nodiscard]] bool ready() const { return m_done || m_stream->freeFrames() == 0; }

  private:
    StreamFeeder(const StreamLayout& layout, std::shared_ptr<PcmStream> stream);
    // Decodes the next part of the file into m_pending; false at the end of a sound played once.
    std::expected<bool, Error> decodeNext(SoundFiles& files);

    StreamLayout m_layout;
    std::shared_ptr<PcmStream> m_stream;
    std::vector<AdpcmDecoder> m_decoders; // one per channel
    std::uint32_t m_block = 0;            // the next block to read
    std::uint32_t m_partInBlock = 0;      // bytes of the block read so far (mono parts)
    std::vector<std::int16_t> m_pending;  // decoded frames, interleaved, not yet in the stream
    std::size_t m_pendingAt = 0;          // the first sample of m_pending not yet written
    std::vector<std::byte> m_read;        // the read buffer
    bool m_done = false;
};

} // namespace coney::audio
