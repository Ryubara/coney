// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every level file (`<level>.lev`) is loaded through the chunk system with the
// collision mesh's handler, and its mesh queried. Every triangle facing up gets a point dropped onto it from just
// above, and a slanted ray cast at it through the grid, compared with a brute-force cast over every triangle. It runs
// only when the environment variable CONEY_DISC names the disc and skips otherwise, so CI never needs the game. It
// prints counts only, never data (LEGAL.md). docs/research/collision.md has the results.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "raycast/collision_mesh.h"

namespace {

using coney::raycast::CollisionMesh;
using coney::raycast::Vec3;

// Totals over every level file.
struct CollisionTotals {
    std::uint64_t levels = 0;
    std::uint64_t failures = 0;
    std::uint64_t triangles = 0;
    std::uint64_t vertices = 0;
    std::uint64_t up = 0;   // n.z > cos 15°
    std::uint64_t down = 0; // n.z < -cos 15°
    std::uint64_t walls = 0;
    std::uint64_t dropped = 0;           // drops from 1 above an up-facing triangle's centre that hit
    std::uint64_t droppedOnto = 0;       // ... and landed 0.1 above it (nothing nearer in between)
    std::uint64_t slanted = 0;           // slanted casts at those centres
    std::uint64_t slantedAgree = 0;      // ... where the grid cast and the brute-force cast give the same t
    std::uint64_t slantedGridMissed = 0; // ... where only the brute-force cast hit
};

// The nearest hit of `ray` over every enabled triangle of `mesh`, ignoring the grid: the reference for the grid walk.
std::optional<float> bruteForce(const CollisionMesh& mesh, const coney::raycast::Ray& ray) {
    std::optional<float> best;
    for (std::uint32_t i = 0; i < mesh.triangles().size(); ++i) {
        const auto& t = mesh.triangles()[i];
        const Vec3 a = mesh.vertices()[t.vertices[0]];
        const Vec3 b = mesh.vertices()[t.vertices[1]];
        const Vec3 c = mesh.vertices()[t.vertices[2]];
        // Möller-Trumbore, one- or two-sided as the triangle says (the same rule as the mesh's own test).
        const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 d = ray.direction;
        const Vec3 p{d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x};
        const float det = e1.x * p.x + e1.y * p.y + e1.z * p.z;
        const bool twoSided = (t.flags & coney::raycast::kTriangleTwoSided) != 0;
        if (twoSided ? std::fabs(det) < 1e-6F : det < 1e-6F) {
            continue;
        }
        const Vec3 s{ray.origin.x - a.x, ray.origin.y - a.y, ray.origin.z - a.z};
        const float u = (s.x * p.x + s.y * p.y + s.z * p.z) / det;
        const Vec3 q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
        const float v = (d.x * q.x + d.y * q.y + d.z * q.z) / det;
        if (u < 0.0F || u > 1.0F || v < 0.0F || u + v > 1.0F) {
            continue;
        }
        const float hit = (e2.x * q.x + e2.y * q.y + e2.z * q.z) / det;
        if (hit >= 0.0F && hit <= ray.length && (!best || hit < *best)) {
            best = hit;
        }
    }
    return best;
}

// Queries one level's mesh and adds to the totals.
void checkMesh(const CollisionMesh& mesh, CollisionTotals& totals) {
    const float wallLimit = std::cos(15.0F * std::numbers::pi_v<float> / 180.0F);
    totals.triangles += mesh.triangles().size();
    totals.vertices += mesh.vertices().size();
    for (std::uint32_t i = 0; i < mesh.triangles().size(); ++i) {
        const Vec3 n = mesh.faceNormal(i);
        if (n.z > wallLimit) {
            ++totals.up;
        } else if (n.z < -wallLimit) {
            ++totals.down;
            continue;
        } else {
            ++totals.walls;
            continue;
        }
        const auto& t = mesh.triangles()[i];
        const Vec3 a = mesh.vertices()[t.vertices[0]];
        const Vec3 b = mesh.vertices()[t.vertices[1]];
        const Vec3 c = mesh.vertices()[t.vertices[2]];
        const Vec3 centre{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F, (a.z + b.z + c.z) / 3.0F};

        // Dropped from 1 above: it lands 0.1 above the triangle unless something lies in between.
        Vec3 p{centre.x, centre.y, centre.z + 1.0F};
        if (coney::raycast::dropToGround(mesh, 2.0F, p)) {
            ++totals.dropped;
            if (std::fabs(p.z - (centre.z + 0.1F)) < 1e-3F) {
                ++totals.droppedOnto;
            }
        }

        // Slanted rays through the grid and by brute force: a steep one from 22 away, and a long shallow one from
        // about 102 away that crosses many columns of the grid walk.
        for (const Vec3 offset : {Vec3{12.0F, 9.0F, -16.0F}, Vec3{-80.0F, 60.0F, -20.0F}}) {
            const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
            const Vec3 d{offset.x / length, offset.y / length, offset.z / length};
            const coney::raycast::Ray ray{.origin = {centre.x - offset.x, centre.y - offset.y, centre.z - offset.z},
                                          .direction = d,
                                          .length = length + 1.0F};
            ++totals.slanted;
            const auto grid = mesh.rayCast(ray, {}, 0);
            const auto reference = bruteForce(mesh, ray);
            if ((grid && reference && std::fabs(grid->t - *reference) < 1e-3F) || (!grid && !reference)) {
                ++totals.slantedAgree;
            } else if (!grid && reference) {
                ++totals.slantedGridMissed;
            }
        }
    }
}

} // namespace

