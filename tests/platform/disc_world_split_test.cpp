// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc for the question of how a level's streamed world splits into buildings
// (docs/research/world-split.md): every level with a level file, its two worlds' part atomics read and unpacked as the
// game's world is drawn, in the game's axes. It counts the connected pieces of each atomic and of each whole world,
// the pieces that reach into more than one sector, the sectors' boxes that overlap and the vertices two sectors share;
// how far the level's collision triangles lie from the visible `s` world, and how much of the visible ground and roofs
// has collision; and how much of each lies far below the street (the interiors). It runs only when CONEY_DISC names the
// disc and skips otherwise, and prints counts only, never data (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "debug/world_split.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "platform/level_file.h"
#include "platform/render_engine.h"
#include "platform/world_atomic.h"
#include "platform/world_edit.h"
#include "raycast/collision_mesh.h"
#include "world/level_object.h"
#include "world/world_streams.h"

namespace {

using coney::debug::MeshTriangle;
using coney::debug::Vec3;

// Vertices closer than this are one (the welding of connectedPieces()).
constexpr float kWeld = 0.01F;
// Below this the game's z is an interior: the rooms are built about 200 m under the street
// (docs/research/characters.md, Teleport).
constexpr float kInteriorZ = -100.0F;
// A piece this tall and this wide on both axes is the size of a building, not a kerb or a sign.
constexpr float kBuildingSize = 3.0F;

// Counts over the levels.
struct SplitTotals {
    std::uint64_t levels = 0;
    std::uint64_t worlds = 0;
    std::uint64_t sectors = 0;   // sectors with an atomic
    std::uint64_t triangles = 0; // triangles of those atomics
    std::uint64_t atomicPieces = 0;
    std::uint64_t atomicPiecesAtFace = 0; // ... with a vertex on their sector box's faces
    std::uint64_t worldPieces = 0;
    std::uint64_t spanning = 0; // world pieces with triangles in two or more sectors
    std::uint64_t buildingPieces = 0;
    std::uint64_t buildingSpanning = 0;
    std::uint64_t largestPieceTriangles = 0; // summed over the worlds: each world's largest piece
    std::uint64_t seamVertices = 0;          // welded positions that two or more sectors' atomics use
    std::uint64_t vertexPositions = 0;       // welded positions in all
    std::uint64_t boxPairs = 0;
    std::uint64_t overlappingBoxPairs = 0; // ... overlapping by more than 5 cm on every axis
    std::uint64_t collisionTriangles = 0;
    std::array<std::uint64_t, 5> collisionByDistance{}; // centroid to the `s` world: <= 2 cm, 10 cm, 50 cm, 2 m, beyond
    std::uint64_t collisionVertices = 0;
    std::uint64_t collisionVerticesOnVisible = 0; // ... within 2 cm of an `s` world triangle
    double upArea = 0.0;                          // up-facing `s` world faces, square metres
    double upAreaCollided = 0.0;                  // ... with collision within 25 cm above or below the centroid
    double upAreaRoofless = 0.0; // ... without, and 3 m or more above the next collision below (or none below)
    std::uint64_t upTrianglesRoofless = 0;
    std::uint64_t interiorCollision = 0; // collision triangles below kInteriorZ
    std::uint64_t interiorSectors = 0;   // sectors whose box lies wholly below kInteriorZ
    std::uint64_t levelsWithInteriors = 0;
};

// Reads a whole WAD entry by name; nothing when there is no such entry.
std::optional<std::vector<std::byte>> readEntry(const coney::io::Wad& wad, const std::string& name) {
    auto entry = wad.lookup(name);
    if (!entry) {
        return std::nullopt;
    }
    auto stream = wad.openEntry(**entry);
    if (!stream) {
        return std::nullopt;
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(stream->size()));
    if (!stream->read(bytes)) {
        return std::nullopt;
    }
    return bytes;
}

// One world's sector atomics in the game's axes: one mesh, each triangle's sector, each sector's box.
struct WorldMesh {
    std::vector<Vec3> positions;
    std::vector<MeshTriangle> triangles;
    std::vector<std::uint32_t> sectorOf;      // per triangle
    std::vector<std::uint32_t> vertexOf;      // per position, its sector
    std::vector<coney::debug::GameBox> boxes; // per sector with an atomic, in reading order
};

// Reads world `name`'s every part, unpacks each atomic and appends it to `mesh`, counting its own pieces.
void readWorld(const coney::io::Wad& wad, const std::string& name, WorldMesh& mesh, SplitTotals& totals) {
    auto stream = readEntry(wad, name + "_sec.wld");
    if (!stream) {
        return;
    }
    auto world = coney::world::inspectWorldStream(*stream);
    REQUIRE(world.has_value());
    ++totals.worlds;
    std::map<std::uint32_t, const coney::world::WorldSector*> byIndex;
    for (const coney::world::WorldSector& sector : world->sectors) {
        if (sector.plugin && sector.plugin->streamedIndex >= 0) {
            byIndex[static_cast<std::uint32_t>(sector.plugin->streamedIndex)] = &sector;
        }
    }
    for (std::uint32_t i = 1; i <= world->partCount; ++i) {
        auto file = readEntry(wad, std::format("{}_ms{}.sec", name, i));
        // Read through a pointer, which the optional-access check follows past a failed REQUIRE.
        const std::vector<std::byte>* const bytes = file ? &*file : nullptr;
        REQUIRE(bytes != nullptr);
        auto part = coney::world::inspectPartFile(*bytes);
        REQUIRE(part.has_value());
        for (const coney::world::PartAtomic& record : part->atomics) {
            const auto found = byIndex.find(record.streamedIndex);
            REQUIRE(found != byIndex.end());
            const coney::world::WorldSector& sector = *found->second;
            const auto* const plugin = sector.plugin ? &*sector.plugin : nullptr;
            REQUIRE(plugin != nullptr);
            auto atomic = coney::platform::WorldAtomic::read(
                std::span<const std::byte>(*bytes).subspan(record.offset, record.bytes), plugin->origin);
            REQUIRE(atomic.has_value());
            atomic->unpack();
            const coney::platform::GameMesh own = coney::platform::gameMeshOf(atomic->atomic());
            const coney::debug::GameBox box = coney::platform::gameBoxOf(sector.box);
            // Its own pieces, and those that touch its box's faces (cut where the sector ends).
            const coney::debug::MeshPieces pieces = coney::debug::connectedPieces(own.positions, own.triangles, kWeld);
            std::vector<bool> atFace(pieces.count, false);
            const auto onFace = [&box](Vec3 p) {
                const float m = 0.02F;
                return std::abs(p.x - box.min.x) < m || std::abs(p.x - box.max.x) < m ||
                       std::abs(p.y - box.min.y) < m || std::abs(p.y - box.max.y) < m ||
                       std::abs(p.z - box.min.z) < m || std::abs(p.z - box.max.z) < m;
            };
            for (std::size_t t = 0; t < own.triangles.size(); ++t) {
                for (const std::uint32_t v : own.triangles[t]) {
                    if (onFace(own.positions[v])) {
                        atFace[pieces.pieceOf[t]] = true;
                    }
                }
            }
            totals.atomicPieces += pieces.count;
            totals.atomicPiecesAtFace += static_cast<std::uint64_t>(std::count(atFace.begin(), atFace.end(), true));
            ++totals.sectors;
            totals.triangles += own.triangles.size();
            // Into the world's one mesh.
            const auto sectorNumber = static_cast<std::uint32_t>(mesh.boxes.size());
            mesh.boxes.push_back(box);
            const auto base = static_cast<std::uint32_t>(mesh.positions.size());
            mesh.positions.insert(mesh.positions.end(), own.positions.begin(), own.positions.end());
            mesh.vertexOf.insert(mesh.vertexOf.end(), own.positions.size(), sectorNumber);
            for (const MeshTriangle& t : own.triangles) {
                mesh.triangles.push_back(MeshTriangle{t[0] + base, t[1] + base, t[2] + base});
                mesh.sectorOf.push_back(sectorNumber);
            }
        }
    }
}

// The pieces of a whole world, welded across its sectors: how many, how many reach into two or more sectors, which of
// them are building-sized, its largest; the positions two sectors share; the boxes that overlap.
void countWorld(const WorldMesh& mesh, SplitTotals& totals) {
    const coney::debug::MeshPieces pieces = coney::debug::connectedPieces(mesh.positions, mesh.triangles, kWeld);
    totals.worldPieces += pieces.count;
    std::vector<std::set<std::uint32_t>> sectorsOf(pieces.count);
    std::vector<coney::debug::GameBox> bounds(pieces.count,
                                              coney::debug::GameBox{{1e9F, 1e9F, 1e9F}, {-1e9F, -1e9F, -1e9F}});
    std::vector<std::uint64_t> sizes(pieces.count, 0);
    for (std::size_t t = 0; t < mesh.triangles.size(); ++t) {
        const std::uint32_t p = pieces.pieceOf[t];
        sectorsOf[p].insert(mesh.sectorOf[t]);
        ++sizes[p];
        for (const std::uint32_t v : mesh.triangles[t]) {
            const Vec3 q = mesh.positions[v];
            bounds[p].min =
                Vec3{std::min(bounds[p].min.x, q.x), std::min(bounds[p].min.y, q.y), std::min(bounds[p].min.z, q.z)};
            bounds[p].max =
                Vec3{std::max(bounds[p].max.x, q.x), std::max(bounds[p].max.y, q.y), std::max(bounds[p].max.z, q.z)};
        }
    }
    for (std::uint32_t p = 0; p < pieces.count; ++p) {
        const bool spans = sectorsOf[p].size() > 1;
        totals.spanning += spans ? 1 : 0;
        const coney::debug::GameBox& b = bounds[p];
        if (b.max.z - b.min.z >= kBuildingSize && b.max.x - b.min.x >= kBuildingSize &&
            b.max.y - b.min.y >= kBuildingSize) {
            ++totals.buildingPieces;
            totals.buildingSpanning += spans ? 1 : 0;
        }
    }
    totals.largestPieceTriangles += sizes.empty() ? 0 : *std::max_element(sizes.begin(), sizes.end());
    // Welded positions and the sectors using each.
    std::map<std::tuple<std::int64_t, std::int64_t, std::int64_t>, std::set<std::uint32_t>> users;
    for (std::size_t v = 0; v < mesh.positions.size(); ++v) {
        const Vec3 q = mesh.positions[v];
        users[{std::llround(q.x / kWeld), std::llround(q.y / kWeld), std::llround(q.z / kWeld)}].insert(
            mesh.vertexOf[v]);
    }
    totals.vertexPositions += users.size();
    for (const auto& [where, sectors] : users) {
        totals.seamVertices += sectors.size() > 1 ? 1 : 0;
    }
    for (std::size_t a = 0; a < mesh.boxes.size(); ++a) {
        for (std::size_t b = a + 1; b < mesh.boxes.size(); ++b) {
            ++totals.boxPairs;
            const coney::debug::GameBox& p = mesh.boxes[a];
            const coney::debug::GameBox& q = mesh.boxes[b];
            const float m = 0.05F;
            if (std::min(p.max.x, q.max.x) - std::max(p.min.x, q.min.x) > m &&
                std::min(p.max.y, q.max.y) - std::max(p.min.y, q.min.y) > m &&
                std::min(p.max.z, q.max.z) - std::max(p.min.z, q.min.z) > m) {
                ++totals.overlappingBoxPairs;
            }
        }
    }
}

// The level's collision against its visible `s` world: distances, shared vertices, the up-facing faces with and
// without collision, and what lies below the street.
void countCollision(const coney::raycast::CollisionMesh& collision, const WorldMesh& solid, SplitTotals& totals) {
    const coney::debug::TriangleGrid grid(solid.positions, solid.triangles, 2.0F);
    const std::array<float, 4> limits{0.02F, 0.1F, 0.5F, 2.0F};
    const std::span<const coney::raycast::Vec3> vertices = collision.vertices();
    bool interior = false;
    for (const coney::raycast::CollisionTriangle& t : collision.triangles()) {
        ++totals.collisionTriangles;
        const Vec3 a = vertices[t.vertices[0]];
        const Vec3 b = vertices[t.vertices[1]];
        const Vec3 c = vertices[t.vertices[2]];
        const Vec3 centre{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F, (a.z + b.z + c.z) / 3.0F};
        const std::optional<float> d = grid.nearest(centre, 2.0F);
        std::size_t bin = limits.size();
        for (std::size_t i = 0; i < limits.size(); ++i) {
            if (d && *d <= limits[i]) {
                bin = i;
                break;
            }
        }
        ++totals.collisionByDistance.at(bin);
        if (centre.z < kInteriorZ) {
            ++totals.interiorCollision;
            interior = true;
        }
    }
    for (const coney::raycast::Vec3& v : vertices) {
        ++totals.collisionVertices;
        const std::optional<float> d = grid.nearest(v, 0.02F);
        totals.collisionVerticesOnVisible += d ? 1 : 0;
    }
    totals.levelsWithInteriors += interior ? 1 : 0;
    // The visible up-facing faces: collision within 25 cm of the centroid, up or down; else how far down the next is.
    for (const MeshTriangle& t : solid.triangles) {
        const Vec3 a = solid.positions[t[0]];
        const Vec3 b = solid.positions[t[1]];
        const Vec3 c = solid.positions[t[2]];
        const Vec3 u{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 w{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 n{u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x};
        const float twice = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (twice <= 1e-6F || std::abs(n.z) / twice < 0.9F) {
            continue;
        }
        const double area = 0.5 * static_cast<double>(twice);
        totals.upArea += area;
        const Vec3 centre{(a.x + b.x + c.x) / 3.0F, (a.y + b.y + c.y) / 3.0F, (a.z + b.z + c.z) / 3.0F};
        const auto near =
            collision.rayCast(coney::raycast::Ray{{centre.x, centre.y, centre.z + 0.25F}, {0, 0, -1}, 0.5F}, {}, 0x1);
        if (near) {
            totals.upAreaCollided += area;
            continue;
        }
        const auto below =
            collision.rayCast(coney::raycast::Ray{{centre.x, centre.y, centre.z - 0.25F}, {0, 0, -1}, 500.0F}, {}, 0x1);
        if (!below || below->t >= 2.75F) {
            totals.upAreaRoofless += area;
            ++totals.upTrianglesRoofless;
        }
    }
}

} // namespace

TEST_CASE("how each level's streamed world splits into pieces and lines up with its collision", "[disc][world_split]") {
    // Expected: unverified, #1123 (the counts are the research's: docs/research/world-split.md; the test checks only
    // that every level reads and that its worlds give triangles, pieces and collision distances to count)
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());

    SplitTotals totals;
    for (int number = 0; number < 200; ++number) {
        const std::string level = std::format("level{}", number);
        if (!wad->lookup(level + ".lev") || !wad->lookup(level + "s_sec.wld")) {
            continue;
        }
        ++totals.levels;
        const SplitTotals before = totals;
        WorldMesh solid;
        WorldMesh detail;
        readWorld(*wad, level + "s", solid, totals);
        readWorld(*wad, level + "d", detail, totals);
        countWorld(solid, totals);
        countWorld(detail, totals);
        for (const WorldMesh* mesh : {&solid, &detail}) {
            for (const coney::debug::GameBox& box : mesh->boxes) {
                totals.interiorSectors += box.max.z < kInteriorZ ? 1 : 0;
            }
        }
        auto loaded = coney::platform::loadLevel(*wad, level, false);
        REQUIRE(loaded.has_value());
        REQUIRE((*loaded)->collision != nullptr);
        countCollision(*(*loaded)->collision, solid, totals);
        // One line a level, counts only.
        const auto u = [](std::uint64_t v) { return static_cast<unsigned long long>(v); };
        std::printf(
            "%s: s %zu sectors %zu triangles, d %zu sectors %zu triangles; world pieces %llu (%llu in two or "
            "more sectors, building-sized %llu, %llu of them in two or more); collision %zu triangles, %llu "
            "within 2 cm of the s world, %llu beyond 50 cm; up-facing %.0f m2, with collision %.0f, roofless "
            "%.0f; below the street %llu collision triangles\n",
            level.c_str(), solid.boxes.size(), solid.triangles.size(), detail.boxes.size(), detail.triangles.size(),
            u(totals.worldPieces - before.worldPieces), u(totals.spanning - before.spanning),
            u(totals.buildingPieces - before.buildingPieces), u(totals.buildingSpanning - before.buildingSpanning),
            (*loaded)->collision->triangles().size(), u(totals.collisionByDistance[0] - before.collisionByDistance[0]),
            u(totals.collisionByDistance[3] + totals.collisionByDistance[4] - before.collisionByDistance[3] -
              before.collisionByDistance[4]),
            totals.upArea - before.upArea, totals.upAreaCollided - before.upAreaCollided,
            totals.upAreaRoofless - before.upAreaRoofless, u(totals.interiorCollision - before.interiorCollision));
    }

    const auto u = [](std::uint64_t v) { return static_cast<unsigned long long>(v); };
    std::printf("levels %llu, worlds %llu, sectors %llu, triangles %llu\n", u(totals.levels), u(totals.worlds),
                u(totals.sectors), u(totals.triangles));
    std::printf("pieces per atomic: %llu in all, %llu touching their sector box's faces\n", u(totals.atomicPieces),
                u(totals.atomicPiecesAtFace));
    std::printf("pieces per world (welded across sectors): %llu, %llu in two or more sectors; building-sized %llu, "
                "%llu of them in two or more sectors; the worlds' largest pieces hold %llu triangles\n",
                u(totals.worldPieces), u(totals.spanning), u(totals.buildingPieces), u(totals.buildingSpanning),
                u(totals.largestPieceTriangles));
    std::printf("welded vertex positions %llu, used by two or more sectors %llu; box pairs %llu, overlapping %llu\n",
                u(totals.vertexPositions), u(totals.seamVertices), u(totals.boxPairs), u(totals.overlappingBoxPairs));
    std::printf("collision triangles %llu: centroid within 2 cm of the s world %llu, 10 cm %llu, 50 cm %llu, 2 m %llu, "
                "beyond %llu; vertices %llu, within 2 cm of an s world triangle %llu\n",
                u(totals.collisionTriangles), u(totals.collisionByDistance[0]), u(totals.collisionByDistance[1]),
                u(totals.collisionByDistance[2]), u(totals.collisionByDistance[3]), u(totals.collisionByDistance[4]),
                u(totals.collisionVertices), u(totals.collisionVerticesOnVisible));
    std::printf("up-facing s world faces %.0f m2: with collision %.0f m2, without and 3 m or more above the next "
                "collision %.0f m2 (%llu triangles)\n",
                totals.upArea, totals.upAreaCollided, totals.upAreaRoofless, u(totals.upTrianglesRoofless));
    std::printf("below z %.0f: %llu collision triangles in %llu levels, %llu sectors\n",
                static_cast<double>(kInteriorZ), u(totals.interiorCollision), u(totals.levelsWithInteriors),
                u(totals.interiorSectors));
    CHECK(totals.levels > 0);
    CHECK(totals.sectors > 0);
    CHECK(totals.triangles > 0);
    CHECK(totals.worldPieces > 0);
    CHECK(totals.collisionByDistance[0] > 0);
}
