// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/wad.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace coney::io {

namespace {

// Whether a stream starts with the signature of a precompiled Lua chunk (ESC "Lua"), as every script on the disc
// does. A stream too short to hold it, or one that cannot be read, is no chunk.
[[maybe_unused]] bool isLuaChunk(std::expected<FileStream, Error> stream) { // stub: unused until the fix
    if (!stream || stream->size() < 4) {
        return false;
    }
    std::array<std::byte, 4> head{};
    if (!stream->read(head)) {
        return false;
    }
    return head == std::array{std::byte{0x1b}, std::byte{'L'}, std::byte{'u'}, std::byte{'a'}};
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
        } else if (!m_overrides.try_emplace((*entry)->nameHash, std::move(path)).second) {
            report.duplicates.push_back(name);
        } else {
            ++report.replaced;
        }
    }
    return report;
}

} // namespace coney::io
