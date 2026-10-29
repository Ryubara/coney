// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "raycast/collision_builder.h"
#include "raycast/collision_mesh.h"

// @coney-only the debug tools' world edits (docs/guides/running-viewers.md#world-edits): taking a level's streamed
// world apart into buildings, an experiment for Coney Studio (docs/research/world-split.md); the original has none.
//
// Everything here is plain data in the game's axes (z up, metres), so it is tested without a window or a disc: the
// platform side (platform/world_edit.h) turns a sector atomic's geometry into these spans and back. One header, as
// the tools and their tests use them together; the edit file's parser is in world_edits.cpp, the geometry in
// world_split.cpp.

namespace coney::debug {

using raycast::Vec3;

/// A box in the game's axes, `min` to `max` on each axis.
struct GameBox {
    Vec3 min;
    Vec3 max;

    /// Whether `point` lies inside the box grown by `margin` on every side.
    [[nodiscard]] bool contains(Vec3 point, float margin = 0.0F) const;
};

/// A triangle of a mesh, by its three vertex indices.
using MeshTriangle = std::array<std::uint32_t, 3>;

/// A mesh's connected pieces: triangles that share a vertex, after welding, belong to one piece.
struct MeshPieces {
    std::vector<std::uint32_t> pieceOf; ///< Per triangle, its piece, numbered 0 .. count - 1 by first triangle.
    std::uint32_t count = 0;            ///< How many pieces.
};

/// The connected pieces of a mesh. Vertices closer than `weld` on every axis count as one (a strip mesh repeats a
/// corner with other texture coordinates, and two sectors' atomics meet at copies of the same point), so the pieces
/// are what a person would call one connected surface, whatever the vertex list says.
[[nodiscard]] MeshPieces connectedPieces(std::span<const Vec3> positions, std::span<const MeshTriangle> triangles,
                                         float weld);

/// How a box picks a mesh's triangles.
enum class CutRule : std::uint8_t {
    Centroid,    ///< A triangle whose centroid lies in the box.
    AllVertices, ///< A triangle whose three corners lie in the box (CollisionMesh::setEnabledInBox's rule).
    Pieces,      ///< Every triangle of each connected piece (connectedPieces()) that lies wholly in the box.
};

/// One byte per triangle of the mesh, 1 for each triangle `box` takes by `rule`. `weld` is connectedPieces()'s, for
/// CutRule::Pieces; every rule allows the same distance as a margin round the box.
[[nodiscard]] std::vector<std::uint8_t> trianglesInBox(std::span<const Vec3> positions,
                                                       std::span<const MeshTriangle> triangles, const GameBox& box,
                                                       CutRule rule, float weld = 0.01F);

/// The triangles of a mesh that face up (a unit normal's z at least `minUpZ`) among those `take` marks, as collision
/// triangles of `material`, wound so that their normals face up (the collision mesh's one-sided tests hit that side).
/// For a roof whose visible faces have no collision (docs/research/world-split.md#collision).
[[nodiscard]] std::vector<raycast::BuildTriangle> upFacingCollision(std::span<const Vec3> positions,
                                                                    std::span<const MeshTriangle> triangles,
                                                                    std::span<const std::uint8_t> take, float minUpZ,
                                                                    std::uint8_t material);

/// The collision triangles of `mesh` whose three corners lie in `box` and that are enabled, moved by `offset`, as
/// triangles to add back to it (CollisionMesh::addExtraTriangles()), each with its flags, material and area byte.
[[nodiscard]] std::vector<raycast::BuildTriangle> collisionInBox(const raycast::CollisionMesh& mesh, const GameBox& box,
                                                                 Vec3 offset);

/// Where one box goes in the sector spread view: point p goes to `centre + (p - centre) * scale + translation`.
struct SpreadPlacement {
    Vec3 centre;        ///< The box's centre, the point it is scaled about.
    float scale = 1.0F; ///< Its scale.
    Vec3 translation;   ///< Then moved by this.
};

/// Where each of a list of boxes goes when they are drawn apart on a grid (the sector spread view): box `i` in
/// column `i % columns`, row `i / columns`; every cell as wide and deep as the largest box's footprint times `scale`,
/// plus `gap`; the grid's first cell centred at `origin`, the columns along +x and the rows along -y. Each scaled box
/// stands on origin.z (its floor laid there) at its cell's centre. With `fitCell`, every box is scaled on its own to
/// fit a footprint of `fitCell` metres instead (each sector seen whatever its size), and the cells are `fitCell` plus
/// `gap`; an empty box keeps scale 1.
[[nodiscard]] std::vector<SpreadPlacement> spreadLayout(std::span<const GameBox> boxes, std::uint32_t columns,
                                                        float gap, float scale, Vec3 origin,
                                                        std::optional<float> fitCell = std::nullopt);

/// One key of a moved piece: its offset from where the disc has it, reached at fixed step `step`.
struct OffsetKey {
    std::uint64_t step = 0;
    Vec3 offset;
};

/// The offset at step `step` of a piece moved through `keys` (in step order): none before the first key, then
/// straight from each key to the next, and the last key's after it.
[[nodiscard]] Vec3 offsetAt(std::span<const OffsetKey> keys, std::uint64_t step);

/// The distance from `p` to the nearest point of the triangle `a`, `b`, `c` (its face, an edge or a corner).
[[nodiscard]] float pointTriangleDistance(Vec3 p, Vec3 a, Vec3 b, Vec3 c);

/// A mesh's triangles sorted into a uniform grid of cubes, for the research's question of how far one mesh lies from
/// another (docs/research/world-split.md#collision): the nearest triangle to a point, within a reach.
class TriangleGrid {
  public:
    /// Sorts `triangles` (by their bounds) into cubes of `cell` metres. The spans must outlive the grid.
    TriangleGrid(std::span<const Vec3> positions, std::span<const MeshTriangle> triangles, float cell);

