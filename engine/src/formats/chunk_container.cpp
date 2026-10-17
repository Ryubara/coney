#include "formats/chunk_container.hpp"

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

Result<ResourceHeader, ParseError> read_resource_header(Bytes bytes) {
    if (bytes.size() < kHeaderSize) {
        return Failure(ParseError::TruncatedHeader);
    }
    if (load_u32(bytes, 0x08) != 0) {
        return Failure(ParseError::NonZeroReserved);
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
    // Every chunk takes at least its header, so a larger count cannot be true.
    if (header->chunk_count > rest.size() / kHeaderSize) {
        return Failure(ParseError::CountExceedsBytes);
    }

    ResourceRead result;
    result.resource.hash = header->hash;
    result.resource.chunks.reserve(header->chunk_count);
    std::uint64_t payload_total = 0;
    std::size_t position = kHeaderSize;

    for (std::uint32_t index = 0; index < header->chunk_count; ++index) {
        Bytes at = bytes.subspan(position);
        if (at.size() < kHeaderSize) {
            return Failure(ParseError::TruncatedHeader);
        }
        const std::uint32_t type = load_u32(at, 0x00);
        const std::uint32_t size = load_u32(at, 0x04);
        if (type > chunk_type::kHighest) {
            return Failure(ParseError::UnknownChunkType);
        }
        if (load_u32(at, 0x08) != 0) {
            return Failure(ParseError::NonZeroReserved);
        }
        if (size % kPayloadAlignment != 0) {
            return Failure(ParseError::MisalignedSize);
        }
        if (size > at.size() - kHeaderSize) {
            return Failure(ParseError::PayloadPastEnd);
        }
        result.resource.chunks.push_back({type, load_u32(at, 0x0C), at.subspan(kHeaderSize, size)});
        payload_total += size;
        position += kHeaderSize + size;
    }

    if (payload_total != header->payload_sum) {
        return Failure(ParseError::ChunkSumMismatch);
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
        return Failure(ParseError::TrailingBytes);
    }
    return std::move(read->resource);
}

Result<Pack, ParseError> parse_pack(Bytes bytes) {
    if (bytes.size() < kHeaderSize) {
        return Failure(ParseError::TruncatedHeader);
    }
    if (load_u32(bytes, 0x0C) != kPackEndMarker) {
        return Failure(ParseError::BadEndMarker);
    }
    if (load_u32(bytes, 0x08) != 0) {
        return Failure(ParseError::NonZeroReserved);
    }
    const std::uint32_t count = load_u32(bytes, 0x00);
    Bytes body = bytes.subspan(kHeaderSize);
    if (load_u32(bytes, 0x04) != body.size()) {
        return Failure(ParseError::PackSizeMismatch);
    }
    if (count > body.size() / kHeaderSize) {
        return Failure(ParseError::CountExceedsBytes);
    }

    Pack pack;
    pack.resources.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        auto read = read_resource(body);
        if (!read) {
            return Failure(read.error());
        }
        body = body.subspan(read->consumed);
        pack.resources.push_back(std::move(read->resource));
    }
    if (!body.empty()) {
        return Failure(ParseError::TrailingBytes);
    }
    return pack;
}

const char* describe(ParseError error) noexcept {
    switch (error) {
    case ParseError::TruncatedHeader:
        return "truncated header";
    case ParseError::PayloadPastEnd:
        return "chunk payload runs past the end";
    case ParseError::BadEndMarker:
        return "wrong pack end marker";
    case ParseError::TrailingBytes:
        return "bytes left over";
    case ParseError::MisalignedSize:
        return "payload size is not a multiple of 16";
    case ParseError::NonZeroReserved:
        return "reserved word is not zero";
    case ParseError::UnknownChunkType:
        return "unknown chunk type";
    case ParseError::ChunkSumMismatch:
        return "payload sum does not match the chunks";
    case ParseError::PackSizeMismatch:
        return "pack size does not match the bytes after the header";
    case ParseError::CountExceedsBytes:
        return "count does not fit in the bytes present";
    }
    return "unknown error";
}

} // namespace coney::formats
