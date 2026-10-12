// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/mesh_sweep.h"

#include <algorithm>
#include <cmath>

namespace coney::physics {

namespace {

using anim::Vec3;

Vec3 operator+(Vec3 a, Vec3 b) { return anim::add(a, b); }
Vec3 operator-(Vec3 a, Vec3 b) { return anim::subtract(a, b); }
Vec3 operator*(Vec3 a, float s) { return anim::scale(a, s); }
float dot3(Vec3 a, Vec3 b) { return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z); }

// A wall's normal has |z| at most this (`0x3f266666`); exactly 0.65 is a wall.
constexpr float kWallNormalZ = 0.65F;
// A centre this close in front of a plane, or behind it, counts as behind (`0x358637bd`).
constexpr float kSideEpsilon = 1e-6F;
// A move must go into a face by more than this (`n · move` < −0.001, `0xba83126f`).
constexpr float kMoveInto = 0.001F;
// A longest edge at least this steep (|unit z|) skips the thin rule (`0x3f4ccccd`).
constexpr float kSteepEdge = 0.8F;
// Two contacts whose normals agree more than this are on one plane (`0x3f666666`).
constexpr float kSamePlane = 0.9F;
// The face hit's inside mask.
constexpr unsigned kFaceRegion = 7;

// The triangle's corners as anim vectors, in file order.
std::array<Vec3, 3> cornersOf(const raycast::CollisionMesh& mesh, std::uint32_t index) {
    const raycast::CollisionTriangle& triangle = mesh.triangles()[index];
    const auto vertices = mesh.vertices();
    std::array<Vec3, 3> corners{};
    for (std::size_t i = 0; i < 3; ++i) {
        const raycast::Vec3 v = vertices[triangle.vertices.at(i)];
        corners.at(i) = Vec3{v.x, v.y, v.z};
    }
    return corners;
}

// Walks the query's cells x outermost, then y, then z, each list in stored order, and calls `visit` once per
// triangle per query (the visited bits).
template <typename Visit>
void walkCells(const raycast::CollisionMesh& mesh, const CellRange& cells, MeshScratch& scratch, Visit visit) {
    for (std::uint32_t x = cells.min[0]; x <= cells.max[0]; ++x) {
        for (std::uint32_t y = cells.min[1]; y <= cells.max[1]; ++y) {
            for (std::uint32_t z = cells.min[2]; z <= cells.max[2]; ++z) {
                for (const std::uint16_t index : mesh.cellTriangles(x, y, z)) {
                    const auto bit = static_cast<std::uint8_t>(1U << (index & 7U));
                    std::uint8_t& byte = scratch.visited.at(index >> 3U);
                    if ((byte & bit) != 0) {
                        continue;
                    }
                    byte = static_cast<std::uint8_t>(byte | bit);
                    visit(static_cast<std::uint32_t>(index));
                }
            }
        }
    }
}

// The working triangle of `index` with its unit normal (edges and plane not yet set).
WorkingTriangle loadTriangle(const raycast::CollisionMesh& mesh, std::uint32_t index) {
    WorkingTriangle t;
    t.corners = cornersOf(mesh, index);
    computeNormal(t);
    return t;
}

// The capsule's low and thin rules on the (possibly turned) working triangle, whose edges are normalised with their
// lengths kept: true when the triangle is too low or too thin to be a wall.
bool lowOrThin(const WorkingTriangle& t, float minHeight) {
    // Low: the steepest edge (first wins a tie) rises less than the limit.
    std::size_t steepest = 0;
    std::size_t longest = 0;
    for (std::size_t k = 1; k < 3; ++k) {
        if (std::fabs(t.edges.at(k).z) > std::fabs(t.edges.at(steepest).z)) {
            steepest = k;
        }
        if (t.lengths.at(k) > t.lengths.at(longest)) {
            longest = k;
        }
    }
    if (std::fabs(t.corners.at((steepest + 1) % 3).z - t.corners.at(steepest).z) < minHeight) {
        return true;
    }
    // Thin: unless the longest edge is nearly vertical, the third corner lies within the limit of its line.
    const Vec3 unit = t.edges.at(longest);
    if (std::fabs(unit.z) >= kSteepEdge) {
        return false;
    }
    const Vec3 across = anim::cross(t.normal, unit);
    return std::fabs(dot3(across, t.corners.at((longest + 2) % 3)) - dot3(across, t.corners.at(longest))) < minHeight;
}

// A contact's common fields from a mesh hit: the normal, the key as the fraction, the triangle.
Contact contactFrom(const MeshContact& hit) {
    Contact contact;
    contact.normal = hit.working.normal;
    contact.fraction = hit.key;
    contact.triangle = hit.triangle;
    return contact;
}

// `move` made unit, or zero.
Vec3 unitOf(Vec3 move) {
    const float lengthSq = dot3(move, move);
    return lengthSq > 0.0F ? move * (1.0F / std::sqrt(lengthSq)) : Vec3{};
}

} // namespace

