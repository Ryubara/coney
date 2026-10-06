// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_stream.h"

#include <algorithm>
#include <format>
#include <utility>

#include "audio/sound_data.h"
#include "fileio/disc.h"
#include "fileio/file_stream.h"

namespace coney::audio {

namespace {

// Bytes of a mono sound read at once: whole ADPCM frames, small enough to read a little each step.
constexpr std::uint32_t kMonoPart = 8192;

// `bytes` rounded down to whole ADPCM frames.
std::uint32_t wholeFrames(std::uint32_t bytes) {
    return bytes - (bytes % static_cast<std::uint32_t>(kAdpcmFrameBytes));
}

// Opens the disc file `name` as a stream of its own.
std::expected<std::unique_ptr<io::FileStream>, Error> openSoundFile(const io::Disc& disc, std::string_view name) {
    auto stream = disc.openFile(name);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    return std::make_unique<io::FileStream>(std::move(*stream));
}

} // namespace

DiscSoundFiles::DiscSoundFiles() = default;

DiscSoundFiles::~DiscSoundFiles() = default;

std::expected<std::unique_ptr<DiscSoundFiles>, Error> DiscSoundFiles::open(const io::Disc& disc) {
    std::unique_ptr<DiscSoundFiles> files(new DiscSoundFiles());
    auto sounds = openSoundFile(disc, kSoundFile);
    if (!sounds) {
        return std::unexpected(std::move(sounds.error()));
    }
    auto music = openSoundFile(disc, kMusicFile);
    if (!music) {
        return std::unexpected(std::move(music.error()));
    }
    files->m_sounds = std::move(*sounds);
    files->m_music = std::move(*music);
    return files;
}

std::expected<void, Error> DiscSoundFiles::read(SoundFile file, std::uint64_t offset, std::span<std::byte> out) {
    io::FileStream& stream = file == SoundFile::Sounds ? *m_sounds : *m_music;
    if (auto moved = stream.seek(offset); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    return stream.read(out);
}

StreamLayout StreamLayout::mono(SoundFile file, std::uint64_t offset, std::uint32_t size, bool loops) {
    return StreamLayout{.file = file,
                        .offset = offset,
                        .channels = 1,
                        .interleave = size,
                        .blocks = 1,
                        .lastBlock = size,
                        .loops = loops};
}

std::expected<std::unique_ptr<StreamFeeder>, Error> StreamFeeder::create(const StreamLayout& layout, int sampleRate,
                                                                         std::uint32_t aheadFrames) {
    if (layout.channels < 1 || layout.channels > 2) {
        return fail(ErrorCode::InvalidArgument, std::format("a stream of {} channels", layout.channels));
    }
    auto stream = PcmStream::create(layout.channels, sampleRate, aheadFrames);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    return std::unique_ptr<StreamFeeder>(new StreamFeeder(layout, std::move(*stream)));
}

StreamFeeder::StreamFeeder(const StreamLayout& layout, std::shared_ptr<PcmStream> stream)
    : m_layout(layout), m_stream(std::move(stream)), m_decoders(static_cast<std::size_t>(layout.channels)) {}

std::expected<bool, Error> StreamFeeder::decodeNext(SoundFiles& files) {
    if (m_block >= m_layout.blocks) {
        if (!m_layout.loops) {
            return false;
        }
        // A loop starts again from its first block with fresh decoders.
        m_block = 0;
        m_partInBlock = 0;
        for (AdpcmDecoder& decoder : m_decoders) {
            decoder.reset();
        }
    }
    const bool last = m_block + 1 == m_layout.blocks;
    const std::uint32_t useful = wholeFrames(
        last && m_layout.lastBlock != 0 ? std::min(m_layout.lastBlock, m_layout.interleave) : m_layout.interleave);
    const std::uint64_t blockStart = m_layout.offset + (std::uint64_t{m_block} * m_layout.interleave *
                                                        static_cast<std::uint64_t>(m_layout.channels));
    m_pending.clear();
    m_pendingAt = 0;
    if (m_layout.channels == 1) {
        // A mono sound: the next part of its one block.
        const std::uint32_t part = std::min(kMonoPart, useful - m_partInBlock);
        m_read.resize(part);
        if (auto done = files.read(m_layout.file, blockStart + m_partInBlock, m_read); !done) {
            return std::unexpected(std::move(done.error()));
        }
        m_decoders.front().decode(m_read, m_pending);
        m_partInBlock += part;
        if (m_partInBlock >= useful) {
            ++m_block;
            m_partInBlock = 0;
        }
        return true;
    }
    // Block-interleaved stereo: the whole block of each channel, decoded and interleaved frame by frame.
    m_read.resize(std::size_t{m_layout.interleave} * 2);
    if (auto done = files.read(m_layout.file, blockStart, m_read); !done) {
        return std::unexpected(std::move(done.error()));
    }
    std::vector<std::int16_t> left;
    std::vector<std::int16_t> right;
    m_decoders[0].decode(std::span<const std::byte>(m_read).first(useful), left);
    m_decoders[1].decode(std::span<const std::byte>(m_read).subspan(m_layout.interleave, useful), right);
    m_pending.reserve(left.size() * 2);
    for (std::size_t i = 0; i < left.size(); ++i) {
        m_pending.push_back(left[i]);
        m_pending.push_back(right[i]);
    }
    ++m_block;
    return true;
}

std::expected<void, Error> StreamFeeder::pump(SoundFiles& files) {
    while (!m_done) {
        // Hand over what is decoded; a full stream waits for the voice to drain it.
        if (m_pendingAt < m_pending.size()) {
            const std::size_t written = m_stream->write(std::span<const std::int16_t>(m_pending).subspan(m_pendingAt));
            m_pendingAt += written * static_cast<std::size_t>(m_layout.channels);
            if (m_pendingAt < m_pending.size()) {
                return {};
            }
        }
        // An empty sound has nothing to loop over.
        auto more = m_layout.span() == 0 ? std::expected<bool, Error>(false) : decodeNext(files);
        if (!more || !*more) {
            m_done = true;
            m_stream->finish();
            if (!more) {
                return std::unexpected(std::move(more.error()));
            }
        }
    }
    return {};
}

} // namespace coney::audio
