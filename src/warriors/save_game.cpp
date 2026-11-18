// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only Coney's own save container: the frame (header, sections, CRC-32) and the steps between versions; the
// sections' codecs are save_sections.cpp (docs/research/save.md#save-format)
#include "warriors/save_game.h"

#include <algorithm>
#include <array>
#include <format>
#include <iterator>
#include <string_view>
#include <utility>

#include "core/assert.h"
#include "core/name_hash.h"
#include "fileio/reader.h"
#include "warriors/save_sections.h"

namespace coney {

namespace {

using save_sections::Section;
using save_sections::Writer;

// The frame (docs/research/save.md#save-format): the same in every version, so any Coney can read a file's name.
constexpr std::string_view kMagic = "CONEYSAV";
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kTrailerSize = 4;
constexpr std::uint32_t kMaxSections = 1024;
constexpr std::size_t kMaxSectionName = 64;

// A file's frame, read: its version and its sections in file order.
struct Framed {
    std::uint32_t version = 0;
    std::vector<Section> sections;
};

// Whether `name` is a section name the frame allows: 1-64 bytes of a-z, 0-9, '-' and ':'.
bool validSectionName(std::string_view name) {
    return !name.empty() && name.size() <= kMaxSectionName && std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == ':';
    });
}

// Whether `bytes` start with the magic: a framed save of some version, not a bare record.
bool hasMagic(std::span<const std::byte> bytes) {
    return bytes.size() >= kMagic.size() &&
           std::ranges::equal(bytes.first(kMagic.size()), kMagic,
                              [](std::byte b, char c) { return static_cast<char>(b) == c; });
}

// Reads the frame: the header, every section and the trailer. With `checkCrc` false it reads as far as it can for
// nameOf, so a damaged file can still be listed under its name.
std::expected<Framed, Error> readFrame(std::span<const std::byte> bytes, bool checkCrc) {
    if (bytes.size() < kHeaderSize + kTrailerSize) {
        return fail(ErrorCode::Truncated, std::format("a save of {} bytes is shorter than its frame", bytes.size()));
    }
    // The header: the magic, the version and the section count.
    if (!hasMagic(bytes)) {
        return fail(ErrorCode::Invalid, "not a Coney save: no CONEYSAV magic");
    }
    io::Reader reader(bytes.subspan(kMagic.size(), bytes.size() - kMagic.size() - kTrailerSize));
    Framed framed;
    framed.version = reader.readU32Le().value_or(0);
    const std::uint32_t count = reader.readU32Le().value_or(0);
    if (count > kMaxSections) {
        return fail(ErrorCode::Invalid, std::format("a save of {} sections, more than {}", count, kMaxSections));
    }
    // Each section: its name, then its payload's length and the payload.
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto ended = [&] {
            return fail(ErrorCode::Truncated, std::format("the save ends in its section {} of {}", i + 1, count));
        };
        const auto nameLength = reader.readU8();
        if (!nameLength) {
            return ended();
        }
        const auto name = reader.readBytes(*nameLength);
        if (!name) {
            return ended();
        }
        const auto length = reader.readU32Le();
        if (!length) {
            return ended();
        }
        const auto payload = reader.readBytes(*length);
        if (!payload) {
            return ended();
        }
        Section section;
        std::ranges::transform(*name, std::back_inserter(section.name),
                               [](std::byte b) { return static_cast<char>(b); });
        if (!validSectionName(section.name)) {
            return fail(ErrorCode::Invalid, std::format("save section {} has a name outside the rules", i + 1));
        }
        section.payload.assign(payload->begin(), payload->end());
        framed.sections.push_back(std::move(section));
    }
    // The trailer follows the last section at once, and seals every byte before it.
    if (reader.remaining() != 0) {
        return fail(ErrorCode::Invalid,
                    std::format("{} bytes between the last section and the CRC", reader.remaining()));
    }
    if (checkCrc) {
        const std::uint32_t stored = io::loadU32Le(bytes.last(kTrailerSize));
        if (const std::uint32_t crc = crc32(bytes.first(bytes.size() - kTrailerSize)); crc != stored) {
            return fail(ErrorCode::Invalid,
                        std::format("the save's CRC-32 is {:#010x}, its bytes give {:#010x}", stored, crc));
        }
    }
    return framed;
}

