// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/profile_record.h"

#include <algorithm>
#include <bit>
#include <format>

#include "core/assert.h"
#include "fileio/reader.h"

namespace coney {

namespace {

// The record's offsets (docs/research/save.md#record), named once so the writer and the reader agree.
constexpr std::size_t kVersionAt = 0x000;
constexpr std::size_t kNameAt = 0x004;
constexpr std::size_t kHardcoreAt = 0x00c;
constexpr std::size_t kHeaderSize = 0x010;
constexpr std::size_t kMissionBytes = 12;
constexpr std::size_t kOptionsAt = 0x4b0;
constexpr std::size_t kScriptFlagsAt = 0x4f4;

// Appends little-endian values to a fixed buffer: the writing half of what io::Reader reads.
class RecordWriter {
  public:
    explicit RecordWriter(std::span<std::byte> out) : m_out(out) {}

    // One byte.
    void u8(std::uint8_t value) { m_out[m_at++] = static_cast<std::byte>(value); }
    // A 32-bit little-endian value.
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    // An IEEE 754 single, little-endian.
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    // A flag stored as a 32-bit word.
    void flag(bool value) { u32(value ? 1U : 0U); }
    // A run of raw bytes.
    void bytes(std::span<const std::uint8_t> values) {
        for (const std::uint8_t value : values) {
            u8(value);
        }
    }
    // Where the next value goes.
    [[nodiscard]] std::size_t position() const { return m_at; }

  private:
    std::span<std::byte> m_out;
    std::size_t m_at = 0;
};

// Copies `count` raw bytes from the reader into `out`.
std::expected<void, Error> readInto(io::Reader& reader, std::span<std::uint8_t> out) {
    auto bytes = reader.readBytes(out.size());
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::ranges::transform(*bytes, out.begin(), [](std::byte b) { return static_cast<std::uint8_t>(b); });
    return {};
}

// The 8-byte name field as a string: up to the first zero byte, or all 8.
std::string nameFrom(std::span<const std::byte> field) {
    std::string name;
    for (const std::byte b : field) {
        if (b == std::byte{0}) {
            break;
        }
        name.push_back(static_cast<char>(b));
    }
    return name;
}

// Whether bit `index` of a little-endian bit set is set.
bool testBit(std::span<const std::uint8_t> bits, std::size_t index) {
    return ((bits[index / 8] >> (index % 8)) & 1U) != 0;
}

// Sets or clears bit `index` of a little-endian bit set.
void putBit(std::span<std::uint8_t> bits, std::size_t index, bool on) {
    const auto mask = static_cast<std::uint8_t>(1U << (index % 8));
    bits[index / 8] = static_cast<std::uint8_t>(on ? (bits[index / 8] | mask) : (bits[index / 8] & ~mask));
}

} // namespace

bool SavedProgress::isLocked(std::size_t index) const { return index >= kUnlockables || testBit(lockedBits, index); }

void SavedProgress::setLocked(std::size_t index, bool locked) {
    if (index < kUnlockables) {
        putBit(lockedBits, index, locked);
    }
}

bool SavedProgress::scriptFlag(std::size_t flag) const {
    return flag >= 1 && flag <= kScriptFlags && testBit(scriptFlags, flag - 1);
}

void SavedProgress::setScriptFlag(std::size_t flag, bool on) {
    if (flag >= 1 && flag <= kScriptFlags) {
        putBit(scriptFlags, flag - 1, on);
    }
}

std::array<std::byte, ProfileRecord::kSize> ProfileRecord::serialise() const {
    std::array<std::byte, kSize> out{};
    RecordWriter w(out);
    // The 16-byte header: version, name (zero-padded, no terminator at 8), the fourth-difficulty unlock.
    w.u32(kVersion);
    for (std::size_t i = 0; i < kNameLength; ++i) {
        w.u8(i < name.size() ? static_cast<std::uint8_t>(name[i]) : 0);
    }
    w.flag(hardcoreFinished);
    // The per-mission bests.
    for (const MissionBest& best : progress.missionBests) {
        w.u32(best.bestScore);
        w.u32(best.total);
        w.u8(best.grade);
        w.u8(best.percent);
        w.bytes(best.rest);
    }
    // The unlockables, the Rumble data and the money.
    w.bytes(progress.lockedBits);
    w.bytes(progress.newBits);
    w.bytes(progress.rumbleData);
    w.bytes(progress.rumbleBits);
    w.u32(progress.bankedMoney);
    // The option block; the three bytes after the brightness byte are written as zero (Coney keeps no value there).
    CONEY_ASSERT(w.position() == kOptionsAt);
    w.u32(progress.optionWord0);
    w.u32(brightness);
    w.f32(progress.soundFxVolume);
    w.f32(progress.musicVolume);
    w.u32(progress.optionWord10);
    // The per-pad options, P1 then P2, then the global ones.
    for (const PadOptions& pad : progress.pads) {
        w.flag(pad.invertCamera);
        w.flag(pad.autoAdjustCamera);
        w.flag(pad.vibration);
    }
    w.flag(progress.splitScreenMerge);
    w.flag(subtitles);
    w.flag(progress.proLogic2);
    w.flag(progress.wideVideo);
    w.u32(difficulty);
    w.u32(progress.stateWord0);
    CONEY_ASSERT(w.position() == kScriptFlagsAt);
    w.bytes(progress.scriptFlags);
    CONEY_ASSERT(w.position() == kSize);
    return out;
}

std::expected<std::string, Error> ProfileRecord::headerName(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) {
        return fail(ErrorCode::Truncated,
                    std::format("a profile header needs {} bytes but only {} are there", kHeaderSize, bytes.size()));
    }
    return nameFrom(bytes.subspan(kNameAt, kNameLength));
}

