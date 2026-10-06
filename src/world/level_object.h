// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/stream.h"
#include "raycast/collision_mesh.h"
#include "world/world_streams.h"

// The level file (`<level>.lev`) and the level object it builds: collision, occluders, path data, the sky, cloud and
// skyline models, the light glows and the subtitles. Chunk order and fields: docs/research/level-loading.md.

namespace coney::world {

/// Chunk types of the level file that its handlers name (docs/research/level-loading.md#the-level-file).
inline constexpr std::uint32_t kOccludersChunk = 0x53;
inline constexpr std::uint32_t kPreinstanceObjectChunk = 0x47; ///< A model clump; read as kLevelModelResult.
inline constexpr std::uint32_t kSectorBspDataChunk = 0x15;     ///< A RenderWare world; read as kLevelWorldResult.
inline constexpr std::uint32_t kPathDataChunk = 0x40;
inline constexpr std::uint32_t kLevelHeaderChunk = 0x17;
inline constexpr std::uint32_t kSubtitlesChunk = 0x51;
inline constexpr std::uint32_t kLevelModelResult = 0x41; ///< What the 0x47 reader pushes.
inline constexpr std::uint32_t kLevelWorldResult = 0x42; ///< What the 0x15 reader pushes.

/// Bytes of the level header chunk (0x17), on which the original builds the level object.
inline constexpr std::size_t kLevelHeaderBytes = 48;

/// An occluder: a vertical wall that hides what lies behind it, as four corners in RenderWare's axes (`P2` above `P0`,
/// `P3` above `P1`; the loader converts them from the game's axes).
struct Occluder {
    std::array<Vec3, 4> corners{};
};

/// Reads the occluders chunk (0x53): a `u32` count, padding to 16 bytes, then one 0x70-byte record per occluder whose
/// four points at +0x30 to +0x60 are turned from the game's axes into RenderWare's, (x, y, z) → (x, z, -y). The planes
/// and the active flag are computed per viewport and not read. Fails with ErrorCode::Truncated when the records run
/// past the chunk.
/// @orig 0x0017a560 Occluders_Load (unknown)
[[nodiscard]] std::expected<std::vector<Occluder>, Error> readOccluders(std::span<const std::byte> chunk);

/// The path data chunk's header (0x40). Coney keeps the chunk's bytes; world::PathMap decodes the records.
struct PathDataHeader {
    std::uint32_t paths = 0;       ///< +0x00: paths, 0x50 bytes each.
    std::uint32_t bCount = 0;      ///< +0x04: B records, 16 bytes each.
    std::uint16_t cCount = 0;      ///< +0x08: C records, 32 bytes each.
    std::uint16_t aCount = 0;      ///< +0x0a: A records, 16 bytes each.
    std::uint32_t dCount = 0;      ///< +0x0c: D records, 8 bytes each.
    std::size_t recordBytes = 0;   ///< What the counts add up to, header included: where the edge lists start.
    std::size_t edgeListBytes = 0; ///< The paths' slab edge lists, to the end of the furthest one.
};

/// Reads the path data chunk's 0x20-byte header, checks that the records it counts fit in the chunk, walks the paths'
/// slab edge lists that follow them, and checks that the chunk ends with a 4- to 19-byte tail padding it to a multiple
/// of 16, as every chunk on the disc does (docs/research/level-loading.md#path-data). Fails with ErrorCode::Truncated
/// when the records or a list run off the chunk, ErrorCode::Invalid when the chunk's size is otherwise wrong.
[[nodiscard]] std::expected<PathDataHeader, Error> inspectPathData(std::span<const std::byte> chunk);

/// The path data, on the object stack between its handler and the level header's.
class PathDataObject final : public chunk::LoadedObject {
  public:
    /// The header and the chunk's bytes.
    PathDataObject(PathDataHeader header, std::vector<std::byte> bytes) : m_header(header), m_bytes(std::move(bytes)) {}
    [[nodiscard]] std::string_view describe() const override { return "path data"; }
    /// The checked header.
    [[nodiscard]] const PathDataHeader& header() const { return m_header; }
    /// The chunk's bytes, for the level object to take.
    [[nodiscard]] std::vector<std::byte>& bytes() { return m_bytes; }

