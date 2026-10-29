// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_index.h"

#include <algorithm>
#include <bit>
#include <format>
#include <string>
#include <utility>

#include "fileio/reader.h"

namespace coney::cpak {

namespace {

// The header's fields after the magic; the index hash is the last 32 of its 64 bytes.
struct Header {
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint32_t alignment = 0;
    std::uint32_t entryCount = 0;
    std::uint32_t indexSize = 0;
    std::uint32_t reserved = 0;
    std::uint64_t archiveSize = 0;
    Sha256Digest indexHash{};
};

// The bytes the index hash covers before the index: the header up to the hash.
constexpr std::size_t kHashedHeaderBytes = 32;

// Reads a little-endian 64-bit value; Reader has no 64-bit read, and the format's sizes need one.
std::expected<std::uint64_t, Error> readU64Le(io::Reader& reader) {
    auto low = reader.readU32Le();
    if (!low) {
        return std::unexpected(low.error());
    }
    auto high = reader.readU32Le();
    if (!high) {
        return std::unexpected(high.error());
    }
    return static_cast<std::uint64_t>(*low) | (static_cast<std::uint64_t>(*high) << 32);
}

// Copies 32 bytes from the reader into a digest.
std::expected<Sha256Digest, Error> readDigest(io::Reader& reader) {
    auto bytes = reader.readBytes(32);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    Sha256Digest digest{};
    std::ranges::transform(*bytes, digest.begin(), [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return digest;
}

// Reads and checks the fixed header: magic, version, flags, alignment and reserved bytes. The sizes are checked
// against the archive by the caller, which knows it.
std::expected<Header, Error> readHeader(std::span<const std::byte> front) {
    if (front.size() < kHeaderSize) {
        return fail(ErrorCode::Truncated, "cpak: shorter than its 64-byte header");
    }
    if (!std::ranges::equal(front.first(4), kMagic)) {
        return fail(ErrorCode::Invalid, "cpak: not a Coney Pack (no CPAK magic)");
    }
    io::Reader reader(front.first(kHeaderSize));
    // The header holds 64 bytes, which the size check above guarantees, so none of these reads can fail.
    static_cast<void>(reader.seek(4));
    Header header;
    header.version = reader.readU16Le().value();
    header.flags = reader.readU16Le().value();
    header.alignment = reader.readU32Le().value();
    header.entryCount = reader.readU32Le().value();
    header.indexSize = reader.readU32Le().value();
    header.reserved = reader.readU32Le().value();
    header.archiveSize = readU64Le(reader).value();
    header.indexHash = readDigest(reader).value();
    if (header.version != kVersion) {
        return fail(ErrorCode::Invalid,
                    std::format("cpak: format version {}; this reader knows only {}", header.version, kVersion));
    }
    if (header.flags != 0 || header.reserved != 0) {
        return fail(ErrorCode::Invalid, "cpak: unknown header flags or reserved bytes set");
    }
    if (!std::has_single_bit(header.alignment) || header.alignment < kMinAlignment ||
        header.alignment > kMaxAlignment) {
        return fail(ErrorCode::Invalid, std::format("cpak: alignment {} is not a power of two from {} to {}",
                                                    header.alignment, kMinAlignment, kMaxAlignment));
    }
    return header;
}

// Reads one record of the index and its name from the name table. Every read lies inside `index`, whose size the
// caller checked against the record count.
std::expected<Entry, Error> readRecord(io::Reader& records, std::span<const std::byte> names) {
    Entry entry;
    entry.offset = readU64Le(records).value();
    entry.storedSize = readU64Le(records).value();
    entry.size = readU64Le(records).value();
    const std::uint32_t nameOffset = records.readU32Le().value();
    const std::uint16_t nameLength = records.readU16Le().value();
    const std::uint8_t compression = records.readU8().value();
    const std::uint8_t flags = records.readU8().value();
    entry.sha256 = readDigest(records).value();
    if (compression > static_cast<std::uint8_t>(Compression::Zstd)) {
        return fail(ErrorCode::Invalid, std::format("cpak: unknown compression {}", compression));
    }
    entry.compression = static_cast<Compression>(compression);
    if (flags != 0) {
        return fail(ErrorCode::Invalid, "cpak: unknown entry flags set");
    }
    if (nameOffset > names.size() || nameLength > names.size() - nameOffset) {
        return fail(ErrorCode::Invalid, "cpak: an entry's name lies outside the name table");
    }
    const auto name = names.subspan(nameOffset, nameLength);
    entry.name.resize(name.size());
    std::ranges::transform(name, entry.name.begin(), [](std::byte b) { return static_cast<char>(b); });
    if (auto checked = checkName(entry.name); !checked) {
        return std::unexpected(checked.error());
    }
    if (entry.compression == Compression::None && entry.size != entry.storedSize) {
        return fail(ErrorCode::Invalid,
                    std::format("cpak: {} is stored as it is but its two sizes differ", entry.name));
    }
    return entry;
}

// Checks the entries against each other and the archive: names in strictly rising byte order (so unique), every
// entry aligned, after the index, inside the archive and overlapping no other.
std::expected<void, Error> checkLayout(const std::vector<Entry>& entries, const Header& header,
                                       std::uint64_t frontSize) {
    for (std::size_t i = 1; i < entries.size(); ++i) {
        if (!(entries[i - 1].name < entries[i].name)) {
            return fail(ErrorCode::Invalid, std::format("cpak: {} is out of name order or repeated", entries[i].name));
        }
    }
    for (const Entry& entry : entries) {
        if (entry.offset % header.alignment != 0) {
            return fail(ErrorCode::Invalid, std::format("cpak: {} is not aligned to {}", entry.name, header.alignment));
        }
        if (entry.offset < frontSize || entry.offset > header.archiveSize ||
            entry.storedSize > header.archiveSize - entry.offset) {
            return fail(ErrorCode::Invalid, std::format("cpak: {} lies outside the archive's data", entry.name));
        }
    }
    // Sorted by offset, each non-empty entry must end before the next begins; an empty entry overlaps nothing.
    std::vector<const Entry*> byOffset;
    for (const Entry& entry : entries) {
        if (entry.storedSize > 0) {
            byOffset.push_back(&entry);
        }
    }
    std::ranges::sort(byOffset, {}, &Entry::offset);
    for (std::size_t i = 1; i < byOffset.size(); ++i) {
        if (byOffset[i - 1]->offset + byOffset[i - 1]->storedSize > byOffset[i]->offset) {
            return fail(ErrorCode::Invalid,
                        std::format("cpak: {} overlaps {}", byOffset[i - 1]->name, byOffset[i]->name));
        }
    }
    return {};
}

// True when `text` is well-formed UTF-8 (RFC 3629, as Python's strict decoder reads it): no stray continuation
// byte, no sequence cut short, no overlong form, no surrogate and nothing past U+10FFFF. The page asks this of every
// name so the Python tools, and any file system an archive is exported to, read a name the same way.
bool isUtf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        if (lead < 0x80) {
            ++i;
            continue;
        }
        // The sequence's length and the range its second byte must lie in, which rules out the overlong forms, the
        // surrogates (ED A0..BF) and code points past U+10FFFF (F4 90..BF).
        std::size_t length = 0;
        unsigned char low = 0x80;
        unsigned char high = 0xbf;
        if (lead >= 0xc2 && lead <= 0xdf) {
            length = 2;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            length = 3;
            low = lead == 0xe0 ? 0xa0 : 0x80;
            high = lead == 0xed ? 0x9f : 0xbf;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            length = 4;
            low = lead == 0xf0 ? 0x90 : 0x80;
            high = lead == 0xf4 ? 0x8f : 0xbf;
        } else {
            return false;
        }
        if (text.size() - i < length) {
            return false;
        }
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if (second < low || second > high) {
            return false;
        }
        for (std::size_t k = 2; k < length; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if (next < 0x80 || next > 0xbf) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

} // namespace

std::string_view compressionName(Compression compression) {
    switch (compression) {
    case Compression::None:
        return "none";
    case Compression::Lz4:
        return "lz4";
    case Compression::Zstd:
        return "zstd";
    }
    return "unknown";
}

std::expected<void, Error> checkName(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameBytes) {
        return fail(ErrorCode::Invalid, std::format("cpak: a name must be 1 to {} bytes long", kMaxNameBytes));
    }
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f || c == '\\' || c == ':') {
            return fail(ErrorCode::Invalid, "cpak: a name holds a control character, a backslash or a colon");
        }
    }
    if (!isUtf8(name)) {
        return fail(ErrorCode::Invalid, "cpak: a name is not well-formed UTF-8");
    }
    // Every segment between slashes is a real name: not empty (no leading, trailing or double slash), not . or ..
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = std::min(name.find('/', start), name.size());
        const std::string_view segment = name.substr(start, slash - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return fail(ErrorCode::Invalid, std::format("cpak: the name {} has an empty, . or .. segment", name));
        }
        start = slash + 1;
    }
    return {};
}