CellRange beginShapeQuery(const raycast::CollisionMesh& mesh, const Box& worldBox, MeshScratch& scratch) {
    scratch.visited.assign((mesh.triangles().size() + 7) / 8, 0);
    CellRange cells;
    cells.min = mesh.cellOf(raycast::Vec3{worldBox.min.x, worldBox.min.y, worldBox.min.z});
    cells.max = mesh.cellOf(raycast::Vec3{worldBox.max.x, worldBox.max.y, worldBox.max.z});
    return cells;
}

void sortByFraction(std::span<const MeshContact> contacts, std::vector<std::size_t>& order) {
    // One slot past the end, which the forward copy below also writes.
    order.assign(contacts.size() + 1, 0);
    for (std::size_t i = 0; i < contacts.size(); ++i) {
        std::size_t j = 0;
        while (j < i && !(contacts[order[j]].key > contacts[i].key)) {
            ++j;
        }
        if (j < i) {
            // The original's shift copies forwards, so every slot after j takes the old order[j].
            for (std::size_t k = j; k <= i; ++k) {
                order[k + 1] = order[k];
            }
        }
        order[j] = i;
    }
    order.resize(contacts.size());
}

int sweepCapsuleMesh(const raycast::CollisionMesh& mesh, const Box& worldBox, const CapsuleSweep& sweep,
                     MeshScratch& scratch, std::vector<Contact>& out) {
    const Sphere sphere{Vec3{sweep.feet.x, sweep.feet.y, sweep.feet.z + sweep.radius + sweep.footGap}, sweep.radius};
    scratch.found.clear();
    const CellRange cells = beginShapeQuery(mesh, worldBox, scratch);
    walkCells(mesh, cells, scratch, [&](std::uint32_t index) {
        // 1-3. Enabled, and a wall.
        if ((mesh.triangles()[index].flags & raycast::kTriangleEnabled) == 0) {
            return;
        }
        WorkingTriangle t = loadTriangle(mesh, index);
        if (std::fabs(t.normal.z) > kWallNormalZ) {
            return;
        }
        // 4. The side, from the centre at the start: behind a one-sided wall it is passed, a two-sided one turns.
        if (dot3(t.normal, sphere.centre) - dot3(t.normal, t.corners[0]) < kSideEpsilon) {
            if ((mesh.triangles()[index].flags & raycast::kTriangleTwoSided) == 0) {
                return;
            }
            flip(t);
        }
        // 5. Only faces the move goes into.
        if (dot3(t.normal, sweep.move) >= -kMoveInto) {
            return;
        }
        // 6-7. Not low or thin (on unit edges, lengths kept).
        computeEdges(t, true);
        if (lowOrThin(t, sweep.minWallHeight)) {
            return;
        }
        // 8. The kernel on the real edges.
        t.plane = dot3(t.corners[0], t.normal);
        computeEdges(t, false);
        const TriangleSweep hit = sweepSphereTriangle(sphere, t, sweep.move);
        // 9. A hit from 0 to 1. (The original's loop counter misses a step after a hit above 1 and reads one index past
        // the cell's list; Coney does not.)
        if (hit.hit && hit.fraction >= 0.0F && hit.fraction <= 1.0F) {
            scratch.found.push_back(
                MeshContact{.key = hit.fraction, .triangle = index, .region = hit.mask, .working = t});
        }
    });
    if (scratch.found.empty()) {
        return 0;
    }
    sortByFraction(scratch.found, scratch.order);
    // The corner slide: each hit beyond the edge its triangle names, while the owner is on the ground, adds the wall's
    // horizontal tangent toward that edge.
    Vec3 slide;
    bool sliding = false;
    for (const std::size_t i : scratch.order) {
        const MeshContact& hit = scratch.found[i];
        if (hit.region == kFaceRegion || sweep.ownerAirborne) {
            continue;
        }
        const unsigned edge = (mesh.triangles()[hit.triangle].flags >> kTriangleSlideEdgeShift) & 3U;
        if (edge == kTriangleNoSlideEdge || (hit.region & (1U << edge)) != 0) {
            continue;
        }
        const Vec3 n = hit.working.normal;
        const Vec3 tangent{n.y, -n.x, 0.0F};
        slide = hit.working.edges.at(edge).z > 0.0F ? slide - tangent : slide + tangent;
        sliding = true;
    }
    slide.z = 0.0F;
    // The PS2's rsqrt(0) leaves a zero sum zero; guard it here.
    slide = unitOf(slide) * std::max(sweep.radius, std::sqrt(dot3(sweep.move, sweep.move)));
    // The output, nearest first, every one of them.
    const Vec3 direction = unitOf(sweep.move);
    for (const std::size_t i : scratch.order) {
        const MeshContact& hit = scratch.found[i];
        Contact contact = contactFrom(hit);
        contact.moveDirection = direction;
        contact.point = sweep.feet + (sweep.move * hit.key);
        contact.slide = slide;
        contact.flags = sliding ? kContactAddSlide : 0;
        out.push_back(contact);
    }
    return static_cast<int>(scratch.order.size());
}