TEST_CASE("every level's collision mesh loads and its ground can be found", "[disc][collision]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    table.setHandlers(coney::raycast::kCollisionMeshChunk,
                      coney::chunk::ChunkHandlers{coney::raycast::onCollisionMeshLoaded, {}});

    std::vector<std::string> levels{"objarena"};
    for (int level = 0; level < 200; ++level) {
        levels.push_back(std::format("level{}", level));
    }
    CollisionTotals totals;
    for (const std::string& level : levels) {
        auto entry = wad->lookup(level + ".lev");
        if (!entry) {
            continue;
        }
        ++totals.levels;
        auto stream = wad->openEntry(**entry);
        REQUIRE(stream.has_value());
        coney::chunk::ChunkStacks stacks;
        auto loaded = coney::chunk::loadContainer(*stream, table, stacks);
        if (!loaded) {
            ++totals.failures;
            std::printf("  %s: %s\n", level.c_str(), loaded.error().message.c_str());
            continue;
        }
        auto mesh = stacks.popObject<CollisionMesh>();
        if (!mesh) {
            ++totals.failures;
            std::printf("  %s: %s\n", level.c_str(), mesh.error().message.c_str());
            continue;
        }
        checkMesh(**mesh, totals);
    }

    std::printf("level files: %llu (failed %llu); triangles %llu, vertices %llu; facing up %llu, down %llu, walls "
                "%llu\n",
                static_cast<unsigned long long>(totals.levels), static_cast<unsigned long long>(totals.failures),
                static_cast<unsigned long long>(totals.triangles), static_cast<unsigned long long>(totals.vertices),
                static_cast<unsigned long long>(totals.up), static_cast<unsigned long long>(totals.down),
                static_cast<unsigned long long>(totals.walls));
    std::printf("drops from 1 above: %llu hit, %llu onto their triangle; slanted casts: %llu, %llu agree with brute "
                "force, %llu missed by the grid walk\n",
                static_cast<unsigned long long>(totals.dropped), static_cast<unsigned long long>(totals.droppedOnto),
                static_cast<unsigned long long>(totals.slanted), static_cast<unsigned long long>(totals.slantedAgree),
                static_cast<unsigned long long>(totals.slantedGridMissed));
    CHECK(totals.levels == 64);
    CHECK(totals.failures == 0);
    CHECK(totals.triangles == 150'567);
    CHECK(totals.vertices == 94'294);
    CHECK(totals.dropped == totals.up);
    CHECK(totals.slantedAgree == totals.slanted);
}
