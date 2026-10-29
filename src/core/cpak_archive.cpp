// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_archive.h"

#include <format>

#include "core/assert.h"

namespace coney::cpak {

std::expected<Archive, Error> Archive::open(std::span<const std::byte> bytes) {
    auto index = Index::parse(bytes, bytes.size());
    if (!index) {
        return std::unexpected(index.error());
    }
    return Archive(std::move(*index), bytes);
}

std::expected<std::span<const std::byte>, Error> Archive::view(const Entry& entry) const {
    if (entry.compression != Compression::None) {
        return fail(ErrorCode::Invalid, std::format("cpak: {} is compressed ({}) and cannot be read in place",
                                                    entry.name, compressionName(entry.compression)));
    }
    // open() checked every entry of this archive lies inside it; an Entry from another archive is a caller's bug.
    CONEY_ASSERT(entry.offset <= m_bytes.size() && entry.storedSize <= m_bytes.size() - entry.offset);
    return m_bytes.subspan(static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.storedSize));
}

std::expected<std::span<const std::byte>, Error> Archive::verified(const Entry& entry) const {
    auto bytes = view(entry);
    if (!bytes) {
        return bytes;
    }
    if (auto checked = verifyEntry(entry, *bytes); !checked) {
        return std::unexpected(checked.error());
    }
    return bytes;
}

std::expected<std::span<const std::byte>, Error> Archive::read(std::string_view name) const {
    const Entry* entry = m_index.find(name);
    if (entry == nullptr) {
        return fail(ErrorCode::NotFound, std::format("cpak: no entry {}", name));
    }
    return verified(*entry);
}

} // namespace coney::cpak
