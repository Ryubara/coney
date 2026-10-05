// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_lighting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <span>
#include <utility>

namespace coney::sandbox {

namespace {

// How far a ray starts off the surface, so it does not hit the face its vertex is on.
constexpr float kRayLift = 0.01F;
// How much of the ambient occlusion also darkens the sunlight: a Coney choice, so contact corners stay readable in sun.
constexpr float kSunOcclusionShare = 0.5F;
// Points this close to a primitive's surface do not count as inside it, so touching primitives do not darken.
constexpr float kInsideMargin = 0.001F;

// An axis-aligned box in the world.
struct Bounds {
    anim::Vec3 min{1e30F, 1e30F, 1e30F};
    anim::Vec3 max{-1e30F, -1e30F, -1e30F};

    // Grows the box to hold `p`.
    void add(anim::Vec3 p) {
        min = anim::Vec3{std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = anim::Vec3{std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }

    // Whether `p` is inside the box grown by `margin` on every side.
    [[nodiscard]] bool near(anim::Vec3 p, float margin) const {
        return p.x >= min.x - margin && p.x <= max.x + margin && p.y >= min.y - margin && p.y <= max.y + margin &&
               p.z >= min.z - margin && p.z <= max.z + margin;
    }

    // Whether the segment from `origin` along unit `direction` for `length` passes through the box (slab test).
    [[nodiscard]] bool crossedBy(anim::Vec3 origin, anim::Vec3 direction, float length) const {
        float t0 = 0.0F;
        float t1 = length;
        const std::array<float, 3> o{origin.x, origin.y, origin.z};
        const std::array<float, 3> d{direction.x, direction.y, direction.z};
        const std::array<float, 3> lo{min.x, min.y, min.z};
        const std::array<float, 3> hi{max.x, max.y, max.z};
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (std::fabs(d.at(axis)) < 1e-9F) {
                if (o.at(axis) < lo.at(axis) || o.at(axis) > hi.at(axis)) {
                    return false;
                }
                continue;
            }
            float near = (lo.at(axis) - o.at(axis)) / d.at(axis);
            float far = (hi.at(axis) - o.at(axis)) / d.at(axis);
            if (near > far) {
                std::swap(near, far);
            }
            t0 = std::max(t0, near);
            t1 = std::min(t1, far);
            if (t0 > t1) {
                return false;
            }
        }
        return true;
    }
};

// Whether world point `p` is strictly inside primitive `prim` (by more than kInsideMargin): a floor vertex under a
// box, say, which must be dark so the floor does not glow along the box's foot.
bool inside(const Primitive& prim, anim::Vec3 p) {
    // Into the primitive's own frame: base at the origin, unturned.
    const anim::Vec3 local = turnByYaw(anim::subtract(p, prim.base), -prim.yawDegrees);
    const float m = kInsideMargin;
    const float hx = prim.size.x * 0.5F - m;
    const float hy = prim.size.y * 0.5F - m;
    const bool inFootprint = std::fabs(local.x) < hx && std::fabs(local.y) < hy;
    switch (prim.shape) {
    case Shape::Box:
        return inFootprint && local.z > -m && local.z < prim.size.z - m;
    case Shape::Ramp: {
        // Under the slope: the height grows from 0 at the low end (-y) to the full height at the high end.
        const float surface = prim.size.z * (local.y / prim.size.y + 0.5F);
        return inFootprint && local.z > -m && local.z < surface - m;
    }
    case Shape::Stairs: {
        // Under the tread of the step the point is over.
        const float run = prim.size.y / static_cast<float>(prim.steps);
        const float rise = prim.size.z / static_cast<float>(prim.steps);
        const float step = std::floor((local.y + prim.size.y * 0.5F) / run);
        return inFootprint && local.z > -m && local.z < (step + 1.0F) * rise - m;
    }
    case Shape::Cylinder:
        return std::hypot(local.x, local.y) < prim.radius - m && local.z > -m && local.z < prim.size.z - m;
    case Shape::Sphere:
    case Shape::Capsule: {
        // Distance to the capsule's axis segment, from z = r to z = h - r.
        const float z = std::clamp(local.z, prim.radius, prim.size.z - prim.radius);
        return std::hypot(local.x, local.y, local.z - z) < prim.radius - m;
    }
    }
    return false;
}

// Whether the occlusion of a vertex on `prim` can come from `prim` itself: only stairs have inside corners.
bool concave(const Primitive& prim) { return prim.shape == Shape::Stairs; }

// Two unit vectors that make a right-handed frame with unit `n`.
void tangentFrame(anim::Vec3 n, anim::Vec3& t, anim::Vec3& b) {
    const anim::Vec3 helper = std::fabs(n.z) < 0.9F ? anim::Vec3{0.0F, 0.0F, 1.0F} : anim::Vec3{1.0F, 0.0F, 0.0F};
    t = anim::normalise(anim::cross(helper, n));
    b = anim::cross(n, t);
}

// One collision triangle prepared for ray tests: a corner and the two edges from it.
struct RayTriangle {
    anim::Vec3 a;
    anim::Vec3 e1;
    anim::Vec3 e2;
};

// The distance along unit `direction` from `origin` to the front of `t`, or nothing: a one-sided Möller-Trumbore
// test, as the collision mesh's own, so rays from inside a primitive pass out of it.
std::optional<float> rayHit(const RayTriangle& t, anim::Vec3 origin, anim::Vec3 direction) {
    const anim::Vec3 p = anim::cross(direction, t.e2);
    const float det = anim::dot(t.e1, p);
    if (det < 1e-6F) {
        return std::nullopt;
    }
    const float inverse = 1.0F / det;
    const anim::Vec3 s = anim::subtract(origin, t.a);
    const float u = anim::dot(s, p) * inverse;
    if (u < 0.0F || u > 1.0F) {
        return std::nullopt;
    }
    const anim::Vec3 q = anim::cross(s, t.e1);
    const float v = anim::dot(direction, q) * inverse;
    if (v < 0.0F || u + v > 1.0F) {
        return std::nullopt;
    }
    return anim::dot(t.e2, q) * inverse;
}

// `a` times `s`, channel by channel, plus `b` times `t`.
Colour mix(Colour a, float s, Colour b, float t) {
    return Colour{a.r * s + b.r * t, a.g * s + b.g * t, a.b * s + b.b * t};
}

// A 2D grid over the solid primitives' bounds, so a vertex tests only the primitives near it instead of all of them.
class PrimitiveGrid {
  public:
    // Lists each solid primitive in the cells its bounds, grown by `margin`, overlap.
    PrimitiveGrid(const SandboxLayout& layout, const std::vector<Bounds>& bounds, float margin) {
        for (std::size_t k = 0; k < bounds.size(); ++k) {
            if (layout.primitives[k].surface.solid && bounds[k].min.x <= bounds[k].max.x) {
                m_area.add(anim::subtract(bounds[k].min, anim::Vec3{margin, margin, 0.0F}));
                m_area.add(anim::add(bounds[k].max, anim::Vec3{margin, margin, 0.0F}));
            }
        }
        if (m_area.min.x > m_area.max.x) {
            return; // nothing solid
        }
        m_nx = cellIndex(m_area.max.x, m_area.min.x) + 1;
        m_ny = cellIndex(m_area.max.y, m_area.min.y) + 1;
        m_cells.resize(std::size_t{m_nx} * m_ny);
        m_stamp.assign(bounds.size(), 0);
        for (std::size_t k = 0; k < bounds.size(); ++k) {
            if (!layout.primitives[k].surface.solid || bounds[k].min.x > bounds[k].max.x) {
                continue;
            }
            forCells(bounds[k].min.x - margin, bounds[k].min.y - margin, bounds[k].max.x + margin,
                     bounds[k].max.y + margin,
                     [k](std::vector<std::uint32_t>& cell) { cell.push_back(static_cast<std::uint32_t>(k)); });
        }
    }

