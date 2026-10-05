// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_mesh.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>
#include <span>
#include <utility>

namespace coney::sandbox {

namespace {

constexpr anim::Vec3 kUp{0.0F, 0.0F, 1.0F};
// A face whose every corner is this close to z = 0 (or below) and that faces down is under the ground: left out.
constexpr float kGroundEpsilon = 1e-4F;
// The most pieces one edge is split into, so a huge face with a fine tessellation stays a sane size.
constexpr float kMaxPieces = 512.0F;

// One point of a lathe profile: distance from the axis, height, and the outward normal in the (r, z) plane.
struct ProfilePoint {
    float r = 0.0F;
    float z = 0.0F;
    float nr = 0.0F;
    float nz = 0.0F;
};

// How many pieces an edge of `length` is split into for `tessellation` (at least 1).
std::uint32_t piecesFor(float length, float tessellation) {
    return static_cast<std::uint32_t>(std::clamp(std::ceil(length / tessellation), 1.0F, kMaxPieces));
}

// Writes the triangles of one primitive into a mesh: takes corners in the primitive's own frame (base at the origin,
// unturned), turns and moves them into the world, splits faces for the tessellation and gives every vertex its
// normal and texture coordinates.
class Emitter {
  public:
    Emitter(SandboxMesh& mesh, float tessellation) : m_mesh(mesh), m_tessellation(tessellation) {}

    // Starts primitive `index`: its frame and its texture scale.
    void begin(const Primitive& primitive, std::uint32_t index) {
        m_primitive = &primitive;
        m_index = index;
        m_uvScale = primitive.uvScale / kTileMetres;
        m_axisX = turnByYaw(anim::Vec3{1.0F, 0.0F, 0.0F}, primitive.yawDegrees);
    }

    // A flat four-cornered face, corners counter-clockwise seen from outside (a -> b -> c -> d), split into a grid.
    void quad(anim::Vec3 a, anim::Vec3 b, anim::Vec3 c, anim::Vec3 d) {
        a = world(a);
        b = world(b);
        c = world(c);
        d = world(d);
        const anim::Vec3 normal = anim::normalise(anim::cross(anim::subtract(b, a), anim::subtract(d, a)));
        if (underGround({a, b, c, d}, normal)) {
            return;
        }
        const std::uint32_t nu = piecesFor(std::max(anim::distance(a, b), anim::distance(d, c)), m_tessellation);
        const std::uint32_t nv = piecesFor(std::max(anim::distance(a, d), anim::distance(b, c)), m_tessellation);
        const auto first = static_cast<std::uint32_t>(m_mesh.vertices.size());
        for (std::uint32_t j = 0; j <= nv; ++j) {
            const float t = static_cast<float>(j) / static_cast<float>(nv);
            for (std::uint32_t i = 0; i <= nu; ++i) {
                const float s = static_cast<float>(i) / static_cast<float>(nu);
                const anim::Vec3 p = anim::lerp(anim::lerp(a, b, s), anim::lerp(d, c, s), t);
                addPlanar(p, normal);
            }
        }
        const auto at = [first, nu](std::uint32_t i, std::uint32_t j) { return first + j * (nu + 1) + i; };
        for (std::uint32_t j = 0; j < nv; ++j) {
            for (std::uint32_t i = 0; i < nu; ++i) {
                addTriangle(at(i, j), at(i + 1, j), at(i + 1, j + 1));
                addTriangle(at(i, j), at(i + 1, j + 1), at(i, j + 1));
            }
        }
    }