std::expected<ProfileRecord, Error> ProfileRecord::parse(std::span<const std::byte> bytes) {
    // Check the size and the version first: either wrong makes the profile not loadable.
    if (bytes.size() != kSize) {
        return fail(bytes.size() < kSize ? ErrorCode::Truncated : ErrorCode::Invalid,
                    std::format("a profile record is {} bytes, not {}", kSize, bytes.size()));
    }
    io::Reader r(bytes);
    ProfileRecord record;
    // Every read below is in bounds (the size was checked), so the values are taken without further tests.
    const auto u32 = [&r] { return r.readU32Le().value_or(0); };
    const auto f32 = [&r] { return r.readF32Le().value_or(0.0F); };
    const auto u8 = [&r] { return r.readU8().value_or(0); };
    const auto flag = [&u32] { return u32() != 0; };

    if (const std::uint32_t version = u32(); version != kVersion) {
        return fail(ErrorCode::Invalid, std::format("profile record version {:#x}, expected {:#x}", version, kVersion));
    }
    CONEY_ASSERT(r.position() == kNameAt);
    record.name = nameFrom(r.readBytes(kNameLength).value_or(std::span<const std::byte>{}));
    CONEY_ASSERT(r.position() == kHardcoreAt);
    record.hardcoreFinished = flag();
    SavedProgress& p = record.progress;
    for (MissionBest& best : p.missionBests) {
        best.bestScore = u32();
        best.total = u32();
        best.grade = u8();
        best.percent = u8();
        best.rest = {u8(), u8()};
    }
    static_assert(kHeaderSize + SavedProgress::kMissions * kMissionBytes == 0x178);
    for (std::span<std::uint8_t> field :
         {std::span<std::uint8_t>(p.lockedBits), std::span<std::uint8_t>(p.newBits),
          std::span<std::uint8_t>(p.rumbleData), std::span<std::uint8_t>(p.rumbleBits)}) {
        if (auto read = readInto(r, field); !read) {
            return std::unexpected(read.error());
        }
    }
    p.bankedMoney = u32();
    CONEY_ASSERT(r.position() == kOptionsAt);
    p.optionWord0 = u32();
    record.brightness = static_cast<std::uint8_t>(u32() & 0xffU);
    p.soundFxVolume = f32();
    p.musicVolume = f32();
    p.optionWord10 = u32();
    for (PadOptions& pad : p.pads) {
        pad.invertCamera = flag();
        pad.autoAdjustCamera = flag();
        pad.vibration = flag();
    }
    p.splitScreenMerge = flag();
    record.subtitles = flag();
    p.proLogic2 = flag();
    p.wideVideo = flag();
    // The difficulty is kept as a byte in the game state.
    record.difficulty = static_cast<std::uint8_t>(u32() & 0xffU);
    p.stateWord0 = u32();
    CONEY_ASSERT(r.position() == kScriptFlagsAt);
    if (auto read = readInto(r, p.scriptFlags); !read) {
        return std::unexpected(read.error());
    }
    return record;
}

} // namespace coney
