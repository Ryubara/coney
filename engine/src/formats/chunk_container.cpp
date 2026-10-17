#include "formats/chunk_container.hpp"

#include <format>

namespace coney::formats {

namespace {

using Bytes = std::span<const std::byte>;

std::uint32_t load_u32(Bytes bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8 * i);
    }
    return value;
}

struct ResourceHeader {
    std::uint32_t chunk_count;
    std::uint32_t payload_sum;
    std::uint32_t hash;
};

Failure<ParseError> fail(ErrorKind kind, std::size_t offset) {
    return Failure(ParseError{kind, offset});
}

Result<ResourceHeader, ParseError> read_resource_header(Bytes bytes) {
    if (bytes.size() < kHeaderSize) {
        return fail(ErrorKind::TruncatedHeader, 0);
    }
    if (load_u32(bytes, 0x08) != 0) {
        return fail(ErrorKind::NonZeroReserved, 0);
    }
    return ResourceHeader{load_u32(bytes, 0x00), load_u32(bytes, 0x04), load_u32(bytes, 0x0C)};
}

} // namespace

// @orig 0x00144180 Read one resource's chunks from a flat container
// @page research/pages/chunk-container/page.md
Result<ResourceRead, ParseError> read_resource(Bytes bytes) {
    const auto header = read_resource_header(bytes);
    if (!header) {
        return Failure(header.error());
    }
    Bytes rest = bytes.subspan(kHeaderSize);
    if (header->chunk_count < kMinChunks) {
        return fail(ErrorKind::CountTooLow, 0);
    }
    // Every chunk takes at least its header, so a larger count cannot be true.
    if (header->chunk_count > rest.size() / kHeaderSize) {
        return fail(ErrorKind::CountExceedsBytes, 0);
    }

    ResourceRead result;
    result.resource.hash = header->hash;
    result.resource.chunks.reserve(header->chunk_count);
    std::uint64_t payload_total = 0;
    std::size_t position = kHeaderSize;

    for (std::uint32_t index = 0; index < header->chunk_count; ++index) {
        Bytes at = bytes.subspan(position);
        if (at.size() < kHeaderSize) {
            return fail(ErrorKind::TruncatedHeader, position);
        }
        const std::uint32_t type = load_u32(at, 0x00);
        const std::uint32_t size = load_u32(at, 0x04);
        if (type > chunk_type::kHighest) {
            return fail(ErrorKind::UnknownChunkType, position);
        }
        if (load_u32(at, 0x08) != 0) {
            return fail(ErrorKind::NonZeroReserved, position);
        }
        if (size % kPayloadAlignment != 0) {
            return fail(ErrorKind::MisalignedSize, position);
        }
        if (size > at.size() - kHeaderSize) {
            return fail(ErrorKind::PayloadPastEnd, position);
        }
        result.resource.chunks.push_back({type, load_u32(at, 0x0C), at.subspan(kHeaderSize, size)});
        payload_total += size;
        position += kHeaderSize + size;
    }

    if (payload_total != header->payload_sum) {
        return fail(ErrorKind::ChunkSumMismatch, 0);
    }
    result.consumed = position;
    return result;
}

Result<Resource, ParseError> parse_flat(Bytes bytes) {
    auto read = read_resource(bytes);
    if (!read) {
        return Failure(read.error());
    }
    if (read->consumed != bytes.size()) {
        return fail(ErrorKind::TrailingBytes, read->consumed);
    }
    return std::move(read->resource);
}

Result<Pack, ParseError> parse_pack(Bytes bytes) {
    if (bytes.size() < kHeaderSize) {
        return fail(ErrorKind::TruncatedHeader, 0);
    }
    if (load_u32(bytes, 0x0C) != kPackHash) {
        return fail(ErrorKind::BadPackHash, 0);
    }
    if (load_u32(bytes, 0x08) != 0) {
        return fail(ErrorKind::NonZeroReserved, 0);
    }
    const std::uint32_t count = load_u32(bytes, 0x00);
    if (load_u32(bytes, 0x04) != bytes.size() - kHeaderSize) {
        return fail(ErrorKind::PackSizeMismatch, 0);
    }
    if (count < kMinPackResources) {
        return fail(ErrorKind::CountTooLow, 0);
    }
    if (count > (bytes.size() - kHeaderSize) / kHeaderSize) {
        return fail(ErrorKind::CountExceedsBytes, 0);
    }

    Pack pack;
    pack.resources.reserve(count);
    std::size_t position = kHeaderSize;
    for (std::uint32_t index = 0; index < count; ++index) {
        auto read = read_resource(bytes.subspan(position));
        if (!read) {
            return fail(read.error().kind, position + read.error().offset);
        }
        position += read->consumed;
        pack.resources.push_back(std::move(read->resource));
    }
    if (position != bytes.size()) {
        return fail(ErrorKind::TrailingBytes, position);
    }
    return pack;
}

const char* describe(ErrorKind kind) noexcept {
    switch (kind) {
    case ErrorKind::TruncatedHeader:
        return "truncated header";
    case ErrorKind::PayloadPastEnd:
        return "chunk payload runs past the end";
    case ErrorKind::BadPackHash:
        return "pack header does not end with the hash of \"package\"";
    case ErrorKind::TrailingBytes:
        return "bytes left over";
    case ErrorKind::MisalignedSize:
        return "payload size is not a multiple of 16";
    case ErrorKind::NonZeroReserved:
        return "reserved word is not zero";
    case ErrorKind::UnknownChunkType:
        return "unknown chunk type";
    case ErrorKind::ChunkSumMismatch:
        return "payload sum does not match the chunks";
    case ErrorKind::PackSizeMismatch:
        return "pack size does not match the bytes after the header";
    case ErrorKind::CountExceedsBytes:
        return "count does not fit in the bytes present";
    case ErrorKind::CountTooLow:
        return "count is below the format's minimum";
    }
    return "unknown error";
}

std::string describe(const ParseError& error) {
    return std::format("{} at offset {:#x}", describe(error.kind), error.offset);
}

} // namespace coney::formats