// Writes the frame around `sections` in the current version.
std::vector<std::byte> writeFrame(const std::vector<Section>& sections) {
    CONEY_ASSERT(sections.size() <= kMaxSections);
    Writer w;
    for (const char c : kMagic) {
        w.u8(static_cast<std::uint8_t>(c));
    }
    w.u32(SaveGame::kVersion);
    w.u32(static_cast<std::uint32_t>(sections.size()));
    for (const Section& section : sections) {
        CONEY_ASSERT(validSectionName(section.name));
        w.string(section.name);
        w.u32(static_cast<std::uint32_t>(section.payload.size()));
        w.raw(section.payload);
    }
    std::vector<std::byte> out = w.take();
    const std::uint32_t crc = crc32(out);
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::byte>(crc >> shift));
    }
    return out;
}

// One migration step: the sections of version n + 1 from those of version n (n >= 1). Version 0 has no sections;
// fromRecord takes its place.
using Step = std::expected<std::vector<Section>, Error> (*)(std::vector<Section>);
// The steps from version 1 up, step i taking version i + 1 to i + 2. Empty while version 1 is the current one.
constexpr std::array<Step, SaveGame::kVersion - 1> kSteps{};

// Version 0, the original's bare record, to a save: each field into its section, nothing modded.
std::expected<SaveGame, Error> fromRecord(std::span<const std::byte> bytes) {
    auto record = ProfileRecord::parse(bytes);
    if (!record) {
        return std::unexpected(record.error());
    }
    SaveGame save;
    save.profile = std::move(*record);
    return save;
}

} // namespace

std::vector<std::byte> SaveGame::serialise() const { return writeFrame(save_sections::encode(*this)); }

std::expected<SaveGame, Error> SaveGame::parse(std::span<const std::byte> bytes) {
    // No magic: version 0, the record earlier Coney builds wrote (and the original's card holds).
    if (!hasMagic(bytes) && bytes.size() == ProfileRecord::kSize) {
        return fromRecord(bytes);
    }
    auto framed = readFrame(bytes, true);
    if (!framed) {
        return std::unexpected(framed.error());
    }
    if (framed->version == 0) {
        return fail(ErrorCode::Invalid, "a framed save cannot be version 0");
    }
    if (framed->version > kVersion) {
        return fail(ErrorCode::Invalid,
                    std::format("the save was made by a newer Coney (version {}; this one reads up to {})",
                                framed->version, kVersion));
    }
    // Step the sections up to the current version, then read them. While version 1 is the current one there is no
    // step, and the loop is left out so that the compiler does not report it as unreachable.
    std::vector<Section> sections = std::move(framed->sections);
    if constexpr (!kSteps.empty()) {
        for (std::uint32_t version = framed->version; version < kVersion; ++version) {
            auto stepped = kSteps.at(version - 1)(std::move(sections));
            if (!stepped) {
                return std::unexpected(stepped.error());
            }
            sections = std::move(*stepped);
        }
    }
    return save_sections::decode(sections);
}

std::string SaveGame::nameOf(std::span<const std::byte> bytes) {
    // No magic: a record's header name, as the original lists a slot it cannot load. A file with the magic is a Coney
    // save, whose first bytes are not a record's header: when its frame cannot be read, it has no name.
    if (!hasMagic(bytes)) {
        return ProfileRecord::headerName(bytes).value_or(std::string{});
    }
    auto framed = readFrame(bytes, false);
    if (!framed) {
        return {};
    }
    // The name leads the profile section in every version.
    for (const Section& section : framed->sections) {
        if (section.name == save_sections::kProfile) {
            return save_sections::profileName(section);
        }
    }
    return {};
}

} // namespace coney
