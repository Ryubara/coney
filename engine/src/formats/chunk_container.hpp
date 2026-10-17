#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "result.hpp"

namespace coney::formats {

// Chunk type ids named by research/pages/chunk-container/page.md. Ids not listed there may still occur in a
// container; a chunk's type is kept as a plain number and these names are only for comparing.
namespace chunk_type {
inline constexpr std::uint32_t kAnimationData = 0x00;
inline constexpr std::uint32_t kClipDescriptor = 0x02;
inline constexpr std::uint32_t kLevel03 = 0x03;
inline constexpr std::uint32_t kLevel04 = 0x04;
inline constexpr std::uint32_t kLevel05 = 0x05;
inline constexpr std::uint32_t kLevel06 = 0x06;
inline constexpr std::uint32_t kLevel07 = 0x07;
inline constexpr std::uint32_t kCharacterData = 0x08;
inline constexpr std::uint32_t kWorldSection = 0x15;
inline constexpr std::uint32_t kLevel17 = 0x17;
inline constexpr std::uint32_t kBoneOffsets = 0x28;
inline constexpr std::uint32_t kGlobal29 = 0x29;
inline constexpr std::uint32_t kTextureDictionary = 0x2A;
inline constexpr std::uint32_t kGlobal31 = 0x31;
inline constexpr std::uint32_t kLevel40 = 0x40;
inline constexpr std::uint32_t kSceneList = 0x43;
inline constexpr std::uint32_t kGlobal44 = 0x44;
inline constexpr std::uint32_t kAnimRangeList = 0x45;
inline constexpr std::uint32_t kGlobal46 = 0x46;
inline constexpr std::uint32_t kModel = 0x47;
inline constexpr std::uint32_t kGlobal48 = 0x48;
inline constexpr std::uint32_t kGlobal49 = 0x49;
inline constexpr std::uint32_t kParticlePage = 0x4C;
inline constexpr std::uint32_t kGlobal4D = 0x4D;
inline constexpr std::uint32_t kGlobal4E = 0x4E;
inline constexpr std::uint32_t kGlobal4F = 0x4F;
inline constexpr std::uint32_t kLevel51 = 0x51;
inline constexpr std::uint32_t kLevel52 = 0x52;
inline constexpr std::uint32_t kLevel53 = 0x53;

inline constexpr std::uint32_t kHighest = 0x53;
} // namespace chunk_type

inline constexpr std::size_t kHeaderSize = 16;
inline constexpr std::size_t kPayloadAlignment = 16;
// The last word of every pack header: the CRC-32 of the text `package`.
inline constexpr std::uint32_t kPackHash = 0xDE686795;

enum class ErrorKind {
    TruncatedHeader,    // fewer bytes than a header needs
    PayloadPastEnd,     // a chunk payload runs beyond the available bytes
    BadPackHash,        // the pack header's last word is not kPackHash
    TrailingBytes,      // bytes remain after the last chunk or resource
    MisalignedSize,     // a payload size is not a multiple of 16
    NonZeroReserved,    // a header's always-zero word is not zero
    UnknownChunkType,   // a chunk type id above the highest known id
    ChunkSumMismatch,   // the resource header's payload sum differs from the chunks' sizes
    PackSizeMismatch,   // the pack header's size differs from the bytes after it
    CountExceedsBytes,  // a declared count cannot fit in the bytes present
};

// What was wrong and where: `offset` counts from the start of the bytes given to parse_flat, parse_pack or
// read_resource. It is the start of the header at fault, or for TrailingBytes the first byte left over.
struct ParseError {
    ErrorKind kind;
    std::size_t offset = 0;

    friend bool operator==(const ParseError&, const ParseError&) = default;
};

struct Chunk {
    std::uint32_t type = 0;
    std::uint32_t hash = 0;
    std::span<const std::byte> payload;
};

struct Resource {
    std::uint32_t hash = 0;
    std::vector<Chunk> chunks;
};

struct Pack {
    std::vector<Resource> resources;
};

// The parsed structures view the input: every payload span points into the bytes that were parsed, so those bytes
// must outlive the result. Nothing is copied.

// Reads a resource from the start of `bytes`; `consumed` is how many bytes it occupies. Bytes after it are left
// alone (they belong to the next resource of a pack).
struct ResourceRead {
    Resource resource;
    std::size_t consumed = 0;
};

[[nodiscard]] Result<ResourceRead, ParseError> read_resource(std::span<const std::byte> bytes);

// A flat container: one resource that fills `bytes` exactly.
[[nodiscard]] Result<Resource, ParseError> parse_flat(std::span<const std::byte> bytes);

// A grouped container: a pack header and its resources, filling `bytes` exactly.
[[nodiscard]] Result<Pack, ParseError> parse_pack(std::span<const std::byte> bytes);

[[nodiscard]] const char* describe(ErrorKind kind) noexcept;

// The error's description and its offset, for logs: "<description> at offset 0x<hex>".
[[nodiscard]] std::string describe(const ParseError& error);

} // namespace coney::formats