std::expected<Index, Error> Index::parse(std::span<const std::byte> front, std::uint64_t archiveSize) {
    auto header = readHeader(front);
    if (!header) {
        return std::unexpected(header.error());
    }
    // The archive's size is checked before anything is allocated, so a damaged count cannot reserve gigabytes.
    if (archiveSize < header->archiveSize) {
        return fail(ErrorCode::Truncated,
                    std::format("cpak: the archive is {} bytes; its header says {}", archiveSize, header->archiveSize));
    }
    if (archiveSize > header->archiveSize) {
        return fail(ErrorCode::Invalid,
                    std::format("cpak: the archive is {} bytes; its header says {}", archiveSize, header->archiveSize));
    }
    const std::uint64_t frontSize = kHeaderSize + std::uint64_t{header->indexSize};
    if (frontSize > header->archiveSize) {
        return fail(ErrorCode::Invalid, "cpak: the index runs past the end of the archive");
    }
    if (front.size() < frontSize) {
        return fail(ErrorCode::Truncated, "cpak: the bytes given end inside the index");
    }
    const auto index = front.subspan(kHeaderSize, header->indexSize);

    // The hash first: a damaged index is refused as damaged, whatever its records say.
    Sha256 sha;
    sha.update(front.first(kHashedHeaderBytes));
    sha.update(index);
    if (sha.finish() != header->indexHash) {
        return fail(ErrorCode::Invalid, "cpak: the index does not match its SHA-256 (a damaged index)");
    }
    if (std::uint64_t{header->entryCount} * kRecordSize > index.size()) {
        return fail(ErrorCode::Invalid, "cpak: the index is smaller than its records");
    }

    const std::size_t recordsSize = std::size_t{header->entryCount} * kRecordSize;
    io::Reader records(index.first(recordsSize));
    const auto names = index.subspan(recordsSize);
    Index result;
    result.m_entries.reserve(header->entryCount);
    for (std::uint32_t i = 0; i < header->entryCount; ++i) {
        auto entry = readRecord(records, names);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        result.m_entries.push_back(std::move(*entry));
    }
    if (auto layout = checkLayout(result.m_entries, *header, frontSize); !layout) {
        return std::unexpected(layout.error());
    }
    result.m_alignment = header->alignment;
    result.m_archiveSize = header->archiveSize;
    result.m_indexSize = header->indexSize;
    return result;
}

