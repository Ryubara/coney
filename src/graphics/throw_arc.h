// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"

// The throw-aim arc: the ribbon drawn along the path a held object will fly while the player aims a throw with L1
// (docs/research/graphics.md#throw-arc, docs/research/objects.md#throws).

namespace coney::graphics {

/// The arc's texture, a texture dictionary of one texture (`aimthrow`, 32 x 64) in the WAD file named by the decimal
/// CRC-32 of this name (`646320484`, `./ee_files/646320484`).
/// @orig 0x0018f268 ThrowArc_Create (unknown)
inline constexpr std::string_view kThrowArcTexture = "aimthrow_tex";
/// The ribbon's pairs of vertices, fixed: 36 vertices and 34 triangles whatever the arc's length, the pairs past a
/// cut arc's end collapsed on its last point.
inline constexpr std::size_t kThrowArcPairs = 18;
inline constexpr std::size_t kThrowArcTriangles = (kThrowArcPairs - 1) * 2;
/// Half the ribbon's width, metres, either side of the arc along the thrower's right axis (0.16 m across).
inline constexpr float kThrowArcHalfWidth = 0.08F;
/// The ribbon's vertex colour (RenderWare's RGBA bytes, alpha apart) while the arc meets nothing a throw targets, and
/// while it does (`hit`: the object `HuIsAimingAt` reads).
inline constexpr std::array<std::uint8_t, 3> kThrowArcColour{127, 127, 127};
inline constexpr std::array<std::uint8_t, 3> kThrowArcHitColour{170, 43, 43};
/// The alpha of every pair but the first: 127 plus up to this much by how far along the arc the pair is.
inline constexpr std::uint8_t kThrowArcAlphaBase = 127;
inline constexpr int kThrowArcAlphaRise = 16;
/// The texture coordinate v of pair 0; pair i has v = this minus i, so the texture repeats once per segment.
inline constexpr float kThrowArcFirstV = 15.0F;

/// One vertex of the ribbon, game axes (z up).
struct ThrowArcVertex {
    anim::Vec3 position;
    float u = 0.0F; ///< 0 on the left vertex of a pair, 1 on the right.
    float v = 0.0F; ///< kThrowArcFirstV at the hand, one less each pair.
    std::array<std::uint8_t, 4> rgba{};
};

/// The ribbon along the arc's `points` (the aim trace's kept points up to where it ended, the hand first, 18 at most;
/// `cut` is whether the trace ended at a contact, which is then the last point): always kThrowArcPairs pairs of
/// vertices, left then right, the point either side by kThrowArcHalfWidth along `right` (the thrower's right axis, a
/// unit vector, so the ribbon lies level across the arc in the plane of his facing). Pair 0 is on the hand, the pairs
/// after it on their points up to the contact, the ones past that on the last point before the contact, and the last
/// pair on the contact (or on the last point of an arc that met nothing). Coloured kThrowArcHitColour when `hit`, else
/// kThrowArcColour; alpha 0 on pair 0, then 127 + trunc(16 x min(pair / count, 1)) with `count` the points before the
/// contact (all of them when not `cut`). Empty for fewer than two points.
/// **Coney's reading**: an arc that met nothing has its last pair on its last point (the page has the last pair at
/// "point `count`", which an 18-point arc has no entry for).
/// @orig 0x0018fa68 ThrowArc_BuildStrip (unknown)
/// @orig 0x0018f268 ThrowArc_Create (unknown)
[[nodiscard]] std::vector<ThrowArcVertex> throwArcStrip(std::span<const anim::Vec3> points, anim::Vec3 right, bool cut,
                                                        bool hit);

/// The triangle list for a ribbon of kThrowArcPairs pairs: two triangles per pair of pairs, 6 indices a quad.
/// Collapsed pairs make degenerate triangles that draw nothing.
[[nodiscard]] std::vector<std::uint16_t> throwArcIndices();

/// The arc's points `alpha` of the way from the step before (`previous`) to the newest (`newest`), for a render between
/// steps: each point lerped when both arcs have as many points, else `newest` as it is. Exactly `newest` at alpha 1.
[[nodiscard]] std::vector<anim::Vec3> blendThrowArc(std::span<const anim::Vec3> previous,
                                                    std::span<const anim::Vec3> newest, float alpha);

} // namespace coney::graphics
