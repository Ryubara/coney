// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/error.h"
#include "graphics/render_device.h"

namespace coney::graphics {

/// Bytes of a particle page's header, before its rectangles.
inline constexpr std::size_t kParticlePageHeaderSize = 0x14;
/// Bytes of one rectangle: four floats.
inline constexpr std::size_t kParticlePageRectSize = 16;

/// The rectangles of a sprite sheet, read from chunk 0x4C ("Particle Page"): texture coordinates into the single
/// texture of the dictionary before it. Every 2D image of the front end and the HUD is one of these rectangles.
///
/// Research: docs/research/gui.md#particle-page
struct ParticlePage {
    /// The rectangle of character 0 when the sheet is used as a font: character `c` is rectangle `firstGlyph + c`.
    /// -1 for a sheet that is not a font.
    std::int32_t firstGlyph = -1;
    /// The rectangles, in the file's order.
    std::vector<UvRect> rects;

    /// Rectangle `index`, which must be below rects.size() (checked by CONEY_ASSERT).
    /// @orig 0x00181e38 Page_Rect (unknown)
    [[nodiscard]] const UvRect& rect(std::size_t index) const;
};

/// Parses the data of a 0x4C chunk: the 20-byte header (a placeholder word, the count, firstGlyph and two words the
/// loader fills in), then `count` rectangles of four little-endian floats. Zero padding after the rectangles is
/// allowed and ignored, as are the placeholder and the two load-time words, which the original overwrites.
///
/// Fails with ErrorCode::Truncated when the data is shorter than its header or than its count says. The coordinates
/// are taken as they are: the original does not check them either.
[[nodiscard]] std::expected<ParticlePage, Error> parseParticlePage(std::span<const std::byte> data);

/// A sprite sheet ready to draw: its rectangles and the texture they refer to. A copy shares the texture.
///
/// The texture must be released before the renderer that made it stops (for librw textures, the RenderEngine).
struct SpriteSheet {
    ParticlePage page;
    std::shared_ptr<const Texture> texture; ///< Never null in a sheet a loader returns.
};

/// Chunk type 0x4D, "Particle Page Header": the table of every sprite sheet, in `warriors.glr`.
inline constexpr std::uint32_t kParticlePageHeader = 0x4D;

/// One record of the sprite sheet table.
struct SheetTableRecord {
    std::uint32_t size = 0;     ///< The sheet resource's size in bytes.
    std::uint32_t nameHash = 0; ///< CRC-32 of the sheet's name; its WAD file is named by this number in decimal.

    friend bool operator==(const SheetTableRecord&, const SheetTableRecord&) = default;
};

/// The table of sprite sheets, read from chunk 0x4D: game code refers to a sheet by its index here (`menu_system` is
/// 3, `big_font` 13) or by its name hash.
///
/// Research: docs/research/gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header
struct SpriteSheetTable {
    std::vector<SheetTableRecord> records;

    /// Record `index`, which must be below records.size() (checked by CONEY_ASSERT).
    /// @orig 0x001828c0 ResourceMgr_SheetRecord (unknown)
    [[nodiscard]] const SheetTableRecord& record(std::size_t index) const;

    /// The size the table gives for the sheet whose name hashes to `nameHash`, or nothing when it is not in the
    /// table. The original then falls back to the size of the WAD file named by the hash; a caller that needs that
    /// asks the WAD itself.
    /// @orig 0x00181e50 ResourceMgr_SheetSize (unknown)
    [[nodiscard]] std::optional<std::uint32_t> sizeOf(std::uint32_t nameHash) const;
};

/// Parses the data of a 0x4D chunk: a little-endian count, then `count` records of {size, name hash}. Fails with
/// ErrorCode::Truncated when the data is shorter than its count says. Bytes after the records are ignored.
[[nodiscard]] std::expected<SpriteSheetTable, Error> parseSpriteSheetTable(std::span<const std::byte> data);

/// The sprite sheet table on the chunk stack, pushed back under 0x4D by its handler.
class SpriteSheetTableObject final : public chunk::LoadedObject {
  public:
    explicit SpriteSheetTableObject(SpriteSheetTable table) : m_table(std::move(table)) {}

    [[nodiscard]] std::string_view describe() const override { return "sprite sheet table"; }

    /// The table.
    [[nodiscard]] const SpriteSheetTable& table() const { return m_table; }

  private:
    SpriteSheetTable m_table;
};

/// The onLoaded handler of chunk type 0x4D: pops the chunk, parses it (parseSpriteSheetTable()) and pushes a
/// SpriteSheetTableObject back under 0x4D. The original keeps the table in its resource manager; Coney has none yet,
/// so whoever loads `warriors.glr` takes the table off the stack. Fails as ChunkStacks::popChunk() and
/// parseSpriteSheetTable() do.
///
/// Research: docs/research/gui.md#sprite-sheet-table-chunk-0x4d-particle-page-header
/// @orig 0x00182820 ChunkLoaded_ParticlePageHeader (unknown)
[[nodiscard]] std::expected<void, Error> onParticlePageHeaderLoaded(chunk::ChunkStacks& stacks, std::uint32_t type);

} // namespace coney::graphics