std::expected<Index, Error> Index::read(io::Stream& stream) {
    // The header first, to learn how much index follows; then header and index together, as parse() wants them.
    std::vector<std::byte> front(std::min<std::uint64_t>(kHeaderSize, stream.size()));
    if (auto seeked = stream.seek(0); !seeked) {
        return std::unexpected(seeked.error());
    }
    if (auto got = stream.read(front); !got) {
        return std::unexpected(got.error());
    }
    auto header = readHeader(front);
    if (!header) {
        return std::unexpected(header.error());
    }
    // Only as much as the stream holds, so a damaged index size cannot make us allocate more than the file.
    const std::uint64_t frontSize = kHeaderSize + std::uint64_t{header->indexSize};
    if (frontSize > stream.size()) {
        return fail(ErrorCode::Truncated, "cpak: the archive ends inside its index");
    }
    front.resize(static_cast<std::size_t>(frontSize));
    if (auto got = stream.read(std::span(front).subspan(kHeaderSize)); !got) {
        return std::unexpected(got.error());
    }
    return parse(front, stream.size());
}

const Entry* Index::find(std::string_view name) const {
    const auto found = std::ranges::lower_bound(m_entries, name, {}, &Entry::name);
    return found != m_entries.end() && found->name == name ? &*found : nullptr;
}

std::expected<void, Error> verifyEntry(const Entry& entry, std::span<const std::byte> stored) {
    if (entry.compression != Compression::None) {
        return fail(ErrorCode::Invalid, std::format("cpak: {} is compressed ({}), which this reader cannot decode",
                                                    entry.name, compressionName(entry.compression)));
    }
    if (stored.size() != entry.storedSize) {
        return fail(ErrorCode::Invalid, std::format("cpak: {} is {} bytes; its record says {}", entry.name,
                                                    stored.size(), entry.storedSize));
    }
    Sha256 sha;
    sha.update(stored);
    if (sha.finish() != entry.sha256) {
        return fail(ErrorCode::Invalid,
                    std::format("cpak: {} does not match its SHA-256 (a damaged entry)", entry.name));
    }
    return {};
}

std::expected<std::vector<std::byte>, Error> readEntry(io::Stream& stream, const Entry& entry) {
    // Refuse a compressed entry before reading it, as verifyEntry() would after.
    if (entry.compression != Compression::None) {
        return std::unexpected(verifyEntry(entry, {}).error());
    }
    if (auto seeked = stream.seek(entry.offset); !seeked) {
        return std::unexpected(seeked.error());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(entry.storedSize));
    if (auto got = stream.read(bytes); !got) {
        return std::unexpected(got.error());
    }
    if (auto verified = verifyEntry(entry, bytes); !verified) {
        return std::unexpected(verified.error());
    }
    return bytes;
}

} // namespace coney::cpak
