// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/body.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace coney::human {

namespace {

// A longest edge at least this steep (|unit z|) counts as vertical for the thin-triangle test.
constexpr float kVerticalEdge = 0.8F;
// The move must go into a wall by more than this (n · move below its negative) for the wall to count.
constexpr float kMoveInto = 0.001F;

raycast::Vec3 toMesh(anim::Vec3 v) { return raycast::Vec3{v.x, v.y, v.z}; }
anim::Vec3 fromMesh(raycast::Vec3 v) { return anim::Vec3{v.x, v.y, v.z}; }

// The point of the triangle `a b c` nearest `p`: inside the face, on an edge or at a corner (by the triangle's
// Voronoi regions).
anim::Vec3 closestOnTriangle(anim::Vec3 p, anim::Vec3 a, anim::Vec3 b, anim::Vec3 c) {
    const anim::Vec3 ab = anim::subtract(b, a);
    const anim::Vec3 ac = anim::subtract(c, a);
    const anim::Vec3 ap = anim::subtract(p, a);
    const float d1 = anim::dot(ab, ap);
    const float d2 = anim::dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F) {
        return a;
    }
    const anim::Vec3 bp = anim::subtract(p, b);
    const float d3 = anim::dot(ab, bp);
    const float d4 = anim::dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F) {
        return anim::add(a, anim::scale(ab, d1 / (d1 - d3)));
    }
    const anim::Vec3 cp = anim::subtract(p, c);
    const float d5 = anim::dot(ab, cp);
    const float d6 = anim::dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F) {
        return anim::add(a, anim::scale(ac, d2 / (d2 - d6)));
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F) {
        return anim::add(b, anim::scale(anim::subtract(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    }
    const float denominator = 1.0F / (va + vb + vc);
    return anim::add(a, anim::add(anim::scale(ab, vb * denominator), anim::scale(ac, vc * denominator)));
}

// The triangles listed in the grid cells a sphere's box covers, each once, into `out`.
void trianglesNear(const raycast::CollisionMesh& mesh, anim::Vec3 centre, float radius,
                   std::vector<std::uint16_t>& out) {
    const auto low = mesh.cellOf(toMesh(anim::subtract(centre, anim::Vec3{radius, radius, radius})));
    const auto high = mesh.cellOf(toMesh(anim::add(centre, anim::Vec3{radius, radius, radius})));
    out.clear();
    for (std::uint32_t z = std::min(low[2], high[2]); z <= std::max(low[2], high[2]); ++z) {
        for (std::uint32_t y = std::min(low[1], high[1]); y <= std::max(low[1], high[1]); ++y) {
            for (std::uint32_t x = std::min(low[0], high[0]); x <= std::max(low[0], high[0]); ++x) {
                const auto list = mesh.cellTriangles(x, y, z);
                out.insert(out.end(), list.begin(), list.end());
            }
        }
    }
    std::ranges::sort(out);
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
}

} // namespace

BodyTuning& bodyTuning() {
    static BodyTuning tuning;
    return tuning;
}

float walkingRadius(float scale) { return bodyTuning().radius * scale; }

float walkingCentreHeight(float scale) { return walkingRadius(scale) + bodyTuning().footGap; }

float playerWalkingRadius(float scale) { return walkingRadius(scale * bodyTuning().playerFactor) * scale; }

float playerWalkingCentreHeight(float scale) { return playerWalkingRadius(scale) + bodyTuning().footGap; }

bool wallTooLow(anim::Vec3 a, anim::Vec3 b, anim::Vec3 c, float minHeight) {
    const std::array<anim::Vec3, 3> corners{a, b, c};
    // The edge whose direction is steepest: if even it rises less than the limit, the triangle is a low step.
    float steepest = -1.0F;
    float rise = 0.0F;
    float longest = -1.0F;
    std::size_t longestEdge = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const anim::Vec3 edge = anim::subtract(corners.at((i + 1) % 3), corners.at(i));
        const float length = anim::length(edge);
        if (length <= 0.0F) {
            continue;
        }
        if (const float slope = std::abs(edge.z) / length; slope > steepest) {
            steepest = slope;
            rise = std::abs(edge.z);
        }
        if (length > longest) {
            longest = length;
            longestEdge = i;
        }
    }
    if (rise < minHeight) {
        return true;
    }
    // A thin sliver lying along a non-vertical longest edge: the third corner's distance from that edge's line.
    const anim::Vec3 from = corners.at(longestEdge);
    const anim::Vec3 edge = anim::subtract(corners.at((longestEdge + 1) % 3), from);
    const anim::Vec3 unit = anim::scale(edge, 1.0F / longest);
    if (std::abs(unit.z) >= kVerticalEdge) {
        return false;
    }
    const anim::Vec3 third = anim::subtract(corners.at((longestEdge + 2) % 3), from);
    const anim::Vec3 across = anim::subtract(third, anim::scale(unit, anim::dot(third, unit)));
    return anim::length(across) < minHeight;
}

