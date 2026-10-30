// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_services.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "core/game_random.h"
#include "raycast/collision_mesh.h"

namespace coney::world_objects {

namespace {

// Applies one triangle's write to `mesh`; an index outside it is skipped.
// The original's triangle writes: an index past the mesh is skipped, bits are added, bit 0 set or cleared.
// @orig 0x003a4710 CollisionTri_SetFlag1 (unknown)
// @orig 0x003a46c8 CollisionTri_ClearFlag1 (unknown)
// @orig 0x003a4768 Collision_SetTriangleTwoSided (unknown)
void applyWrite(raycast::CollisionMesh& mesh, std::uint32_t index, const TriangleWrite& write) {
    const std::span<raycast::CollisionTriangle> all = mesh.mutableTriangles();
    if (index >= all.size()) {
        return;
    }
    raycast::CollisionTriangle& triangle = all[index];
    triangle.flags = static_cast<std::uint16_t>(triangle.flags | write.bits);
    if (write.material >= 0) {
        triangle.material = static_cast<std::uint8_t>(write.material);
    }
    if (write.enabled == 1) {
        triangle.flags = static_cast<std::uint16_t>(triangle.flags | raycast::kTriangleEnabled);
    } else if (write.enabled == 0) {
        triangle.flags = static_cast<std::uint16_t>(triangle.flags & ~raycast::kTriangleEnabled);
    }
}

// Switches the triangles of `mesh` wholly inside `write`'s box.
// @coney-only forwards a kept box switch to the mesh (CollisionMesh::setEnabledInBox)
void applyBox(raycast::CollisionMesh& mesh, const BoxWrite& write) {
    mesh.setEnabledInBox(raycast::Vec3{write.low[0], write.low[1], write.low[2]},
                         raycast::Vec3{write.high[0], write.high[1], write.high[2]}, write.on);
}

} // namespace

// Binds the level's mesh and applies the writes kept before it was bound.
// @coney-only Coney keeps the writes made before the play mode binds the mesh
void ObjectWorld::bindCollision(raycast::CollisionMesh* mesh) {
    collision = mesh;
    if (collision == nullptr) {
        return;
    }
    // The bits and materials first: they only add, so their order does not matter.
    for (const auto& [index, write] : triangleWrites) {
        applyWrite(*collision, index, TriangleWrite{.bits = write.bits, .material = write.material});
    }
    // Then the switches on and off, single triangles and boxes together, in the order they were made.
    struct Switch {
        std::uint32_t order;
        const BoxWrite* box;
        std::uint32_t index;
        int enabled;
    };
    std::vector<Switch> switches;
    for (const auto& [index, write] : triangleWrites) {
        if (write.enabled >= 0) {
            switches.push_back(
                Switch{.order = write.enabledOrder, .box = nullptr, .index = index, .enabled = write.enabled});
        }
    }
    for (const BoxWrite& box : boxWrites) {
        switches.push_back(Switch{.order = box.order, .box = &box, .index = 0, .enabled = -1});
    }
    std::ranges::sort(switches, {}, &Switch::order);
    for (const Switch& next : switches) {
        if (next.box != nullptr) {
            applyBox(*collision, *next.box);
        } else {
            applyWrite(*collision, next.index, TriangleWrite{.enabled = next.enabled});
        }
    }
}

// @coney-only forgets the kept writes
void ObjectWorld::clearWrites() {
    triangleWrites.clear();
    boxWrites.clear();
    nextWriteOrder = 1;
}

// Turns the triangles on or off, kept for a mesh bound later (docs/research/collision.md#enable).
// @orig 0x003a4710 CollisionTri_SetFlag1 (unknown)
// @orig 0x003a46c8 CollisionTri_ClearFlag1 (unknown)
void setTrianglesEnabled(ObjectWorld& world, std::span<const std::uint32_t> triangles, bool on) {
    for (const std::uint32_t index : triangles) {
        TriangleWrite& kept = world.triangleWrites[index];
        kept.enabled = on ? 1 : 0;
        kept.enabledOrder = world.nextWriteOrder++;
        if (world.collision != nullptr) {
            applyWrite(*world.collision, index, TriangleWrite{.enabled = on ? 1 : 0});
        }
    }
}

// @coney-only keeps and forwards a box switch; the mesh walk is CollisionMesh::setEnabledInBox
void setTrianglesEnabledInBox(ObjectWorld& world, std::array<float, 3> low, std::array<float, 3> high, bool on) {
    const BoxWrite write{.low = low, .high = high, .on = on, .order = world.nextWriteOrder++};
    world.boxWrites.push_back(write);
    if (world.collision != nullptr) {
        applyBox(*world.collision, write);
    }
}

void markTriangles(ObjectWorld& world, std::span<const std::uint32_t> triangles, std::uint16_t flags, int material) {
    for (const std::uint32_t index : triangles) {
        TriangleWrite& kept = world.triangleWrites[index];
        kept.bits = static_cast<std::uint16_t>(kept.bits | flags);
        if (material >= 0) {
            kept.material = material;
        }
        if (world.collision != nullptr) {
            applyWrite(*world.collision, index, TriangleWrite{.bits = flags, .material = material});
        }
    }
}

// @coney-only Coney's GameRandom for a 0 to n - 1 draw, on the effects stream the world objects draw from
int randomBelow(GameRandom* random, int n) {
    if (random == nullptr || n <= 0) {
        return 0;
    }
    return random->range(RandomStream::Effects, 0, n - 1);
}

// @coney-only quaternion helper
anim::Quat turnAboutVertical(anim::Quat rotation, float degrees) {
    return turnAboutOwnAxis(rotation, anim::Vec3{0.0F, 0.0F, 1.0F}, degrees);
}

anim::Quat turnAboutOwnAxis(anim::Quat rotation, anim::Vec3 axis, float degrees) {
    // The turn's half-angle quaternion about the axis, applied in the object's frame: rotation × turn.
    const float half = degrees * std::numbers::pi_v<float> / 360.0F;
    const float sine = std::sin(half);
    const anim::Quat turn{axis.x * sine, axis.y * sine, axis.z * sine, std::cos(half)};
    const anim::Quat& a = rotation;
    return anim::normalise(anim::Quat{a.w * turn.x + a.x * turn.w + a.y * turn.z - a.z * turn.y,
                                      a.w * turn.y - a.x * turn.z + a.y * turn.w + a.z * turn.x,
                                      a.w * turn.z + a.x * turn.y - a.y * turn.x + a.z * turn.w,
                                      a.w * turn.w - a.x * turn.x - a.y * turn.y - a.z * turn.z});
}

// @coney-only quaternion helper
anim::Vec3 rotate(anim::Quat rotation, anim::Vec3 v) {
    return anim::transformDirection(anim::matrixFromQuat(rotation), v);
}

} // namespace coney::world_objects