int sweepSphereMesh(const raycast::CollisionMesh& mesh, const Box& worldBox, const SphereSweep& sweep,
                    MeshScratch& scratch, std::vector<Contact>& out) {
    const Sphere& sphere = sweep.sphere;
    const bool keepAll = (sweep.options & kQueryKeepAll) != 0;
    const bool skipBit3 = (sweep.bodyFlags & kBodySkipBit3) != 0 || (sweep.options & kQuerySkipBit3) != 0;
    scratch.found.clear();
    const CellRange cells = beginShapeQuery(mesh, worldBox, scratch);
    walkCells(mesh, cells, scratch, [&](std::uint32_t index) {
        const std::uint16_t flags = mesh.triangles()[index].flags;
        // 1-2. Bit 3 for such a body or query; enabled.
        if (((flags & kTriangleBit3) != 0 && skipBit3) || (flags & raycast::kTriangleEnabled) == 0) {
            return;
        }
        // 3. The query's n.z range.
        WorkingTriangle t = loadTriangle(mesh, index);
        if (((sweep.options & kQueryMinNormalZ) != 0 && t.normal.z < sweep.minNormalZ) ||
            ((sweep.options & kQueryMaxNormalZ) != 0 && t.normal.z > sweep.maxNormalZ)) {
            return;
        }
        // 4. The side: a two-sided face turns; a one-sided one is skipped, or kept as behind with kQueryKeepAll.
        std::uint32_t marks = 0;
        const float side = dot3(t.normal, sphere.centre) - dot3(t.normal, t.corners[0]);
        if (side < kSideEpsilon) {
            if ((flags & raycast::kTriangleTwoSided) != 0) {
                flip(t);
            } else if (keepAll) {
                marks |= kContactBehind;
            } else {
                return;
            }
        }
        // 5. The move into the face, or with kQueryKeepAll along it or away.
        const float into = dot3(t.normal, sweep.move);
        if (into >= -kMoveInto) {
            if (!keepAll) {
                return;
            }
            marks |= into <= kMoveInto ? kContactAlong : kContactAway;
        }
        // 6. The kernel; a hit from 0 to 1 is kept.
        t.plane = dot3(t.corners[0], t.normal);
        computeEdges(t, false);
        const TriangleSweep hit = sweepSphereTriangle(sphere, t, sweep.move);
        if (hit.hit && hit.fraction >= 0.0F && hit.fraction <= 1.0F) {
            scratch.found.push_back(MeshContact{
                .key = hit.fraction, .triangle = index, .flags = marks, .startDistance = side, .working = t});
        }
    });
    if (scratch.found.empty()) {
        return 0;
    }
    // Sort, cut to the most asked for, then one per plane: an entry agreeing with an earlier one is replaced by the
    // last, which is not tested again in that slot.
    sortByFraction(scratch.found, scratch.order);
    std::size_t count = std::min(scratch.order.size(), static_cast<std::size_t>(std::max(sweep.max, 0)));
    if ((sweep.options & kQueryOnePerPlane) != 0) {
        for (std::size_t i = 0; i < count; ++i) {
            for (std::size_t j = i + 1; j < count; ++j) {
                const Vec3 ni = scratch.found[scratch.order[i]].working.normal;
                const Vec3 nj = scratch.found[scratch.order[j]].working.normal;
                if (std::fabs(dot3(ni, nj)) > kSamePlane) {
                    scratch.order[j] = scratch.order[count - 1];
                    --count;
                }
            }
        }
    }
    const Vec3 direction = unitOf(sweep.move);
    for (std::size_t i = 0; i < count; ++i) {
        const MeshContact& hit = scratch.found[scratch.order[i]];
        Contact contact = contactFrom(hit);
        contact.moveDirection = direction;
        contact.point = sphere.centre + (sweep.move * hit.key);
        contact.flags = hit.flags;
        contact.startDistance = hit.startDistance;
        out.push_back(contact);
    }
    return static_cast<int>(count);
}

