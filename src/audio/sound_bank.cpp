// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/sound_bank.h"

#include <format>
#include <optional>

#include "audio/adpcm.h"
#include "audio/sound_data.h"
#include "fileio/reader.h"
#include "fileio/wad.h"

namespace coney::audio {

namespace {

constexpr std::size_t kIndexPairBytes = 8;

// Reads the whole WAD entry `name` into memory.
std::expected<std::vector<std::byte>, Error> readEntry(const io::Wad& wad, const std::string& name) {
    auto entry = wad.lookup(name);
    if (!entry) {
        return std::unexpected(std::move(entry.error()));
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    std::vector<std::byte> bytes(stream->size());
    if (auto done = stream->read(bytes); !done) {
        return std::unexpected(std::move(done.error()));
    }
    return bytes;
}

} // namespace

std::vector<std::pair<std::uint32_t, std::uint32_t>> SoundBank::parseIndex(std::span<const std::byte> msd) {
    std::vector<std::pair<std::uint32_t, std::uint32_t>> pairs;
    for (std::size_t at = 0; at + kIndexPairBytes <= msd.size(); at += kIndexPairBytes) {
        const std::uint32_t hash = io::loadU32Le(msd.subspan(at));
        const std::uint32_t offset = io::loadU32Le(msd.subspan(at + 4));
        if (hash == 0 && offset == 0) {
            break;
        }
        pairs.emplace_back(hash, offset);
    }
    return pairs;
}

SoundBank SoundBank::decode(std::string name, std::span<const std::byte> msd, std::span<const std::byte> msb,
                            const SoundTables& tables) {
    SoundBank bank;
    bank.m_name = std::move(name);
    for (const auto& [hash, offset] : parseIndex(msd)) {
        const SoundRecord* record = tables.find(hash);
        if (record == nullptr || offset > msb.size() || record->size > msb.size() - offset) {
            ++bank.m_skipped;
            continue;
        }
        // A bank sample is one-shot ADPCM ending with a frame flagged as the end; decode up to it.
        std::vector<std::int16_t> samples = decodeAdpcm(msb.subspan(offset, record->size), true);
        const SoundClass* soundClass = tables.classOf(*record);
        std::optional<LoopPoints> loop;
        if (soundClass != nullptr && soundClass->loops() && !samples.empty()) {
            loop = LoopPoints{.start = 0, .end = static_cast<std::uint32_t>(samples.size())};
        }
        auto sound = PcmSound::create(std::move(samples), 1, record->sampleRate(), loop);
        if (!sound) {
            ++bank.m_skipped;
            continue;
        }
        bank.m_samples.emplace(hash, std::make_shared<const PcmSound>(std::move(*sound)));
    }
    return bank;
}

std::shared_ptr<const PcmSound> SoundBank::find(std::uint32_t hash) const {
    const auto it = m_samples.find(hash);
    return it == m_samples.end() ? nullptr : it->second;
}

std::expected<SoundBank, Error> loadSoundBank(const io::Wad& wad, std::string_view name, const SoundTables& tables) {
    auto msd = readEntry(wad, std::format("{}.msd", name));
    if (!msd) {
        return std::unexpected(std::move(msd.error()));
    }
    auto msb = readEntry(wad, std::format("{}.msb", name));
    if (!msb) {
        return std::unexpected(std::move(msb.error()));
    }
    return SoundBank::decode(std::string(name), *msd, *msb, tables);
}

} // namespace coney::audio
