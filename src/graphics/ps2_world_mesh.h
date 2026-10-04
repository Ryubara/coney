// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "core/error.h"

// The PS2 native geometry of the streamed world's atomics, decoded without RenderWare. Each mesh is a DMA chain that
// feeds the PS2's vector unit 1 the vertices in batches, packed as integers; the game's own pipelines (right to render
// 0x30082 and 0x30083) unpack them. librw's PS2 reader keeps these chains but cannot unpack this layout, so Coney does
// it here. Layout and evidence: docs/research/world.md#ps2-world-geometry.

namespace coney::graphics {

/// One vertex as the vector unit receives it, before any scaling.
struct Ps2PackedVertex {
    std::array<std::int16_t, 4> position{};  ///< x, y, z, then a word the game does not use for positions.
    std::array<std::int16_t, 4> texCoords{}; ///< u, v of the first set, then u, v of the second (0 with one set).
    std::array<std::uint8_t, 4> colour{};    ///< Prelighting colour, RGBA.
    std::array<std::int8_t, 4> normal{};     ///< x, y, z, then padding.

    friend bool operator==(const Ps2PackedVertex&, const Ps2PackedVertex&) = default;
};

/// Which vertex attributes a mesh's batches carry (the position always).
struct Ps2Attributes {
    std::uint32_t texCoordSets = 0; ///< 0, 1 (V2_16) or 2 (V4_16).
    bool colours = false;
    bool normals = false;
};

/// A decoded mesh: its vertices in strip (or list) order, the batches joined.
struct Ps2WorldMesh {
    std::vector<Ps2PackedVertex> vertices;
    Ps2Attributes attributes;
    std::uint32_t batches = 0;
};

/// Decodes one mesh's DMA chain, as stored in PS2 native geometry (offsets of reference tags counted in 16-byte units
/// from the chain's start). Walks the tags (cnt, ref, refs, ret, end, refe), runs the VIF commands they carry and
/// collects each batch: up to four interleaved UNPACKs (STCYCL 4, 1) to vector-unit slots 0 to 3 with position
/// (V4_16, always), texture coordinates (V4_16 for two sets, V2_16 for one), colour (V4_8 unsigned) and normal (V4_8),
/// ended by ITOP (the batch's real vertex count; each slot may unpack a few more, as padding) and a microprogram
/// start. In a strip, every batch after the first
/// repeats the last two vertices of the one before, so those are dropped when joining.
///
/// Fails with ErrorCode::Truncated when a tag, command or UNPACK runs past the chain, and ErrorCode::Invalid for a
/// layout other than the one above: an unknown DMA tag or VIF command, masked or offset unpacks, a slot with another
/// format, an ITOP larger than a slot, or strip batches that do not overlap.
[[nodiscard]] std::expected<Ps2WorldMesh, Error> decodePs2WorldMesh(std::span<const std::byte> chain,
                                                                    bool triangleStrip);

} // namespace coney::graphics