    // A flat triangle, counter-clockwise seen from outside, split into n² smaller ones.
    void triangle(anim::Vec3 a, anim::Vec3 b, anim::Vec3 c) {
        a = world(a);
        b = world(b);
        c = world(c);
        const anim::Vec3 e1 = anim::subtract(b, a);
        const anim::Vec3 e2 = anim::subtract(c, a);
        const anim::Vec3 normal = anim::normalise(anim::cross(e1, e2));
        if (underGround({a, b, c}, normal)) {
            return;
        }
        const float longest = std::max({anim::length(e1), anim::length(e2), anim::distance(b, c)});
        const std::uint32_t n = piecesFor(longest, m_tessellation);
        // Vertex (i, j) is a + e1·i/n + e2·j/n, for i + j <= n; row j holds n - j + 1 of them.
        std::vector<std::uint32_t> rowStart(n + 2);
        const auto first = static_cast<std::uint32_t>(m_mesh.vertices.size());
        std::uint32_t count = 0;
        for (std::uint32_t j = 0; j <= n; ++j) {
            rowStart[j] = first + count;
            for (std::uint32_t i = 0; i + j <= n; ++i) {
                const float fi = static_cast<float>(i) / static_cast<float>(n);
                const float fj = static_cast<float>(j) / static_cast<float>(n);
                addPlanar(anim::add(a, anim::add(anim::scale(e1, fi), anim::scale(e2, fj))), normal);
                ++count;
            }
        }
        const auto at = [&rowStart](std::uint32_t i, std::uint32_t j) { return rowStart[j] + i; };
        for (std::uint32_t j = 0; j < n; ++j) {
            for (std::uint32_t i = 0; i + j < n; ++i) {
                addTriangle(at(i, j), at(i + 1, j), at(i, j + 1));
                if (i + j + 1 < n) {
                    addTriangle(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
                }
            }
        }
    }

    // A surface of revolution about the primitive's vertical axis: `profile` from bottom to top, turned through a
    // full circle in `segments` steps, with the profile's normals for smooth shading.
    void lathe(std::span<const ProfilePoint> profile, std::uint32_t segments) {
        for (std::size_t k = 0; k + 1 < profile.size(); ++k) {
            const ProfilePoint& lo = profile[k];
            const ProfilePoint& hi = profile[k + 1];
            const float bandLength = std::hypot(hi.r - lo.r, hi.z - lo.z);
            if (bandLength < 1e-6F) {
                continue; // a hard edge: two normals at one point
            }
            // A downward disc on the ground is never seen.
            if (lo.nz < -0.99F && hi.nz < -0.99F && m_primitive->base.z + lo.z <= kGroundEpsilon) {
                continue;
            }
            const std::uint32_t pieces = piecesFor(bandLength, m_tessellation);
            for (std::uint32_t piece = 0; piece < pieces; ++piece) {
                const ProfilePoint p0 = blend(lo, hi, static_cast<float>(piece) / static_cast<float>(pieces));
                const ProfilePoint p1 = blend(lo, hi, static_cast<float>(piece + 1) / static_cast<float>(pieces));
                for (std::uint32_t j = 0; j < segments; ++j) {
                    latheQuad(p0, p1, j, segments, lo, hi);
                }
            }
        }
    }

  private:
    // The world position of a point in the primitive's frame.
    [[nodiscard]] anim::Vec3 world(anim::Vec3 local) const {
        return anim::add(turnByYaw(local, m_primitive->yawDegrees), m_primitive->base);
    }

    // Whether a face lies on or under the ground plane facing down.
    static bool underGround(std::initializer_list<anim::Vec3> corners, anim::Vec3 normal) {
        return normal.z < -0.99F &&
               std::ranges::all_of(corners, [](const anim::Vec3& p) { return p.z <= kGroundEpsilon; });
    }

    // Profile point `t` of the way from `a` to `b`, the normal renormalised.
    static ProfilePoint blend(const ProfilePoint& a, const ProfilePoint& b, float t) {
        ProfilePoint p{a.r + (b.r - a.r) * t, a.z + (b.z - a.z) * t, a.nr + (b.nr - a.nr) * t,
                       a.nz + (b.nz - a.nz) * t};
        const float length = std::hypot(p.nr, p.nz);
        if (length > 0.0F) {
            p.nr /= length;
            p.nz /= length;
        }
        return p;
    }

    // One piece of a lathe band between profile points p0 (lower) and p1, segment j; `lo` and `hi` are the band's
    // ends, which say how it is textured.
    void latheQuad(const ProfilePoint& p0, const ProfilePoint& p1, std::uint32_t j, std::uint32_t segments,
                   const ProfilePoint& lo, const ProfilePoint& hi) {
        const float step = 2.0F * std::numbers::pi_v<float> / static_cast<float>(segments);
        const float theta0 = step * static_cast<float>(j);
        const float theta1 = step * static_cast<float>(j + 1);
        struct Corner {
            anim::Vec3 position;
            anim::Vec3 normal;
            float arc; // distance round the axis, for a side band's texture
        };
        const auto corner = [this](const ProfilePoint& p, float theta) {
            const float c = std::cos(theta);
            const float s = std::sin(theta);
            return Corner{world(anim::Vec3{p.r * c, p.r * s, p.z}),
                          turnByYaw(anim::Vec3{p.nr * c, p.nr * s, p.nz}, m_primitive->yawDegrees), theta * p.r};
        };
        const std::array<Corner, 4> q{corner(p0, theta0), corner(p0, theta1), corner(p1, theta1), corner(p1, theta0)};
        // Texture: a side band unrolls round the axis; a flat disc is projected from above or below; anything else
        // takes the axis nearest the piece's own normal.
        const bool side = std::fabs(lo.nz) < 0.05F && std::fabs(hi.nz) < 0.05F;
        const bool disc = std::fabs(lo.nz) > 0.99F && std::fabs(hi.nz) > 0.99F;
        const anim::Vec3 faceNormal = facetNormal(q[0].position, q[1].position, q[2].position, q[3].position);
        const auto first = static_cast<std::uint32_t>(m_mesh.vertices.size());
        for (const Corner& k : q) {
            float u = 0.0F;
            float v = 0.0F;
            if (side) {
                u = k.arc;
                v = k.position.z;
            } else if (disc) {
                planarUv(k.position, anim::Vec3{0.0F, 0.0F, lo.nz > 0.0F ? 1.0F : -1.0F}, u, v);
            } else {
                triplanarUv(k.position, faceNormal, u, v);
            }
            addVertex(k.position, anim::normalise(k.normal), u * m_uvScale, v * m_uvScale);
        }
        // A corner on the axis makes the piece a triangle.
        if (p0.r <= 0.0F) {
            addTriangle(first, first + 2, first + 3);
        } else if (p1.r <= 0.0F) {
            addTriangle(first, first + 1, first + 2);
        } else {
            addTriangle(first, first + 1, first + 2);
            addTriangle(first, first + 2, first + 3);
        }
    }

    // The outward normal of a lathe piece from whichever of its two triangles is not degenerate.
    static anim::Vec3 facetNormal(anim::Vec3 a, anim::Vec3 b, anim::Vec3 c, anim::Vec3 d) {
        anim::Vec3 n = anim::cross(anim::subtract(c, a), anim::subtract(d, a));
        if (anim::length(n) < 1e-9F) {
            n = anim::cross(anim::subtract(b, a), anim::subtract(c, a));
        }
        return anim::length(n) < 1e-9F ? kUp : anim::normalise(n);
    }

    // A flat face's texture coordinates: `u` along the face's horizontal direction (the primitive's x axis for a face
    // with none), `v` up the face; anchored at the world origin so neighbouring faces line up.
    void planarUv(anim::Vec3 p, anim::Vec3 normal, float& u, float& v) const {
        anim::Vec3 tangent = anim::cross(kUp, normal);
        tangent = anim::length(tangent) < 1e-3F ? m_axisX : anim::normalise(tangent);
        const anim::Vec3 bitangent = anim::cross(normal, tangent);
        u = anim::dot(p, tangent);
        v = anim::dot(p, bitangent);
    }

    // Texture coordinates projected along the world axis nearest `normal`, turned so the pattern reads the same way
    // round on opposite faces.
    static void triplanarUv(anim::Vec3 p, anim::Vec3 normal, float& u, float& v) {
        const float ax = std::fabs(normal.x);
        const float ay = std::fabs(normal.y);
        const float az = std::fabs(normal.z);
        if (az >= ax && az >= ay) {
            u = p.x;
            v = normal.z >= 0.0F ? p.y : -p.y;
        } else if (ax >= ay) {
            u = normal.x >= 0.0F ? p.y : -p.y;
            v = p.z;
        } else {
            u = normal.y >= 0.0F ? -p.x : p.x;
            v = p.z;
        }
    }

    // A vertex of a flat face, textured by planarUv().
    void addPlanar(anim::Vec3 p, anim::Vec3 normal) {
        float u = 0.0F;
        float v = 0.0F;
        planarUv(p, normal, u, v);
        addVertex(p, normal, u * m_uvScale, v * m_uvScale);
    }

    void addVertex(anim::Vec3 p, anim::Vec3 normal, float u, float v) {
        m_mesh.vertices.push_back(MeshVertex{
            .position = p, .normal = normal, .u = u, .v = v, .colour = Colour{1.0F, 1.0F, 1.0F}, .primitive = m_index});
    }

    void addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        m_mesh.triangles.push_back(MeshTriangle{.vertices = {a, b, c}, .primitive = m_index});
    }