    // Calls `visit` once for each primitive listed in the cells over [x0, x1] × [y0, y1]; stops when it returns true.
    template <typename Visit> bool any(float x0, float y0, float x1, float y1, Visit visit) {
        if (m_cells.empty()) {
            return false;
        }
        ++m_query;
        bool found = false;
        forCells(x0, y0, x1, y1, [&](std::vector<std::uint32_t>& cell) {
            for (const std::uint32_t k : cell) {
                if (!found && m_stamp[k] != m_query) {
                    m_stamp[k] = m_query;
                    found = visit(std::size_t{k});
                }
            }
        });
        return found;
    }

  private:
    static constexpr float kCell = 4.0F;

    // The cell index of coordinate `value` along an axis that starts at `low`, at least 0.
    static std::uint32_t cellIndex(float value, float low) {
        return static_cast<std::uint32_t>(std::max(0.0F, std::floor((value - low) / kCell)));
    }

    // Calls `body` on every cell over [x0, x1] × [y0, y1], clamped to the grid.
    template <typename Body> void forCells(float x0, float y0, float x1, float y1, Body body) {
        const std::uint32_t i0 = std::min(cellIndex(x0, m_area.min.x), m_nx - 1);
        const std::uint32_t i1 = std::min(cellIndex(x1, m_area.min.x), m_nx - 1);
        const std::uint32_t j0 = std::min(cellIndex(y0, m_area.min.y), m_ny - 1);
        const std::uint32_t j1 = std::min(cellIndex(y1, m_area.min.y), m_ny - 1);
        for (std::uint32_t j = j0; j <= j1; ++j) {
            for (std::uint32_t i = i0; i <= i1; ++i) {
                body(m_cells[std::size_t{j} * m_nx + i]);
            }
        }
    }

