// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/wad.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <system_error>
#include <utility>

namespace coney::io {

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

std::expected<FileStream, Error> Wad::openEntry(const WadEntry& entry) const {
    return m_disc.openFileRange(kWadFile, entry.wadOffset, entry.size);
}

} // namespace coney::io
