// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/wad.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace coney::io {

namespace {

// What a stream holds, as the script check sees it.
enum class StreamKind : std::uint8_t {
    Data,       // anything but a chunk, a stream shorter than the signature included
    Chunk,      // a precompiled Lua chunk (ESC "Lua"), as every script on the disc is
    Unreadable, // the stream could not be opened or read
};

// Tells a chunk by its signature. A stream that cannot be opened or read is Unreadable, never Data, so the check
// fails closed: a later loader that ran other bytes could not be reached through a read error.
StreamKind streamKind(std::expected<FileStream, Error> stream) {
    if (!stream) {
        return StreamKind::Unreadable;
    }
    if (stream->size() < 4) {
        return StreamKind::Data;
    }
    std::array<std::byte, 4> head{};
    if (!stream->read(head)) {
        return StreamKind::Unreadable;
    }
    return head == std::array{std::byte{0x1b}, std::byte{'L'}, std::byte{'u'}, std::byte{'a'}} ? StreamKind::Chunk
                                                                                               : StreamKind::Data;
}

} // namespace

std::expected<Wad, Error> Wad::open(Disc disc) {
    auto dirData = disc.readFile(kWadDirFile);
    if (!dirData) {
        return std::unexpected(std::move(dirData.error()));
    }
    auto wadSize = disc.fileSize(kWadFile);
    if (!wadSize) {
        return std::unexpected(std::move(wadSize.error()));
    }
    auto index = WadIndex::parse(*dirData, *wadSize);
    if (!index) {
        return std::unexpected(std::move(index.error()));
    }
    return Wad(std::move(disc), std::move(*index));
}

std::expected<const WadEntry*, Error> Wad::lookup(std::string_view nameOrHash) const {
    if (nameOrHash.empty()) {
        return fail(ErrorCode::InvalidArgument, "an entry needs a name or a 0x hash");
    }
    // A hash: parse it strictly, then look it up as stored, without the name prefix.
    if (nameOrHash.starts_with("0x") || nameOrHash.starts_with("0X")) {
        const std::string_view digits = nameOrHash.substr(2);
        const bool allHex = !digits.empty() && digits.size() <= 8 && std::ranges::all_of(digits, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        });
        std::uint32_t hash = 0;
        if (!allHex || std::from_chars(digits.data(), digits.data() + digits.size(), hash, 16).ec != std::errc{}) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("\"{}\" is not a hash (0x and 1 to 8 hex digits)", nameOrHash));
        }
        if (const WadEntry* entry = m_index.findHash(hash)) {
            return entry;
        }
        return fail(ErrorCode::NotFound, std::format("no WAD entry has the hash {:#010x}", hash));
    }
    // Anything else is a file name.
    if (const WadEntry* entry = m_index.find(nameOrHash)) {
        return entry;
    }
    return fail(ErrorCode::NotFound, std::format("no WAD entry is called \"{}\"", nameOrHash));
}

// Coney's choice: a FileStream is a value, so any number of entries are open at once. The original's stream file
// system hands out streams from a pool and opens one at a time (docs/research/file-io.md#original-structure).
std::expected<FileStream, Error> Wad::openEntry(const WadEntry& entry) const {
    // An install's override replaces the entry whole, whatever its size; Coney's own step before the original's read.
    if (const auto found = m_overrides.find(entry.nameHash); found != m_overrides.end()) {
        std::error_code ec;
        const std::uintmax_t size = std::filesystem::file_size(found->second, ec);
        if (ec) {
            return fail(ErrorCode::NotFound, std::format("{}: {}", found->second.string(), ec.message()));
        }
        return FileStream::open(found->second, 0, size);
    }
    return m_disc.openFileRange(kWadFile, entry.wadOffset, entry.size);
}

std::expected<WadOverrideReport, Error> Wad::useOverrides(const std::filesystem::path& folder) {
    WadOverrideReport report;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return report; // no overrides folder: the game as it is on the disc
    }
    // The files' names, sorted, so which of two names for one entry wins does not depend on the file system's order.
    std::vector<std::pair<std::string, std::filesystem::path>> files;
    for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec)) {
            const std::u8string name = it->path().filename().u8string();
            files.emplace_back(std::string(name.begin(), name.end()), it->path());
        }
    }
    if (ec) {
        return fail(ErrorCode::NotFound,
                    std::format("{}: cannot list the overrides: {}", folder.string(), ec.message()));
    }
    std::ranges::sort(files);
    for (auto& [name, path] : files) {
        auto entry = lookup(name);
        if (!entry) {
            report.unmatched.push_back(name);
            continue;
        }
        // Never a script: the player's own disc chunks are the only bytecode the game state runs
        // (docs/guides/studio/security.md#surface). The WAD names no kinds, so a script is told by its bytes, the
        // disc entry's and the file's; either one unreadable refuses the file too.
        std::error_code sizeError;
        const std::uintmax_t fileSize = std::filesystem::file_size(path, sizeError);
        const StreamKind onDisc = streamKind(m_disc.openFileRange(kWadFile, (*entry)->wadOffset, (*entry)->size));
        const StreamKind inFile = sizeError ? StreamKind::Unreadable : streamKind(FileStream::open(path, 0, fileSize));
        if (onDisc == StreamKind::Chunk || inFile == StreamKind::Chunk) {
            report.scripts.push_back(name);
        } else if (onDisc == StreamKind::Unreadable || inFile == StreamKind::Unreadable) {
            report.unreadable.push_back(name);
        } else if (!m_overrides.try_emplace((*entry)->nameHash, std::move(path)).second) {
            report.duplicates.push_back(name);
        } else {
            ++report.replaced;
        }
    }
    return report;
}

} // namespace coney::io