namespace {

// A wall triangle the sphere at `centre` is in front of, as a wall test sees it: its corners (in the order its edges
// are numbered), its normal turned toward the sphere and the sphere's distance from its plane.
struct FacingWall {
    std::array<anim::Vec3, 3> corners;
    anim::Vec3 normal;
    float distance = 0.0F;
    bool flipped = false; // a two-sided triangle met from behind
};

// Triangle `index` as a wall `filter` keeps for a sphere at `centre`: enabled, not of an excluded material, a wall
// (|n.z| ≤ kWallNormalZ), the sphere in front of it (either side for a two-sided one), the move going into it, and for
// a walking body tall enough to be a wall. Nothing otherwise.
std::optional<FacingWall> keptWall(const raycast::CollisionMesh& mesh, std::uint16_t index, anim::Vec3 centre,
                                   const WallFilter& filter) {
    const raycast::CollisionTriangle& triangle = mesh.triangles()[index];
    if ((triangle.flags & raycast::kTriangleEnabled) == 0 ||
        std::ranges::find(filter.excludeMaterials, triangle.material) != filter.excludeMaterials.end()) {
        return std::nullopt;
    }
    FacingWall wall;
    wall.normal = fromMesh(mesh.faceNormal(index));
    if (anim::length(wall.normal) < 0.5F || std::abs(wall.normal.z) > kWallNormalZ) {
        return std::nullopt; // degenerate, or a floor or ceiling
    }
    const auto vertices = mesh.vertices();
    for (std::size_t i = 0; i < 3; ++i) {
        wall.corners.at(i) = fromMesh(vertices[triangle.vertices.at(i)]);
    }
    wall.distance = anim::dot(wall.normal, anim::subtract(centre, wall.corners[0]));
    if (wall.distance < 0.0F) {
        if ((triangle.flags & raycast::kTriangleTwoSided) == 0) {
            return std::nullopt; // behind a one-sided wall
        }
        wall.normal = anim::scale(wall.normal, -1.0F);
        wall.distance = -wall.distance;
        wall.flipped = true;
    }
    // Only walls the move goes into, and for a walking body only walls tall enough to be walls.
    if (anim::length(filter.move) > 0.0F && anim::dot(wall.normal, filter.move) >= -kMoveInto) {
        return std::nullopt;
    }
    if (filter.skipLow && wallTooLow(wall.corners[0], wall.corners[1], wall.corners[2], bodyTuning().minWallHeight)) {
        return std::nullopt;
    }
    return wall;
}

// How far the sphere at `centre` is from the wall's closest point: face, edge or corner.
float reachOf(const FacingWall& wall, anim::Vec3 centre) {
    return anim::distance(centre, closestOnTriangle(centre, wall.corners[0], wall.corners[1], wall.corners[2]));
}

// The edge a triangle names for the corner slide, in its flag bits 14-15; 3 names none.
constexpr unsigned kSlideEdgeShift = 14;
constexpr unsigned kNoSlideEdge = 3;

} // namespace

std::optional<anim::Vec3> nearestWallPush(const raycast::CollisionMesh& mesh, anim::Vec3 centre, float radius,
                                          const WallFilter& filter, std::vector<std::uint16_t>& scratch) {
    trianglesNear(mesh, centre, radius, scratch);
    // The nearest enabled wall whose face, edge or corner the sphere reaches.
    std::optional<anim::Vec3> push;
    float nearest = radius;
    for (const std::uint16_t index : scratch) {
        const auto wall = keptWall(mesh, index, centre, filter);
        if (!wall) {
            continue;
        }
        // How near the sphere is to the triangle: to its closest point, so that an edge or a corner between two
        // walls holds the body as well as their faces do. The push is along the face's normal.
        const float reach = reachOf(*wall, centre);
        if (reach >= nearest) {
            continue;
        }
        nearest = reach;
        // The walking sweep stops at contact: along the normal until the closest point (on the face, or on an edge
        // such as a low face's top) is a radius away, and the resolver's back-off beyond. The push-out clears the
        // face's plane.
        float clear = radius;
        if (filter.toContact) {
            const float aside = std::max(reach * reach - wall->distance * wall->distance, 0.0F);
            clear = std::sqrt(std::max(radius * radius - aside, 0.0F));
        }
        push = anim::scale(wall->normal, clear + filter.keepClear - wall->distance);
    }
    return push;
}

anim::Vec3 edgeSlide(const raycast::CollisionMesh& mesh, anim::Vec3 centre, float radius, const WallFilter& filter,
                     std::vector<std::uint16_t>& scratch) {
    trianglesNear(mesh, centre, radius, scratch);
    anim::Vec3 sum{};
    for (const std::uint16_t index : scratch) {
        const unsigned named = (mesh.triangles()[index].flags >> kSlideEdgeShift) & 3U;
        if (named == kNoSlideEdge) {
            continue;
        }
        const auto wall = keptWall(mesh, index, centre, filter);
        if (!wall || wall->flipped || reachOf(*wall, centre) >= radius) {
            continue;
        }
        // Only a sphere beyond the named edge counts: outside that edge's line within the face's plane, which is
        // where the sphere wraps round it. (One inside every edge is on the face itself.)
        const anim::Vec3 from = wall->corners.at(named);
        const anim::Vec3 edge = anim::subtract(wall->corners.at((named + 1) % 3), from);
        const anim::Vec3 outward = anim::cross(edge, wall->normal);
        if (anim::dot(outward, centre) <= anim::dot(outward, from)) {
            continue;
        }
        // The wall's tangent n × up, toward the named edge: taken away when the edge rises, added otherwise.
        const anim::Vec3 tangent = anim::cross(wall->normal, anim::Vec3{0.0F, 0.0F, 1.0F});
        sum = edge.z > 0.0F ? anim::subtract(sum, tangent) : anim::add(sum, tangent);
    }
    return sum;
}

} // namespace coney::human
