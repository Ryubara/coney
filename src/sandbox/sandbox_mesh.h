// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "animation/anim_math.h"
#include "raycast/collision_builder.h"
#include "sandbox/sandbox_layout.h"

namespace coney::sandbox {

/// One vertex of the sandbox's drawn geometry, in the game's axes.
struct MeshVertex {
    anim::Vec3 position;
    anim::Vec3 normal; ///< Unit length, pointing out of the primitive.
    float u = 0.0F;    ///< Texture coordinates: one unit is one texture tile.
    float v = 0.0F;
    Colour colour{1.0F, 1.0F, 1.0F}; ///< The baked light (bakeLighting()); white until then.
    std::uint32_t primitive = 0;     ///< Index into SandboxLayout::primitives.
};

/// One triangle; its front is the side `(b - a) × (c - a)` points to, which is the outside.
struct MeshTriangle {
    std::array<std::uint32_t, 3> vertices{};
    std::uint32_t primitive = 0; ///< Index into SandboxLayout::primitives; its texture is the triangle's.
};

/// The sandbox's geometry: what is drawn, or (untessellated) what the collision mesh is made of.
struct SandboxMesh {
    std::vector<MeshVertex> vertices;
    std::vector<MeshTriangle> triangles;
};

/// No subdivision: each face as few triangles as its shape needs. The collision mesh is built this way.
inline constexpr float kNoTessellation = std::numeric_limits<float>::infinity();

/// Builds the geometry of `layout`'s primitives, in layout order. Flat faces are split into a grid of pieces no longer
/// than `tessellation` metres on a side, so baked lighting has vertices to vary across them; curved surfaces have the
/// primitive's segments round their axis and pieces no taller than `tessellation` along it. A face lying on or below
/// the ground plane (z = 0) facing down is left out: nothing can see it.
///
/// Texture coordinates keep one tile per kTileMetres (times the primitive's uv scale) on every face: a flat face is
/// projected onto its own plane, `u` along the face's horizontal direction and `v` up the face (on a wall `v` is the
/// height above z = 0, so the grid measures heights from the ground); a face with no horizontal direction (a floor or a
/// roof) uses the primitive's turned x axis for `u`. A cylinder's side unrolls round its axis; a sphere's faces are
/// projected along the axis nearest their normal (triplanar).
[[nodiscard]] SandboxMesh buildSandboxMesh(const SandboxLayout& layout, float tessellation);

/// The collision triangles of `layout`: the untessellated geometry of every solid primitive, each triangle tagged with
/// its primitive's flags, material and area byte.
[[nodiscard]] std::vector<raycast::BuildTriangle> sandboxCollisionTriangles(const SandboxLayout& layout);

/// The local-to-world turn of a primitive: its yaw about +z, counter-clockwise seen from above.
[[nodiscard]] anim::Vec3 turnByYaw(anim::Vec3 local, float yawDegrees);

} // namespace coney::sandbox