  private:
    PathDataHeader m_header;
    std::vector<std::byte> m_bytes;
};

/// The chunk 0x40 handler: pops the path data, checks its header and pushes it as a PathDataObject. The original
/// fixes the records up in place and keeps them in two globals; Coney keeps the bytes in the level object instead
/// (world::PathMap decodes them for the AI).
/// @orig 0x0024e720 PathData_OnLoaded (unknown)
[[nodiscard]] std::expected<void, Error> onPathDataLoaded(chunk::ChunkStacks& stacks, std::uint32_t type);

/// A model of the level with the texture dictionary it is linked to: the skyline, the sky box or the cloud box. Both
/// are the platform's objects (librw); the level object only owns them.
struct LevelModel {
    std::unique_ptr<chunk::LoadedObject> dictionary;
    std::unique_ptr<chunk::LoadedObject> model;
};

/// The level object: everything the level file holds, once its handlers have run.
///
/// Research: docs/research/level-loading.md#the-level-object
class LevelObject final : public chunk::LoadedObject {
  public:
    [[nodiscard]] std::string_view describe() const override { return "level object"; }

    std::unique_ptr<raycast::CollisionMesh> collision;   ///< +0x04.
    std::unique_ptr<chunk::LoadedObject> glowDictionary; ///< +0x08: the glow sprites' textures.
    std::unique_ptr<chunk::LoadedObject> levelWorld;     ///< +0x0c: the light glows (the platform's object).
    LevelModel skyline;                                  ///< +0x10, +0x14.
    LevelModel skyBox;                                   ///< +0x18, +0x1c.
    LevelModel cloudBox;                                 ///< +0x20, +0x24.
    std::vector<Occluder> occluders;                     ///< +0x28.
    PathDataHeader pathHeader;                           ///< The path data (globals in the original).
    std::vector<std::byte> pathData;
    std::vector<std::byte> subtitles; ///< Chunk 0x51, not decoded (a global in the original).
};

/// Links a model to its texture dictionary: the platform gives the model the game's pipelines and its first material
/// the dictionary's first texture. Fails when either object is not the platform's.
using LinkLevelModel = std::expected<void, Error> (*)(chunk::LoadedObject& model, chunk::LoadedObject& dictionary);

/// The chunk 0x17 handler: pops the 48-byte level header, takes the collision mesh and the path data from the object
/// stack, then pops the level world (0x42) and its dictionary (0x0B), the cloud box, sky box and skyline each as a
/// model (0x41) and dictionary (0x0B), linking each pair with `link`, and the occluders (0x53); pushes the
/// LevelObject. Fails when anything is missing or out of order, or as `link` and readOccluders() do.
/// @orig 0x0040ce30 LevelObject_OnLoaded (unknown)
[[nodiscard]] chunk::OnLoaded makeLevelHeaderHandler(LinkLevelModel link);

/// Registers the level file's platform-neutral handlers in `table`: the collision mesh (0x03), the path data (0x40)
/// and the level header (0x17, linking with `link`). The platform adds the texture dictionaries (0x2A), the models
/// (0x47) and the level world (0x15).
void addLevelFileHandlers(chunk::ChunkHandlerTable& table, LinkLevelModel link);

/// Loads a level file from `stream` with `table` (which needs the handlers of addLevelFileHandlers() and the
/// platform's) and returns the level object, with the subtitles chunk attached. Everything the file pushed must have
/// gone into the level: anything left on either stack fails with ErrorCode::Invalid, as do the load's own failures.
/// It stands for the original's `WorldLevel_Load` (`0x0040c688`), which the source map places in the tolua
/// middleware's range, so it carries no tag (docs/research/level-loading.md#open-questions).
[[nodiscard]] std::expected<std::unique_ptr<LevelObject>, Error> loadLevelFile(io::Stream& stream,
                                                                               const chunk::ChunkHandlerTable& table);

} // namespace coney::world