    Bounds m_area;
    std::uint32_t m_nx = 0;
    std::uint32_t m_ny = 0;
    std::vector<std::vector<std::uint32_t>> m_cells;
    std::vector<std::uint32_t> m_stamp; // the query that last visited each primitive, so each is visited once
    std::uint32_t m_query = 0;
};

} // namespace

std::vector<anim::Vec3> hemisphereDirections(std::size_t count) {
    std::vector<anim::Vec3> directions;
    directions.reserve(count);
    // Cosine-weighted: points spread evenly over the unit disc (radius sqrt of an even step), lifted onto the
    // hemisphere; the golden angle turns each one from the last.
    const float golden = std::numbers::pi_v<float> * (3.0F - std::sqrt(5.0F));
    for (std::size_t i = 0; i < count; ++i) {
        const float r = std::sqrt((static_cast<float>(i) + 0.5F) / static_cast<float>(count));
        const float phi = golden * static_cast<float>(i);
        directions.push_back(anim::Vec3{r * std::cos(phi), r * std::sin(phi), std::sqrt(std::max(0.0F, 1.0F - r * r))});
    }
    return directions;
}

BakeStats bakeLighting(SandboxMesh& mesh, const SandboxLayout& layout) {
    const Lighting& light = layout.lighting;
    const anim::Vec3 toSun = anim::scale(anim::normalise(light.sunDirection), -1.0F);
    const std::vector<anim::Vec3> directions = hemisphereDirections(kOcclusionRays);
    BakeStats stats;

    // Each primitive's bounds, from its drawn vertices, and the top of the scene, where shadow rays can stop.
    std::vector<Bounds> bounds(layout.primitives.size());
    float sceneTop = 0.0F;
    for (const MeshVertex& v : mesh.vertices) {
        bounds[v.primitive].add(v.position);
        sceneTop = std::max(sceneTop, v.position.z);
    }
    // Each solid primitive's collision triangles (its untessellated faces), grouped so a ray tests only those of the
    // primitives near it.
    std::vector<std::vector<RayTriangle>> occluders(layout.primitives.size());
    const SandboxMesh coarse = buildSandboxMesh(layout, kNoTessellation);
    for (const MeshTriangle& t : coarse.triangles) {
        if (layout.primitives[t.primitive].surface.solid) {
            const anim::Vec3 a = coarse.vertices[t.vertices[0]].position;
            occluders[t.primitive].push_back(RayTriangle{a, anim::subtract(coarse.vertices[t.vertices[1]].position, a),
                                                         anim::subtract(coarse.vertices[t.vertices[2]].position, a)});
        }
    }
    const float radius = light.occlusionRadius;
    PrimitiveGrid grid(layout, bounds, radius);

    for (MeshVertex& v : mesh.vertices) {
        const Primitive& own = layout.primitives[v.primitive];
        // Whether another solid primitive listed over [x0, x1] × [y0, y1] (or this one, if it has inside corners)
        // passes the test.
        const auto anyOther = [&](float x0, float y0, float x1, float y1, auto test) {
            return grid.any(x0, y0, x1, y1, [&](std::size_t k) {
                const Primitive& other = layout.primitives[k];
                return (k != v.primitive || concave(other)) && test(k, other);
            });
        };
        // The nearest hit within `length` of a ray from `origin` on the primitives near it, or nothing.
        const auto nearest = [&](anim::Vec3 origin, anim::Vec3 direction, float length) {
            const anim::Vec3 end = anim::add(origin, anim::scale(direction, length));
            std::optional<float> best;
            anyOther(std::min(origin.x, end.x), std::min(origin.y, end.y), std::max(origin.x, end.x),
                     std::max(origin.y, end.y), [&](std::size_t k, const Primitive&) {
                         if (bounds[k].crossedBy(origin, direction, length)) {
                             for (const RayTriangle& t : occluders[k]) {
                                 const std::optional<float> hit = rayHit(t, origin, direction);
                                 if (hit && *hit >= 0.0F && *hit <= length && (!best || *hit < *best)) {
                                     best = hit;
                                 }
                             }
                         }
                         return false; // look at every candidate
                     });
            return best;
        };
        const float px = v.position.x;
        const float py = v.position.y;
        // A vertex buried in another solid primitive is in the dark: fully shadowed and occluded.
        const bool buried =
            anyOther(px, py, px, py, [&](std::size_t, const Primitive& other) { return inside(other, v.position); });

        // 1. The sky and ground ambient, by how far the normal points up.
        const float up = 0.5F + 0.5F * v.normal.z;
        const Colour ambient = mix(light.groundAmbient, 1.0F - up, light.skyAmbient, up);

        // 2. The sun, unless something stands between.
        float sun = buried ? 0.0F : std::max(0.0F, anim::dot(v.normal, toSun));
        const anim::Vec3 origin = anim::add(v.position, anim::scale(v.normal, kRayLift));
        if (sun > 0.0F && light.shadows && toSun.z > 0.01F) {
            const float length = (sceneTop - origin.z) / toSun.z + 0.1F;
            if (length > 0.0F) {
                ++stats.shadowRays;
                if (nearest(origin, toSun, length)) {
                    sun = 0.0F;
                    ++stats.shadowed;
                }
            }
        }

        // 3. The ambient occlusion: the nearer the hits round the normal, the darker.
        float occlusion = buried ? 1.0F : 0.0F;
        if (!buried && light.occlusion && anyOther(px, py, px, py, [&](std::size_t k, const Primitive&) {
                return bounds[k].near(v.position, radius);
            })) {
            anim::Vec3 t;
            anim::Vec3 b;
            tangentFrame(v.normal, t, b);
            for (const anim::Vec3& d : directions) {
                const anim::Vec3 direction =
                    anim::add(anim::add(anim::scale(t, d.x), anim::scale(b, d.y)), anim::scale(v.normal, d.z));
                ++stats.occlusionRays;
                if (const std::optional<float> hit = nearest(origin, direction, radius)) {
                    occlusion += 1.0F - *hit / radius;
                }
            }
            occlusion /= static_cast<float>(directions.size());
        }
        const float ao = 1.0F - light.occlusionStrength * occlusion;
        const float sunAo = 1.0F - kSunOcclusionShare * (1.0F - ao);

        // 4. Together, tinted and clamped: librw's vertex colours stop at 1 anyway.
        const Colour lit = mix(ambient, ao, light.sun, sun * sunAo);
        v.colour = Colour{std::min(1.0F, lit.r * own.tint.r), std::min(1.0F, lit.g * own.tint.g),
                          std::min(1.0F, lit.b * own.tint.b)};
    }
    return stats;
}

} // namespace coney::sandbox
