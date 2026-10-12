// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "animation/anim_math.h"
#include "debug/debug_lines.h"
#include "graphics/render_device.h"
#include "human/body.h"
#include "human/human.h"
#include "raycast/collision_mesh.h"

// The Debug draw page's collision shapes (docs/guides/debug-menu.md#debug-draw): a human's bodies as the engine moves
// it, its strike and struck shapes, its contacts, and the collision mesh coloured by how the walking sweep classes each
// triangle. Built from the human's and the body code's own accessors and rules (human/body.h), never recomputed, so
// what is drawn is what the physics uses.

namespace coney::debug {

/// Which of a human's shapes to draw.
struct ShapeLayers {
    /// The walking sphere (grounded) or the air push-out sphere (airborne), the upright capsule, the ground probe and,
    /// while climbing, the climb's face, normal and top.
    bool bodies = true;
    /// The strike shapes switched on (red) and the spine and head they are tested against (yellow).
    bool strikes = false;
    /// The contacts the last walking sweep slid along: a point on the walking sphere and its normal.
    bool contacts = false;
};

/// The shapes' colours, for the guide's legend and the tests.
inline constexpr graphics::Rgba kWalkingSphereColour{60, 200, 255, 255};
inline constexpr graphics::Rgba kAirSphereColour{255, 90, 255, 255};
inline constexpr graphics::Rgba kCapsuleColour{200, 200, 200, 255};
inline constexpr graphics::Rgba kGroundProbeColour{90, 255, 90, 255};
inline constexpr graphics::Rgba kClimbColour{255, 160, 40, 255};
inline constexpr graphics::Rgba kStrikeColour{255, 40, 40, 255};
inline constexpr graphics::Rgba kStruckColour{255, 230, 60, 255};
inline constexpr graphics::Rgba kContactColour{255, 120, 0, 255};
inline constexpr graphics::Rgba kHighlightColour{255, 255, 255, 255};

/// Adds `human`'s shapes of `layers` to `lines`, where the human is at its newest step moved by `offset`: the play
/// mode draws the models between steps, so it passes the distance from the newest step's feet to the drawn ones and the
/// shapes line up with the models at any frame rate.
void addHumanShapes(DebugLines& lines, const human::Human& human, const ShapeLayers& layers, anim::Vec3 offset = {});

/// Where the walking sphere touched what the contact with outward `normal` is: the point of the sphere at `centre` of
/// `radius` furthest along −normal.
[[nodiscard]] anim::Vec3 contactPoint(anim::Vec3 centre, float radius, anim::Vec3 normal);

/// The colour the classified wireframe draws a triangle of `kind` in: walls red, low or thin steep faces (which a walk
/// steps over) yellow, floors green, ceilings blue, disabled ones dark grey, degenerate ones magenta.
[[nodiscard]] graphics::Rgba wallClassColour(human::WallClass kind);

/// Adds the collision triangles of `mesh` whose first corner lies within `radius` of `centre` as a wireframe, at most
/// `maxTriangles`: each in one colour, or with `classify` coloured by human::wallClassOf() (wallClassColour()).
/// Returns how many triangles it added.
std::size_t addCollisionWireframe(DebugLines& lines, const raycast::CollisionMesh& mesh, anim::Vec3 centre,
                                  float radius, bool classify, std::size_t maxTriangles);

/// A marker over an entity at `position`: a tall upright and a diamond at `height` above it, so the selected one
/// stands out in a crowd.
void addHighlight(DebugLines& lines, anim::Vec3 position, float height);

} // namespace coney::debug