int overlapSphereMesh(const raycast::CollisionMesh& mesh, const Box& worldBox, const Sphere& sphere,
                      std::uint32_t options, float minNormalZ, float maxNormalZ, int room, MeshScratch& scratch,
                      std::vector<Contact>& out) {
    scratch.found.clear();
    const CellRange cells = beginShapeQuery(mesh, worldBox, scratch);
    walkCells(mesh, cells, scratch, [&](std::uint32_t index) {
        if ((mesh.triangles()[index].flags & raycast::kTriangleEnabled) == 0) {
            return;
        }
        // No side test and no turn: a one-sided face is met from behind too, with the file's normal.
        WorkingTriangle t = loadTriangle(mesh, index);
        if (((options & kQueryMinNormalZ) != 0 && t.normal.z < minNormalZ) ||
            ((options & kQueryMaxNormalZ) != 0 && t.normal.z > maxNormalZ)) {
            return;
        }
        float depth = 0.0F;
        if (sphereTrianglePenetration(sphere, t.corners, depth)) {
            scratch.found.push_back(MeshContact{.key = depth, .triangle = index, .working = t});
        }
    });
    if (scratch.found.empty()) {
        return 0;
    }
    // The shallowest `room` of them, written deepest first.
    sortByFraction(scratch.found, scratch.order);
    const std::size_t count = std::min(scratch.order.size(), static_cast<std::size_t>(std::max(room, 0)));
    for (std::size_t i = count; i > 0; --i) {
        out.push_back(contactFrom(scratch.found[scratch.order[i - 1]]));
    }
    return 1;
}

int overlapNothing() { return 0; }

std::optional<Vec3> pushOutOfWalls(const raycast::CollisionMesh& mesh, const Sphere& sphere, MeshScratch& scratch) {
    const Vec3 reach{sphere.radius, sphere.radius, sphere.radius};
    const CellRange cells = beginShapeQuery(mesh, Box{sphere.centre - reach, sphere.centre + reach}, scratch);
    // The nearest wall by its closest point (sorted by the squared distance; the first of equal ones wins).
    std::optional<Vec3> normal;
    float nearest = 0.0F;
    walkCells(mesh, cells, scratch, [&](std::uint32_t index) {
        if ((mesh.triangles()[index].flags & raycast::kTriangleEnabled) == 0) {
            return;
        }
        WorkingTriangle t = loadTriangle(mesh, index);
        if (std::fabs(t.normal.z) > kWallNormalZ) {
            return;
        }
        if (dot3(t.normal, sphere.centre) - dot3(t.normal, t.corners[0]) < kSideEpsilon) {
            if ((mesh.triangles()[index].flags & raycast::kTriangleTwoSided) == 0) {
                return;
            }
            flip(t);
        }
        const float distanceSq = pointTriangleDistanceSq(sphere.centre, t.corners);
        if (distanceSq < sphere.radius * sphere.radius && (!normal || distanceSq < nearest)) {
            normal = t.normal;
            nearest = distanceSq;
        }
    });
    if (!normal) {
        return std::nullopt;
    }
    // Along the normal's horizontal part only, so a sloped wall pushes less than its depth.
    return Vec3{normal->x, normal->y, 0.0F} * (sphere.radius - std::sqrt(nearest));
}

} // namespace coney::physics