    /// The distance from `p` to the nearest triangle within `reach` (no farther than `cell`), or nothing.
    [[nodiscard]] std::optional<float> nearest(Vec3 p, float reach) const;

  private:
    // The key of the cube at cube coordinates (x, y, z): the three packed into one integer.
    [[nodiscard]] std::uint64_t keyOf(std::int64_t x, std::int64_t y, std::int64_t z) const;
    // The cube coordinate along one axis of the value `v`.
    [[nodiscard]] std::int64_t cellOf(float v) const;

    std::span<const Vec3> m_positions;
    std::span<const MeshTriangle> m_triangles;
    float m_cell = 1.0F;
    std::vector<std::pair<std::uint64_t, std::uint32_t>> m_entries; // {cube, triangle}, sorted by cube
};

/// A world edit file's statements (docs/guides/running-viewers.md#world-edits), in the game's axes.
struct WorldEdits {
    /// `hide s` / `hide d`: the `s` or the `d` world not drawn.
    std::array<bool, 2> hidden{};
    /// `spread`: every sector drawn apart on a grid; in play, the sectors play has loaded, without the background.
    struct Spread {
        std::uint32_t columns = 10;
        float gap = 2.0F;
        float scale = 0.25F;
        std::optional<float> cell; ///< `cell=M`: each sector scaled to fit an M-metre cell.
        Vec3 origin;
    };
    std::optional<Spread> spread;
    /// `camera`: where the world viewer's camera starts, and its heading and pitch in degrees (heading 0 looks along
    /// +y, growing towards +x; pitch positive up).
    struct Camera {
        Vec3 at;
        float heading = 0.0F;
        float pitch = 0.0F;
    };
    std::optional<Camera> camera;
    /// `cut`: a piece taken out of every sector atomic of both worlds, with its collision.
    struct Cut {
        std::string name;
        GameBox box;
        CutRule rule = CutRule::Centroid;
        bool collision = true; ///< Its collision triangles (wholly in the box) move with it.
        std::vector<OffsetKey> keys;
    };
    std::vector<Cut> cuts;
    /// `roof`: collision added from the visible up-facing triangles in a box.
    struct Roof {
        GameBox box;
        std::uint8_t material = raycast::kMaterialConcrete;
    };
    std::vector<Roof> roofs;
    /// `debugdraw collision`: the Debug draw page's collision wireframe on from the first step, reaching this far.
    std::optional<float> collisionWire;
};

/// Reads a world edit file: one statement a line, `#` to the end of a line a comment, words separated by spaces,
/// `key=value` arguments, numbers in metres and degrees, triples as `x,y,z`:
///
///     hide s|d
///     spread [columns=N] [gap=M] [scale=S | cell=M] [at=X,Y,Z]
///     camera at=X,Y,Z [heading=DEG] [pitch=DEG]
///     cut NAME box=X0,Y0,Z0,X1,Y1,Z1 [by=centroid|corners|pieces] [collision=yes|no]
///     move NAME step=N offset=DX,DY,DZ
///     roof box=X0,Y0,Z0,X1,Y1,Z1 [material=ID]
///     debugdraw collision [radius=M]
///
/// A `move` names a `cut` above it and adds a key (steps in order). Fails with ErrorCode::InvalidArgument naming the
/// line and what is wrong.
[[nodiscard]] std::expected<WorldEdits, Error> parseWorldEdits(std::string_view text);

} // namespace coney::debug
