// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "graphics/rw_stream.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct TexDictionary;
struct Texture;
} // namespace rw

namespace coney::platform {

/// A texture as 8-bit RGBA pixels, rows top first: what a texture looks like once it has left the PS2's formats.
struct RgbaImage {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; ///< width * height * 4 bytes.
};

/// A RenderWare texture dictionary read with librw: a list of named textures with their rasters. Owns the librw
/// dictionary and destroys it (and its textures and rasters) when it goes. Move-only.
///
/// Everything here needs a running RenderEngine, of either backend: librw's PS2 raster code works on both, and only
/// convertForDrawing() needs the OpenGL one. A dictionary must be destroyed before the engine stops.
///
/// Research: docs/research/chunk-system.md#chunk-type-table (types 0x0B and 0x2A),
/// docs/research/formats/wad-contents.md#renderware
class TextureDictionary {
  public:
    /// Reads the texture dictionary that starts at the beginning of `stream` (its 0x16 section header). The stream
    /// is checked with graphics::inspectTexDictionary() first, so damaged or unexpected data fails as that function
    /// says rather than reaching librw; ErrorCode::Invalid if librw still refuses it. A texture whose pixel or palette
    /// data is larger than the buffers librw would make for it is left out of the dictionary and counted in
    /// skippedTextures(): librw would copy it past their end.
    [[nodiscard]] static std::expected<TextureDictionary, Error> read(std::span<const std::byte> stream);

    TextureDictionary(TextureDictionary&& other) noexcept;
    TextureDictionary& operator=(TextureDictionary&& other) noexcept;
    TextureDictionary(const TextureDictionary&) = delete;
    TextureDictionary& operator=(const TextureDictionary&) = delete;
    ~TextureDictionary();

    /// What the stream's headers said about the textures that were read, from the check before reading.
    [[nodiscard]] const graphics::TexDictionaryInfo& info() const { return m_info; }

    /// Textures left out because librw's PS2 reader would overrun its buffers on them (see read()).
    [[nodiscard]] std::size_t skippedTextures() const { return m_skipped; }

    /// The textures, in the dictionary's order. The pointers stay valid as long as this dictionary.
    [[nodiscard]] std::vector<rw::Texture*> textures() const;

    /// The librw dictionary, for the global texture lookup (platform/texture_lookup.h). Valid as long as this object.
    [[nodiscard]] rw::TexDictionary* rwDictionary() const { return m_dictionary; }

    /// Converts every texture to RGBA pixels, on either backend; palettes are expanded and the PS2's alpha range
    /// (0 to 128) is scaled to 0 to 255, as librw does. Fails with ErrorCode::Invalid naming the first texture librw
    /// could not convert.
    [[nodiscard]] std::expected<std::vector<RgbaImage>, Error> toImages() const;

    /// Replaces every texture's PS2 raster with one for the current platform (OpenGL textures), so it can be drawn.
    /// Only with the OpenGL backend running (checked by CONEY_ASSERT). Fails with ErrorCode::Invalid naming the first
    /// texture librw could not convert; the textures before it stay converted.
    [[nodiscard]] std::expected<void, Error> convertForDrawing();

  private:
    TextureDictionary(rw::TexDictionary* dictionary, graphics::TexDictionaryInfo info, std::size_t skipped)
        : m_dictionary(dictionary), m_info(std::move(info)), m_skipped(skipped) {}

    /// Destroys the librw dictionary, if this object still owns one.
    void destroy() noexcept;

    rw::TexDictionary* m_dictionary = nullptr; // owned; null after a move
    graphics::TexDictionaryInfo m_info;
    std::size_t m_skipped = 0;
};

/// A texture dictionary on the chunk stack, pushed by the 0x0B and 0x2A stream readers under type 0x0B.
class TextureDictionaryObject final : public chunk::LoadedObject {
  public:
    explicit TextureDictionaryObject(TextureDictionary dictionary) : m_dictionary(std::move(dictionary)) {}

    [[nodiscard]] std::string_view describe() const override { return "texture dictionary"; }

    /// The dictionary.
    [[nodiscard]] TextureDictionary& dictionary() { return m_dictionary; }

  private:
    TextureDictionary m_dictionary;
};

/// The stream reader of chunk type 0x0B (Texture Dictionary TID): finds the RenderWare texture dictionary section
/// (0x16) in the chunk, reads it and pushes it as a TextureDictionaryObject under type 0x0B. Fails as
/// graphics::findRwSection() and TextureDictionary::read() do.
///
/// Research: docs/research/chunk-system.md#chunk-type-table
/// @orig 0x001906e8 ChunkReader_TextureDictionaryTid (unknown)
[[nodiscard]] std::expected<void, Error> readTextureDictionaryChunk(io::Stream& chunk, const chunk::ChunkHeader& header,
                                                                    chunk::ChunkStacks& stacks);

/// The stream reader of chunk type 0x2A (Renderware Texture Dic): reads the chunk's texture dictionary like 0x0B's
/// reader and pushes it under type 0x0B. The original reads it through its render device, which also makes the
/// dictionary current and clears that again; Coney has no current dictionary, so the read is all that is left.
///
/// Research: docs/research/chunk-system.md#chunk-type-table
/// @orig 0x00190770 ChunkReader_RenderwareTextureDic (unknown)
[[nodiscard]] std::expected<void, Error>
readRwTextureDictionaryChunk(io::Stream& chunk, const chunk::ChunkHeader& header, chunk::ChunkStacks& stacks);

/// Registers the two texture dictionary stream readers above in `table`, for types 0x0B and 0x2A.
void addTextureDictionaryHandlers(chunk::ChunkHandlerTable& table);

/// The texture dictionaries of one WAD entry, whatever its shape: a sector atomics file (a headered RenderWare stream
/// whose first section is a texture dictionary, graphics::detectHeaderedRwStream()), a world stream
/// (graphics::isWorldStream()), or a chunk container, flat or grouped, loaded with
/// `table` (which should have the handlers of addTextureDictionaryHandlers()); then every 0x0B result is taken.
/// Fails with ErrorCode::NotFound when the entry holds no texture dictionary, and as the load does otherwise.
[[nodiscard]] std::expected<std::vector<TextureDictionary>, Error>
loadTextureDictionaries(const io::Wad& wad, const io::WadEntry& entry, const chunk::ChunkHandlerTable& table);

} // namespace coney::platform
