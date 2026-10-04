// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "core/error.h"

// The files of a streamed world, read without RenderWare: the world stream (`<world>_sec.wld`), whose RenderWare world
// librw has no reader for, and the part files (`<world>_ms<i>.sec`), whose atomics are checked here before librw reads
// them. Formats: docs/research/world.md.

namespace coney::world {

/// A point or a direction in RenderWare's axes.
struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

/// A RenderWare frame's matrix: the rows `right`, `up` and `at`, then `position`; a point p goes to
/// p.x·right + p.y·up + p.z·at + position.
struct FrameMatrix {
    Vec3 right{1.0F, 0.0F, 0.0F};
    Vec3 up{0.0F, 1.0F, 0.0F};
    Vec3 at{0.0F, 0.0F, 1.0F};
    Vec3 position;
};

/// An axis-aligned box: `min` is the smallest corner, `max` the largest.
struct Box {
    Vec3 min;
    Vec3 max;

    /// Whether `point` is inside the box grown by `margin` on every side.
    [[nodiscard]] bool contains(Vec3 point, float margin) const;
};

/// The 20 streamed bytes of the game's world sector plugin (id 0x3F1): where a sector's geometry is and where it goes.
/// Research: docs/research/world.md#sector-plugin
struct SectorPluginData {
    std::int32_t streamedIndex = -1; ///< The sector's slot in the world's sector table; -1 when it has no atomic.
    std::uint32_t part = 0;          ///< The part file holding its atomic (`<world>_ms<part>.sec`); 0 without one.
    Vec3 origin;                     ///< Where the atomic's frame is placed.
};

/// The RenderWare plugin id of the world sector data above.
inline constexpr std::uint32_t kSectorPluginId = 0x3F1;

/// Reads the sector plugin's stream data: exactly 20 bytes, else ErrorCode::Invalid.
/// @orig 0x00198e20 SectorPlugin_StreamRead (unknown)
[[nodiscard]] std::expected<SectorPluginData, Error> readSectorPluginData(std::span<const std::byte> data);

/// The 12 streamed bytes of the game's atomic plugin (id 0x3F0). The game's own PS2 pipelines upload the two floats to
/// the vector unit; Coney's PS2 world mesh decoder uses them as the scales of the packed vertex data
/// (graphics/ps2_world_mesh.h).
/// Research: docs/research/world.md#atomic-plugin
struct AtomicPluginData {
    float positionScale = 1.0F; ///< +0x00: multiplies the packed 16-bit positions (disc check, world.md).
    float secondScale = 1.0F;   ///< +0x04: uploaded next to it; what it scales is not settled.
    std::uint32_t word = 0;     ///< +0x08: always 0 in the streamed worlds; meaning unknown.
};

/// What an atomic carries when its stream has no plugin data (the original's constructor, 0x00192618).
inline constexpr AtomicPluginData kDefaultAtomicPluginData{};

/// The RenderWare plugin id of the atomic data above.
inline constexpr std::uint32_t kAtomicPluginId = 0x3F0;

/// Bytes the atomic plugin streams.
inline constexpr std::size_t kAtomicPluginStreamBytes = 12;

/// Reads the atomic plugin's stream data: exactly 12 bytes, else ErrorCode::Invalid.
/// @orig 0x00192688 AtomicPlugin_StreamRead (unknown)
[[nodiscard]] std::expected<AtomicPluginData, Error> readAtomicPluginData(std::span<const std::byte> data);

/// One atomic sector (a leaf of the world's BSP): its box and, when present, the game's plugin data.
struct WorldSector {
    Box box;
    std::optional<SectorPluginData> plugin;
};

/// One child of a BSP plane: another plane, or a leaf (an atomic sector).
struct BspChild {
    bool leaf = true;        ///< An atomic sector rather than a plane.
    std::uint32_t index = 0; ///< Into WorldStream::sectors for a leaf, WorldStream::planes otherwise.
};

/// An inner node of a world's BSP (a RenderWare plane sector): the axis it splits, where, and its two children. The
/// left child holds the side below the value (inferred from RenderWare; Coney's disc test checks it).
struct BspPlane {
    std::uint32_t axis = 0; ///< 0 x, 1 y, 2 z.
    float value = 0.0F;     ///< Where the plane cuts the axis.
    BspChild left;
    BspChild right;
};

/// What a world stream holds, from its structure. Research: docs/research/world.md#world-stream
struct WorldStream {
    std::uint32_t partCount = 0;      ///< The leading `u32`: how many part files the world has.
    std::size_t dictionaryOffset = 0; ///< Where its texture dictionary section starts.
    std::size_t dictionaryBytes = 0;  ///< The dictionary's size, header included.
    std::uint32_t worldFormat = 0;    ///< The world's geometry format flags.
    Box worldBox;                     ///< The world's own bounding box.
    std::uint32_t planeSectors = 0;   ///< Inner nodes of the BSP read.
    std::vector<WorldSector> sectors; ///< Atomic sectors, in stream order (the BSP's leaves, left first).
    std::vector<BspPlane> planes;     ///< The BSP's inner nodes, in stream order (parents before children).
    BspChild root;                    ///< The BSP's root: a plane, or the only sector.
};

/// Reads the layout of a world stream: the part count, where the texture dictionary is, and every atomic sector of
/// the RenderWare world (section 0x0B) with its box and sector plugin data. The world's own header must agree with
/// what was found (plane and atomic sector counts). Fails with ErrorCode::Truncated when a section runs past its parent
/// and ErrorCode::Invalid for anything out of place: a missing section, a sector holding triangles, a BSP deeper than
/// its sector count allows or a sector plugin record that is not 20 bytes.
[[nodiscard]] std::expected<WorldStream, Error> inspectWorldStream(std::span<const std::byte> stream);

/// One `{u32 index, atomic}` record of a part file.
struct PartAtomic {
    std::uint32_t streamedIndex = 0; ///< The sector the atomic belongs to (SectorPluginData::streamedIndex).
    std::size_t offset = 0;          ///< Where its atomic section (0x14) starts in the file.
    std::size_t bytes = 0;           ///< The atomic section's size, header included.
};

/// What a part file holds, from its structure. Research: docs/research/world.md#part-file
struct PartFile {
    std::uint32_t nameHash = 0;       ///< The header's fourth word: CRC-32 of the file's own name; unused.
    std::size_t dictionaryOffset = 0; ///< Where its texture dictionary section starts.
    std::size_t dictionaryBytes = 0;  ///< The dictionary's size, header included.
    std::vector<PartAtomic> atomics;  ///< In file order.
};

/// Reads the layout of a part file: the 16-byte header `{1, 0, 0, nameHash}`, the texture dictionary, the count and
/// the `{index, atomic}` records. Fails with ErrorCode::Truncated when the file ends early and ErrorCode::Invalid for a
/// wrong header or a section out of place. Bytes after the last record are an error too: the format has none.
[[nodiscard]] std::expected<PartFile, Error> inspectPartFile(std::span<const std::byte> file);

/// One mesh of a geometry: a strip or list of `indexCount` vertices with one material.
struct MeshInfo {
    std::uint32_t indexCount = 0;
    std::uint32_t materialIndex = 0;
    std::span<const std::byte> nativeData; ///< Its PS2 DMA chain from the native data section (0x510).
};

/// What one standalone atomic section (0x14) of a part file holds, as far as librw's readers depend on it.
struct AtomicSection {
    std::uint32_t atomicFlags = 0;          ///< The atomic's RenderWare flags (collision test, render).
    std::uint32_t geometryFlags = 0;        ///< The geometry's format flags; always PS2 native here.
    std::uint32_t triangleCount = 0;        ///< Triangles the geometry header says it has.
    std::uint32_t vertexCount = 0;          ///< Vertices the geometry header says it has.
    std::uint32_t materialCount = 0;        ///< Entries of its material list.
    bool triangleStrips = false;            ///< The meshes are triangle strips rather than lists.
    std::vector<MeshInfo> meshes;           ///< From the mesh plugin (0x50E) and the native data (0x510).
    std::optional<AtomicPluginData> plugin; ///< The game's atomic plugin data (0x3F0), when present.
    std::optional<std::uint32_t> pipeline;  ///< Right-to-render plugin data: which pipeline draws it.
    std::uint32_t pipelinePlugin = 0;       ///< Right-to-render plugin id (the plugin owning that pipeline).
    std::span<const std::byte> geometry;    ///< The geometry section (0x0F), header included.
    std::span<const std::byte> extension;   ///< The atomic's extension section (0x03), header included.
};

/// The PS2 platform id at the start of native geometry data.
inline constexpr std::uint32_t kPs2Platform = 4;

/// Checks one standalone atomic section, header included, as RenderWare 3.7 writes it for an atomic outside a clump
/// (struct, the geometry itself, extension), and every count and size librw's geometry, material and texture readers
/// trust: material list indices, texture name lengths, the mesh plugin's material indices and totals, and the native
/// data's per-mesh sizes. Only PS2 native geometry is accepted. Fails with ErrorCode::Truncated or ErrorCode::Invalid
/// naming what is wrong.
[[nodiscard]] std::expected<AtomicSection, Error> inspectAtomicSection(std::span<const std::byte> section);

/// The one model of a level file's preinstanced clump (chunk 0x47), rearranged for the part atomics' reader.
struct ClumpModel {
    std::vector<std::byte> atomicSection; ///< A standalone atomic section: its struct, its geometry, its extension.
    FrameMatrix frame;                    ///< The atomic's frame in the world: its frame times every parent's.
};

/// Reads a RenderWare clump (section 0x10) of one atomic and one geometry, as the level file's skyline, sky box and
/// cloud box are, and rearranges it as a standalone atomic section (inspectAtomicSection()'s input), with the
/// atomic's frame composed with its parents' (the disc's root frames place and turn the models). Fails with
/// ErrorCode::Truncated or ErrorCode::Invalid for a clump of another shape: not exactly one atomic and one geometry,
/// a frame index out of range or frames that loop.
///
/// Research: docs/research/level-loading.md#the-level-object
[[nodiscard]] std::expected<ClumpModel, Error> extractClumpModel(std::span<const std::byte> clump);

/// The level file's world (chunk 0x15), rearranged for the part atomics' reader.
struct LevelWorldModel {
    std::vector<std::byte> atomicSection; ///< A standalone atomic section holding the world's one sector.
    std::uint32_t worldFormat = 0;        ///< The world's geometry format flags.
    std::uint32_t triangleCount = 0;      ///< Triangles of its sector.
    std::uint32_t vertexCount = 0;        ///< Vertices of its sector.
    Box box;                              ///< The sector's box.
};

/// Reads a RenderWare world (section 0x0B) with no planes and one atomic sector, as the level file's glow world is,
/// and rearranges the sector as a standalone atomic section: a geometry with the world's format, the sector's counts,
/// the world's material list and the sector's mesh and native data plugins, in an atomic with no plugins (so no
/// pipeline: RenderWare's default). Fails with ErrorCode::Truncated or ErrorCode::Invalid for a world of another
/// shape, naming what is out of place.
///
/// Research: docs/research/level-loading.md#the-level-object
[[nodiscard]] std::expected<LevelWorldModel, Error> extractLevelWorld(std::span<const std::byte> world);

} // namespace coney::world
