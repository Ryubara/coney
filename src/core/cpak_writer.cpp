// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/cpak_writer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <string_view>
#include <utility>

namespace coney::cpak {

namespace {

// Appends `value` little-endian, `width` bytes of it.
void putLe(std::vector<std::byte>& out, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xffU));
    }
}

// Appends a digest's 32 bytes.
void putDigest(std::vector<std::byte>& out, const Sha256Digest& digest) {
    for (const std::uint8_t b : digest) {
        out.push_back(static_cast<std::byte>(b));
    }
}

// `name` with ASCII letters in lower case: two names that fold alike would be one file when exported to a folder on
// Windows or macOS, so an archive never holds both.
std::string folded(std::string_view name) {
    std::string out(name);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// `value` rounded up to a multiple of `alignment`, a power of two.
std::uint64_t alignUp(std::uint64_t value, std::uint32_t alignment) {
    return (value + alignment - 1) & ~std::uint64_t{alignment - 1U};
}

// Writes `count` zero bytes to the sink, in pieces of a fixed buffer.
std::expected<void, Error> writeZeros(const Sink& sink, std::uint64_t count) {
    static constexpr std::array<std::byte, 4096> kZeros{};
    while (count > 0) {
        const auto piece = static_cast<std::size_t>(std::min<std::uint64_t>(count, kZeros.size()));
        if (auto put = sink(std::span(kZeros).first(piece)); !put) {
            return put;
        }
        count -= piece;
    }
    return {};
}

} // namespace

std::expected<void, Error> Writer::add(std::string name, std::span<const std::byte> bytes) {
    Sha256 sha;
    sha.update(bytes);
    return add(std::move(name), bytes.size(), sha.finish(), [bytes](const Sink& sink) { return sink(bytes); });
}

std::expected<void, Error> Writer::add(std::string name, std::uint64_t size, const Sha256Digest& sha256, Fetch fetch) {
    if (auto checked = checkName(name); !checked) {
        return checked;
    }
    const auto [at, added] = m_folded.try_emplace(folded(name), name);
    if (!added) {
        return fail(ErrorCode::Invalid, std::format("cpak: {} is already in the archive as {} (names must differ by "
                                                    "more than case)",
                                                    name, at->second));
    }
    m_pending.push_back(Pending{std::move(name), size, sha256, std::move(fetch)});
    return {};
}

std::expected<void, Error> Writer::write(const Sink& sink) const {
    if (!std::has_single_bit(m_alignment) || m_alignment < kMinAlignment || m_alignment > kMaxAlignment) {
        return fail(ErrorCode::InvalidArgument, std::format("cpak: alignment {} is not a power of two from {} to {}",
                                                            m_alignment, kMinAlignment, kMaxAlignment));
    }
    // Entries in name order (byte order), which the reader requires and its lookup relies on.
    std::vector<const Pending*> order;
    order.reserve(m_pending.size());
    for (const Pending& pending : m_pending) {
        order.push_back(&pending);
    }
    std::ranges::sort(order, {}, &Pending::name);

    // Lay out the index: the records, then the names in the same order, then each entry at the next aligned offset.
    std::uint64_t namesSize = 0;
    for (const Pending* pending : order) {
        namesSize += pending->name.size();
    }
    const std::uint64_t indexSize = order.size() * kRecordSize + namesSize;
    if (indexSize > 0xffffffffU || order.size() > 0xffffffffU) {
        return fail(ErrorCode::InvalidArgument, "cpak: too many entries for one index");
    }
    std::vector<std::uint64_t> offsets;
    std::uint64_t cursor = kHeaderSize + indexSize;
    for (const Pending* pending : order) {
        cursor = alignUp(cursor, m_alignment);
        offsets.push_back(cursor);
        cursor += pending->size;
    }
    const std::uint64_t archiveSize = cursor;

    std::vector<std::byte> index;
    index.reserve(static_cast<std::size_t>(indexSize));
    std::uint64_t nameAt = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        putLe(index, offsets[i], 8);
        putLe(index, order[i]->size, 8); // stored size: every entry is written as it is
        putLe(index, order[i]->size, 8);
        putLe(index, nameAt, 4);
        putLe(index, order[i]->name.size(), 2);
        putLe(index, static_cast<std::uint8_t>(Compression::None), 1);
        putLe(index, 0, 1); // flags
        putDigest(index, order[i]->sha256);
        nameAt += order[i]->name.size();
    }
    for (const Pending* pending : order) {
        for (const char c : pending->name) {
            index.push_back(static_cast<std::byte>(c));
        }
    }

    // The header; its last 32 bytes are the SHA-256 of its first 32 and the index.
    std::vector<std::byte> header(kMagic.begin(), kMagic.end());
    putLe(header, kVersion, 2);
    putLe(header, 0, 2); // flags
    putLe(header, m_alignment, 4);
    putLe(header, order.size(), 4);
    putLe(header, indexSize, 4);
    putLe(header, 0, 4); // reserved
    putLe(header, archiveSize, 8);
    Sha256 indexSha;
    indexSha.update(header);
    indexSha.update(index);
    putDigest(header, indexSha.finish());

    if (auto put = sink(header); !put) {
        return put;
    }
    if (auto put = sink(index); !put) {
        return put;
    }
    // The entries: padding up to each one's offset, then its bytes, counted and hashed as they pass so a source
    // that changed since add() is refused rather than written under the wrong hash.
    std::uint64_t written = kHeaderSize + indexSize;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const Pending& pending = *order[i];
        if (auto padded = writeZeros(sink, offsets[i] - written); !padded) {
            return padded;
        }
        std::uint64_t got = 0;
        Sha256 sha;
        const Sink counting = [&](std::span<const std::byte> piece) -> std::expected<void, Error> {
            if (piece.size() > pending.size - got) {
                return fail(ErrorCode::Invalid,
                            std::format("cpak: {} is longer than the {} bytes added", pending.name, pending.size));
            }
            got += piece.size();
            sha.update(piece);
            return sink(piece);
        };
        if (auto fetched = pending.fetch(counting); !fetched) {
            return fetched;
        }
        if (got != pending.size) {
            return fail(ErrorCode::Invalid,
                        std::format("cpak: {} gave {} bytes; {} were added", pending.name, got, pending.size));
        }
        if (sha.finish() != pending.sha256) {
            return fail(ErrorCode::Invalid,
                        std::format("cpak: {} changed since it was added (its SHA-256 differs)", pending.name));
        }
        written = offsets[i] + pending.size;
    }
    return {};
}

std::expected<std::vector<std::byte>, Error> Writer::toBytes() const {
    std::vector<std::byte> out;
    auto written = write([&out](std::span<const std::byte> piece) -> std::expected<void, Error> {
        out.insert(out.end(), piece.begin(), piece.end());
        return {};
    });
    if (!written) {
        return std::unexpected(written.error());
    }
    return out;
}

} // namespace coney::cpak