    SandboxMesh& m_mesh;
    float m_tessellation;
    const Primitive* m_primitive = nullptr;
    std::uint32_t m_index = 0;
    float m_uvScale = 1.0F;
    anim::Vec3 m_axisX{1.0F, 0.0F, 0.0F};
};

// A box: the footprint's corners at the bottom and the top, then the six faces.
void emitBox(Emitter& out, anim::Vec3 size) {
    const float x = size.x * 0.5F;
    const float y = size.y * 0.5F;
    const float h = size.z;
    out.quad({-x, -y, h}, {x, -y, h}, {x, y, h}, {-x, y, h});   // top
    out.quad({-x, y, 0}, {x, y, 0}, {x, -y, 0}, {-x, -y, 0});   // bottom
    out.quad({x, -y, 0}, {x, y, 0}, {x, y, h}, {x, -y, h});     // +x
    out.quad({-x, y, 0}, {-x, -y, 0}, {-x, -y, h}, {-x, y, h}); // -x
    out.quad({x, y, 0}, {-x, y, 0}, {-x, y, h}, {x, y, h});     // +y
    out.quad({-x, -y, 0}, {x, -y, 0}, {x, -y, h}, {-x, -y, h}); // -y
}

// A wedge rising along +y: the slope, the high end, the two triangular sides and the bottom.
void emitRamp(Emitter& out, anim::Vec3 size) {
    const float x = size.x * 0.5F;
    const float y = size.y * 0.5F;
    const float h = size.z;
    out.quad({-x, -y, 0}, {x, -y, 0}, {x, y, h}, {-x, y, h}); // slope
    out.quad({x, y, 0}, {-x, y, 0}, {-x, y, h}, {x, y, h});   // high end, facing +y
    out.triangle({x, -y, 0}, {x, y, 0}, {x, y, h});           // +x side
    out.triangle({-x, y, 0}, {-x, -y, 0}, {-x, y, h});        // -x side
    out.quad({-x, y, 0}, {x, y, 0}, {x, -y, 0}, {-x, -y, 0}); // bottom
}

// Solid stairs rising along +y: per step its tread, riser and the two side columns under it; then the back and the
// bottom. No face is hidden inside another, so nothing fights for the same pixels.
void emitStairs(Emitter& out, const Primitive& p) {
    const float x = p.size.x * 0.5F;
    const float y0 = -p.size.y * 0.5F;
    const float run = p.size.y / static_cast<float>(p.steps);
    const float rise = p.size.z / static_cast<float>(p.steps);
    for (std::uint32_t i = 0; i < p.steps; ++i) {
        const float front = y0 + run * static_cast<float>(i);
        const float back = front + run;
        const float bottom = rise * static_cast<float>(i);
        const float top = bottom + rise;
        out.quad({-x, front, top}, {x, front, top}, {x, back, top}, {-x, back, top});         // tread
        out.quad({-x, front, bottom}, {x, front, bottom}, {x, front, top}, {-x, front, top}); // riser
        out.quad({x, front, 0}, {x, back, 0}, {x, back, top}, {x, front, top});               // +x side
        out.quad({-x, back, 0}, {-x, front, 0}, {-x, front, top}, {-x, back, top});           // -x side
    }
    const float y1 = -y0;
    const float h = p.size.z;
    out.quad({x, y1, 0}, {-x, y1, 0}, {-x, y1, h}, {x, y1, h}); // back
    out.quad({-x, y1, 0}, {x, y1, 0}, {x, y0, 0}, {-x, y0, 0}); // bottom
}

// The profile of a cylinder: bottom disc, side, top disc, with a hard edge (a repeated point) at each rim.
std::vector<ProfilePoint> cylinderProfile(float r, float h) {
    return {{0, 0, 0, -1}, {r, 0, 0, -1}, {r, 0, 1, 0}, {r, h, 1, 0}, {r, h, 0, 1}, {0, h, 0, 1}};
}

// The profile of a capsule `h` high (a sphere when h = 2r): a half circle split at the equator by the straight part,
// `rings` steps per quarter.
std::vector<ProfilePoint> capsuleProfile(float r, float h, std::uint32_t rings) {
    std::vector<ProfilePoint> profile;
    const float quarter = std::numbers::pi_v<float> * 0.5F;
    // The lower quarter runs from the bottom pole (-90°) to the equator round z = r, the upper from the equator to the
    // top pole round z = h - r; for a sphere the two equator points coincide and that band is skipped.
    for (const auto& [centre, start] : {std::pair{r, -quarter}, std::pair{h - r, 0.0F}}) {
        for (std::uint32_t i = 0; i <= rings; ++i) {
            const float phi = start + quarter * static_cast<float>(i) / static_cast<float>(rings);
            const float c = std::cos(phi);
            const float s = std::sin(phi);
            profile.push_back({c * r, centre + s * r, c, s});
        }
    }
    // The poles are exactly on the axis.
    profile.front().r = 0.0F;
    profile.back().r = 0.0F;
    return profile;
}

// Writes one primitive.
void emitPrimitive(Emitter& out, const Primitive& p) {
    switch (p.shape) {
    case Shape::Box:
        emitBox(out, p.size);
        break;
    case Shape::Ramp:
        emitRamp(out, p.size);
        break;
    case Shape::Stairs:
        emitStairs(out, p);
        break;
    case Shape::Cylinder:
        out.lathe(cylinderProfile(p.radius, p.size.z), p.segments);
        break;
    case Shape::Sphere:
    case Shape::Capsule:
        out.lathe(capsuleProfile(p.radius, p.size.z, std::max(1U, p.segments / 4)), p.segments);
        break;
    }
}

} // namespace

anim::Vec3 turnByYaw(anim::Vec3 local, float yawDegrees) {
    const float angle = yawDegrees * std::numbers::pi_v<float> / 180.0F;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return anim::Vec3{local.x * c - local.y * s, local.x * s + local.y * c, local.z};
}

SandboxMesh buildSandboxMesh(const SandboxLayout& layout, float tessellation) {
    SandboxMesh mesh;
    Emitter out(mesh, tessellation);
    for (std::size_t i = 0; i < layout.primitives.size(); ++i) {
        out.begin(layout.primitives[i], static_cast<std::uint32_t>(i));
        emitPrimitive(out, layout.primitives[i]);
    }
    return mesh;
}

std::vector<raycast::BuildTriangle> sandboxCollisionTriangles(const SandboxLayout& layout) {
    const SandboxMesh mesh = buildSandboxMesh(layout, kNoTessellation);
    std::vector<raycast::BuildTriangle> triangles;
    triangles.reserve(mesh.triangles.size());
    for (const MeshTriangle& t : mesh.triangles) {
        const SurfaceTags& surface = layout.primitives[t.primitive].surface;
        if (!surface.solid) {
            continue;
        }
        raycast::BuildTriangle out{
            .corners = {}, .flags = surface.flags, .material = surface.material, .area = surface.area};
        for (std::size_t k = 0; k < 3; ++k) {
            const anim::Vec3 p = mesh.vertices[t.vertices.at(k)].position;
            out.corners.at(k) = raycast::Vec3{p.x, p.y, p.z};
        }
        // A sliver from a pole or a rim adds nothing to collide with.
        const anim::Vec3 a = mesh.vertices[t.vertices[0]].position;
        const anim::Vec3 e = anim::cross(anim::subtract(mesh.vertices[t.vertices[1]].position, a),
                                         anim::subtract(mesh.vertices[t.vertices[2]].position, a));
        if (anim::length(e) > 1e-8F) {
            triangles.push_back(out);
        }
    }
    return triangles;
}

} // namespace coney::sandbox
