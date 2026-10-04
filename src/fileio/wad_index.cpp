// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/wad_index.h"

#include <format>

#include "core/assert.h"
#include "core/name_hash.h"
#include "fileio/reader.h"

namespace coney::io {

std::string wadPath(std::string_view name) {
    if (name.starts_with("./")) {
        return std::string(name);
    }
    std::string path(kWadNamePrefix);
    path += name;
    return path;
}

std::expected<WadIndex, Error> WadIndex::parse(std::span<const std::byte> dirData,
                                               std::optional<std::uint64_t> wadSize) {
    if (dirData.size() < kWadDirHeaderSize) {
        return fail(ErrorCode::Truncated, std::format("WARRIORS.DIR is {} bytes long; the header alone is {}",
                                                      dirData.size(), kWadDirHeaderSize));
    }
    Reader reader(dirData);
    const std::uint32_t count = reader.readU32Le().value(); // the size check above covers it
    // 64-bit arithmetic: a damaged count cannot overflow the expected size.
    const std::uint64_t expected = kWadDirHeaderSize + std::uint64_t{count} * kWadDirEntrySize;
    if (dirData.size() != expected) {
        return fail(dirData.size() < expected ? ErrorCode::Truncated : ErrorCode::Invalid,
                    std::format("WARRIORS.DIR says {} entries ({} bytes) but the file has {} bytes", count, expected,
                                dirData.size()));
    }
    // The 12 bytes after the count are padding: the entries start at 0x10.
    reader.seek(kWadDirHeaderSize).value();

    WadIndex index;
    index.m_entries.reserve(count);
    index.m_byHash.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        // The exact-size check above covers every read in this loop, so these cannot fail.
        WadEntry entry;
        entry.index = i;
        entry.wadOffset = reader.readU32Le().value();
        entry.size = reader.readU32Le().value();
        entry.nameHash = reader.readU32Le().value();
        if (wadSize && std::uint64_t{entry.wadOffset} + entry.size > *wadSize) {
            return fail(ErrorCode::Invalid,
                        std::format("WARRIORS.DIR entry {} ({:08x}) ends at {}, past the end of WARRIORS.WAD ({})", i,
                                    entry.nameHash, std::uint64_t{entry.wadOffset} + entry.size, *wadSize));
        }
        index.m_entries.push_back(entry);
        index.m_byHash.emplace(entry.nameHash, i); // emplace keeps the first: the original scan stops there
    }
    CONEY_ASSERT(index.m_entries.size() == count);
    return index;
}

const WadEntry* WadIndex::find(std::string_view name) const { return findHash(nameHash(wadPath(name))); }

const WadEntry* WadIndex::findHash(std::uint32_t hash) const {
    auto it = m_byHash.find(hash);
    return it == m_byHash.end() ? nullptr : &m_entries[it->second];
}

} // namespace coney::io
