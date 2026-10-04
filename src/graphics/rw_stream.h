// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/error.h"
#include "fileio/reader.h"

// RenderWare binary streams, checked without RenderWare: the structure of the streams the game's texture dictionaries
// are stored in. librw does the real reading (src/platform/texture_dictionary.h); this code runs first, so that a
// damaged or unexpected stream is reported as an Error instead of reaching librw, which asserts or reads out of bounds
// on data it does not expect. The format is RenderWare's own, as librw reads it; what the WAD holds is in
// docs/research/formats/wad-contents.md#renderware.

namespace coney::graphics {

/// RenderWare section ids used by texture dictionaries.
inline constexpr std::uint32_t kRwStruct = 0x01;
inline constexpr std::uint32_t kRwString = 0x02;
inline constexpr std::uint32_t kRwExtension = 0x03;
inline constexpr std::uint32_t kRwTextureNative = 0x15;
inline constexpr std::uint32_t kRwTexDictionary = 0x16;

/// Bytes of a RenderWare section header: id, size, library stamp.
inline constexpr std::size_t kRwHeaderSize = 12;

/// The library stamp of nearly every RenderWare stream in the WAD: RenderWare 3.7.0.2, build 0x000a.
inline constexpr std::uint32_t kRwLibraryStamp = 0x1C02000A;

/// The platform word that starts a PS2 native texture: "PS2\0" read as a little-endian 32-bit value.
inline constexpr std::uint32_t kPs2NativeTexture = 0x00325350;

/// The 12 bytes before every RenderWare section.
struct RwSectionHeader {
    std::uint32_t id = 0;           ///< What the section is (kRwStruct, kRwTexDictionary, ...).
    std::uint32_t size = 0;         ///< Bytes of section data after this header.
    std::uint32_t libraryStamp = 0; ///< Packed RenderWare version and build that wrote it.
};

/// Reads a section header at the reader's position. Fails with ErrorCode::Truncated when fewer than 12 bytes remain.
[[nodiscard]] std::expected<RwSectionHeader, Error> readRwSectionHeader(io::Reader& reader);

/// The 16 bytes before the RenderWare stream of a "headered" WAD entry
/// (docs/research/chunk-system.md#container-layout):
/// `{1, 0, 0, id}`, then the stream itself.
struct HeaderedRwStream {
    std::uint32_t id = 0;         ///< The header's fourth word; meaning unknown (chunk-system.md, open questions).
    RwSectionHeader firstSection; ///< The stream's first section header.
};

/// Bytes before the RenderWare stream in a headered entry.
inline constexpr std::size_t kHeaderedRwStreamOffset = 16;

/// Recognises a WAD entry that is a 16-byte header `{1, 0, 0, id}` followed directly by a RenderWare stream stamped
/// kRwLibraryStamp. `entryStart` is the start of the entry; 28 bytes are enough. Returns nothing for anything else,
/// including chunk containers, whose second word is a non-zero byte count.
[[nodiscard]] std::optional<HeaderedRwStream> detectHeaderedRwStream(std::span<const std::byte> entryStart);

/// Bytes before the texture dictionary in a world stream (`%s_sec.wld`): one `u32`, the count of sector atomics files.
inline constexpr std::size_t kWorldStreamDictionaryOffset = 4;

/// Recognises a world stream (docs/research/graphics.md#loading-textures): a `u32` followed directly by a texture
/// dictionary section stamped kRwLibraryStamp, then the world. `entryStart` is the start of the entry; 16 bytes are
/// enough.
[[nodiscard]] bool isWorldStream(std::span<const std::byte> entryStart);

/// Finds the first top-level section with `id` in `stream`, skipping the sections before it, as RenderWare's
/// RwStreamFindChunk does. Returns the offset of its header. Fails with ErrorCode::NotFound when the stream ends first
/// and ErrorCode::Truncated when a section runs past the end.
[[nodiscard]] std::expected<std::size_t, Error> findRwSection(std::span<const std::byte> stream, std::uint32_t id);

/// What a PS2 native texture says about itself, from its headers. Field meanings follow librw's reader
/// (src/ps2/ps2raster.cpp, readNativeTexture).
struct Ps2TextureInfo {
    std::string name;                   ///< The texture's name, at most 31 characters.
    std::string mask;                   ///< The name of its alpha mask texture; usually empty.
    std::uint32_t filterAddressing = 0; ///< Filter mode and U/V addressing, packed as RenderWare does.
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;        ///< Bits per pixel: 4 or 8 (palettised), 16 or 32.
    std::uint32_t rasterFormat = 0; ///< RenderWare raster format and type bits (Raster::C8888, PAL8, ...).
    std::int32_t version = 0;       ///< The raster's layout version: below 2 is the older layout without GIF packets.
    std::uint32_t pixelBytes = 0;   ///< Bytes of pixel data, mipmaps and GIF packets included.
    std::uint32_t paletteBytes = 0; ///< Bytes of palette data; 0 without a palette.
    std::uint32_t rasterLibraryStamp = 0; ///< The raster header section's library stamp; librw reads by its version.
    std::size_t sectionOffset = 0;        ///< Where its texture native section starts in the dictionary stream.
    std::size_t sectionBytes = 0;         ///< The size of that section, its header included.
};

/// What a texture dictionary holds, from its headers.
struct TexDictionaryInfo {
    std::uint16_t deviceId = 0;           ///< The device it was made for; 6 is the PS2.
    std::uint32_t libraryStamp = 0;       ///< The dictionary section's library stamp.
    std::size_t streamBytes = 0;          ///< Bytes of the whole dictionary, header included.
    std::vector<Ps2TextureInfo> textures; ///< In stream order.
};

/// Checks that `stream`, which starts with a texture dictionary section (kRwTexDictionary), is laid out as librw's
/// reader expects for PS2 native textures, and describes it. Every size is checked against its parent's before it is
/// used. Fails with ErrorCode::Truncated when a section runs past its parent and ErrorCode::Invalid for a section out
/// of place, a non-PS2 texture, a name longer than librw's 32-byte field, a raster format librw cannot convert or
/// pixel data whose size disagrees with the raster header. Bytes after the dictionary are ignored.
[[nodiscard]] std::expected<TexDictionaryInfo, Error> inspectTexDictionary(std::span<const std::byte> stream);

} // namespace coney::graphics
