// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "animation/anim_math.h"
#include "sandbox/sandbox_layout.h"
#include "sandbox/sandbox_mesh.h"

namespace coney::sandbox {

/// What a bake did: counts for the summary line and the tests.
struct BakeStats {
    std::size_t occlusionRays = 0; ///< Ambient-occlusion rays cast.
    std::size_t shadowRays = 0;    ///< Rays cast towards the sun.
    std::size_t shadowed = 0;      ///< Vertices facing the sun that something hides it from.
};

/// How many rays each vertex sends for its ambient occlusion.
inline constexpr std::size_t kOcclusionRays = 16;

/// The light `layout` describes, baked into every vertex colour of `mesh` (game axes, z up). Coney's own lighting
/// (the game's LightManager is not researched): a sky/ground ambient blended by how far the normal points up, plus
/// the sun's diffuse light where the vertex faces it, then multiplied by the primitive's tint and clamped to 1.
///
/// Two terms come from rays against the solid primitives' collision triangles (the untessellated faces the collision
/// mesh is made of, so the light sees what the characters and cameras collide with), each ray testing only the
/// primitives a grid of their bounds puts near it, which keeps a bake fast even in a debug build:
/// - sun shadows: a vertex whose ray towards the sun hits something gets no sunlight;
/// - ambient occlusion: kOcclusionRays rays over the hemisphere round the normal, `occlusionRadius` long; the nearer
///   the hits, the darker the ambient (by up to `occlusionStrength`). Only vertices within the radius of another
///   primitive's bounds (or of their own, for stairs, which have inside corners) cast them.
///
/// A vertex inside another solid primitive (the floor under a box) is fully dark, so no light bleeds along a foot.
/// Deterministic: the ray directions are fixed, so the same layout always bakes the same colours.
BakeStats bakeLighting(SandboxMesh& mesh, const SandboxLayout& layout);

/// `count` unit directions over the hemisphere round +z, spread evenly and denser towards the pole (cosine-weighted,
/// a Fibonacci spiral), the same every call.
[[nodiscard]] std::vector<anim::Vec3> hemisphereDirections(std::size_t count);

} // namespace coney::sandbox
