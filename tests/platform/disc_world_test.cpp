// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every streamed world (`<world>_sec.wld`) and every part it can load
// (`<world>_ms<i>.sec`, i = 1 .. part count) read, each part atomic read with librw and its PS2 native geometry
// decoded, then the vertices placed at the sector's origin and compared with the sector's box: unscaled, with the
// atomic plugin's first float (0x3F0 +0x00) as the scale, and with its second (+0x04). Then each atomic is unpacked
// into plain librw geometry through the game pipeline's stand-in and checked again, with its triangle count against
// the geometry header's. docs/research/world.md has the results. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise, so CI never needs the game. It prints counts only, never data
// (LEGAL.md).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "fileio/disc.h"
#include "fileio/wad.h"
#include "graphics/ps2_world_mesh.h"
#include "platform/render_engine.h"
#include "platform/world_atomic.h"
#include "world/world_streams.h"

namespace {

// The three ways of scaling the packed positions that are compared.
// The last is the first float again with no margin at all.
enum Scaling : std::uint8_t { Unscaled, FirstFloat, SecondFloat, FirstFloatExact, kScalings };

// Totals over the whole disc.
struct WorldTotals {
    std::uint64_t worlds = 0;
    std::uint64_t worldFailures = 0;
    std::uint64_t sectors = 0;         // atomic sectors in the BSPs
    std::uint64_t streamedSectors = 0; // ... with sector plugin data naming a part
    std::uint64_t parts = 0;
    std::uint64_t partFailures = 0;
    std::uint64_t atomics = 0;       // records in the parts
    std::uint64_t unknownSector = 0; // ... naming no streamed sector of the world, or one of another part
    std::uint64_t decoded = 0;       // ... decoded by Coney's PS2 world mesh decoder and read with librw
    std::uint64_t failed = 0;
    std::uint64_t vertices = 0; // decoded vertex references, batches joined
    std::uint64_t batches = 0;
    std::array<std::uint64_t, kScalings> verticesInBox{};  // vertices inside their sector's box
    std::array<std::uint64_t, kScalings> atomicsInBox{};   // atomics whose every vertex is inside
    std::array<std::uint64_t, kScalings> atomicsFillBox{}; // atomics whose vertex bounds are the box, within the margin
    std::uint64_t scalesDiffer = 0;                        // atomics whose two floats differ
    std::array<std::uint64_t, kScalings> fillWhenDiffer{}; // ... whose vertex bounds are the box
    std::uint64_t unpacked = 0;                            // atomics unpacked into plain librw geometry
    std::uint64_t unpackedInBox = 0;                  // ... whose every librw vertex, placed by its frame, is inside
    std::uint64_t trianglesMatch = 0;                 // ... whose triangle count equals the geometry header's
    std::uint64_t triangles = 0;                      // triangles librw derived from the unpacked strips
    std::uint64_t headerTriangles = 0;                // triangles the geometry headers announce
    std::uint64_t distinctVertices = 0;               // vertices of the unpacked geometry (identical ones shared)
    std::uint64_t headerVertices = 0;                 // vertices the geometry headers announce
    std::map<std::uint64_t, std::uint64_t> pipelines; // right-to-render data -> atomics
    std::uint64_t planes = 0;                         // BSP planes
    std::uint64_t planesLeftBelow = 0;                // ... whose left subtree lies below the right on the axis
};

// The union of the sector boxes below `node` of `world`'s BSP; `depth` bounds the recursion on damaged data.
coney::world::Box subtreeBox(const coney::world::WorldStream& world, coney::world::BspChild node, int depth) {
    if (node.leaf || depth > 64) {
        return node.leaf && node.index < world.sectors.size() ? world.sectors[node.index].box : coney::world::Box{};
    }
    const coney::world::BspPlane& plane = world.planes.at(node.index);
    const coney::world::Box a = subtreeBox(world, plane.left, depth + 1);
    const coney::world::Box b = subtreeBox(world, plane.right, depth + 1);
    return coney::world::Box{{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y), std::min(a.min.z, b.min.z)},
                             {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y), std::max(a.max.z, b.max.z)}};
}

// Counts the planes of `world`'s BSP whose left subtree's centre lies below the right subtree's on the plane's axis:
// the side Coney's back-to-front walk (StreamedWorld::collectSectors) takes the left child to be.
void checkBsp(const coney::world::WorldStream& world, WorldTotals& totals) {
    const auto centre = [](const coney::world::Box& box, std::uint32_t axis) {
        const std::array<float, 3> lo{box.min.x, box.min.y, box.min.z};
        const std::array<float, 3> hi{box.max.x, box.max.y, box.max.z};
        return (lo.at(axis) + hi.at(axis)) * 0.5F;
    };
    for (const coney::world::BspPlane& plane : world.planes) {
        ++totals.planes;
        const std::uint32_t axis = std::min<std::uint32_t>(plane.axis, 2);
        if (centre(subtreeBox(world, plane.left, 0), axis) < centre(subtreeBox(world, plane.right, 0), axis)) {
            ++totals.planesLeftBelow;
        }
    }
}

// A sector with an atomic: its box and its plugin data.
struct StreamedSector {
    coney::world::Box box;
    coney::world::SectorPluginData plugin;
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

// How far outside the box a vertex may lie and still count as inside: 1 % of the box's largest side, for rounding in
// the packed positions and boxes that were computed from a slightly different mesh.
float marginOf(const coney::world::Box& box) {
    const float side = std::max({box.max.x - box.min.x, box.max.y - box.min.y, box.max.z - box.min.z});
    return 0.01F * side + 0.001F;
}

// Whether `bounds` is `box`, every face within `margin`.
bool sameBox(const coney::world::Box& bounds, const coney::world::Box& box, float margin) {
    const auto near = [margin](float a, float b) { return a - b <= margin && b - a <= margin; };
    return near(bounds.min.x, box.min.x) && near(bounds.min.y, box.min.y) && near(bounds.min.z, box.min.z) &&
           near(bounds.max.x, box.max.x) && near(bounds.max.y, box.max.y) && near(bounds.max.z, box.max.z);
}

// Grows `bounds` to take in `point`.
void grow(coney::world::Box& bounds, coney::world::Vec3 point) {
    bounds.min = {std::min(bounds.min.x, point.x), std::min(bounds.min.y, point.y), std::min(bounds.min.z, point.z)};
    bounds.max = {std::max(bounds.max.x, point.x), std::max(bounds.max.y, point.y), std::max(bounds.max.z, point.z)};
}

// Decodes every mesh of `atomic` and, for each scaling, counts its vertices inside `sector`'s box and whether the
// vertices' own bounds are that box (a box test alone cannot tell a scale that is too small: it only shrinks the mesh
// towards the origin, which lies inside the box).
std::expected<void, coney::Error> checkPackedVertices(const coney::world::AtomicSection& atomic,
                                                      const coney::world::SectorPluginData& sector,
                                                      const coney::world::Box& box, WorldTotals& totals) {
    const coney::world::AtomicPluginData scales = atomic.plugin.value_or(coney::world::kDefaultAtomicPluginData);
    const std::array<float, kScalings> factor{1.0F, scales.positionScale, scales.secondScale, scales.positionScale};
    const std::array<float, kScalings> margin{marginOf(box), marginOf(box), marginOf(box), 0.0F};
    std::array<bool, kScalings> allInside{true, true, true, true};
    constexpr float kHuge = 1.0e30F;
    std::array<coney::world::Box, kScalings> bounds{};
    bounds.fill(coney::world::Box{{kHuge, kHuge, kHuge}, {-kHuge, -kHuge, -kHuge}});
    for (const coney::world::MeshInfo& mesh : atomic.meshes) {
        auto decoded = coney::graphics::decodePs2WorldMesh(mesh.nativeData, atomic.triangleStrips);
        if (!decoded) {
            return std::unexpected(std::move(decoded.error()));
        }
        totals.batches += decoded->batches;
        totals.vertices += decoded->vertices.size();
        for (const coney::graphics::Ps2PackedVertex& vertex : decoded->vertices) {
            for (std::size_t s = 0; s < kScalings; ++s) {
                const coney::world::Vec3 placed{static_cast<float>(vertex.position[0]) * factor[s] + sector.origin.x,
                                                static_cast<float>(vertex.position[1]) * factor[s] + sector.origin.y,
                                                static_cast<float>(vertex.position[2]) * factor[s] + sector.origin.z};
                grow(bounds[s], placed);
                if (box.contains(placed, margin[s])) {
                    ++totals.verticesInBox[s];
                } else {
                    allInside[s] = false;
                }
            }
        }
    }
    const bool differ = scales.positionScale != scales.secondScale;
    totals.scalesDiffer += differ ? 1 : 0;
    for (std::size_t s = 0; s < kScalings; ++s) {
        totals.atomicsInBox[s] += allInside[s] ? 1 : 0;
        const bool fills = sameBox(bounds[s], box, margin[s]);
        totals.atomicsFillBox[s] += fills ? 1 : 0;
        totals.fillWhenDiffer[s] += fills && differ ? 1 : 0;
    }
    return {};
}

// Unpacks a librw atomic into plain geometry and checks its vertices, placed by its frame, and its triangle count.
void checkUnpacked(coney::platform::WorldAtomic& atomic, const coney::world::Box& box, WorldTotals& totals) {
    atomic.unpack();
    const rw::Geometry* geometry = atomic.atomic()->geometry;
    if ((geometry->flags & rw::Geometry::NATIVE) != 0 || geometry->morphTargets[0].vertices == nullptr) {
        return;
    }
    ++totals.unpacked;
    const rw::V3d origin = atomic.atomic()->getFrame()->getLTM()->pos;
    const float margin = marginOf(box);
    bool inside = true;
    for (rw::int32 i = 0; i < geometry->numVertices; ++i) {
        const rw::V3d& v = geometry->morphTargets[0].vertices[i];
        inside = inside && box.contains(coney::world::Vec3{v.x + origin.x, v.y + origin.y, v.z + origin.z}, margin);
    }
    totals.unpackedInBox += inside ? 1 : 0;
    totals.triangles += static_cast<std::uint64_t>(geometry->numTriangles);
    totals.headerTriangles += atomic.info().triangleCount;
    totals.distinctVertices += static_cast<std::uint64_t>(geometry->numVertices);
    totals.headerVertices += atomic.info().vertexCount;
    totals.trianglesMatch += static_cast<std::uint32_t>(geometry->numTriangles) == atomic.info().triangleCount ? 1 : 0;
}

// Reads one part file and checks each of its atomics against the world's sectors.
void checkPart(const std::vector<std::byte>& file, std::uint32_t partNumber,
               const std::map<std::uint32_t, StreamedSector>& byIndex, WorldTotals& totals) {
    auto part = coney::world::inspectPartFile(file);
    if (!part) {
        ++totals.partFailures;
        std::printf("  part failed: %s\n", part.error().message.c_str());
        return;
    }
    for (const coney::world::PartAtomic& record : part->atomics) {
        ++totals.atomics;
        const auto found = byIndex.find(record.streamedIndex);
        if (found == byIndex.end() || found->second.plugin.part != partNumber) {
            ++totals.unknownSector;
            continue;
        }
        const StreamedSector& sector = found->second;
        const auto section = std::span<const std::byte>(file).subspan(record.offset, record.bytes);
        auto info = coney::world::inspectAtomicSection(section);
        if (!info) {
            if (++totals.failed <= 5) {
                std::printf("  atomic failed: %s\n", info.error().message.c_str());
            }
            continue;
        }
        if (auto decoded = checkPackedVertices(*info, sector.plugin, sector.box, totals); !decoded) {
            if (++totals.failed <= 5) {
                std::printf("  atomic failed: %s\n", decoded.error().message.c_str());
            }
            continue;
        }
        ++totals.pipelines[std::uint64_t{info->pipelinePlugin} << 32 | info->pipeline.value_or(0)];
        auto atomic = coney::platform::WorldAtomic::read(section, sector.plugin.origin);
        if (!atomic) {
            if (++totals.failed <= 5) {
                std::printf("  atomic failed: %s\n", atomic.error().message.c_str());
            }
            continue;
        }
        ++totals.decoded;
        checkUnpacked(*atomic, sector.box, totals);
    }
}

// The names of every world that may exist: `<level>s` and `<level>d`, `<level>` alone, and `objarena`.
std::vector<std::string> candidateWorlds() {
    std::vector<std::string> names{"objarena"};
    for (int level = 0; level < 200; ++level) {
        for (const char* suffix : {"s", "d", ""}) {
            names.push_back(std::format("level{}{}", level, suffix));
        }
    }
    return names;
}

// Prints a count with its share of `whole`.
void printShare(const char* what, std::uint64_t count, std::uint64_t whole) {
    std::printf("  %s: %llu of %llu (%.2f %%)\n", what, static_cast<unsigned long long>(count),
                static_cast<unsigned long long>(whole),
                whole == 0 ? 0.0 : 100.0 * static_cast<double>(count) / static_cast<double>(whole));
}

} // namespace

TEST_CASE("every streamed world's part atomics decode and land in their sectors", "[disc][world]") {
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

    WorldTotals totals;
    for (const std::string& name : candidateWorlds()) {
        auto stream = readEntry(*wad, name + "_sec.wld");
        if (!stream) {
            continue;
        }
        ++totals.worlds;
        auto world = coney::world::inspectWorldStream(*stream);
        if (!world) {
            ++totals.worldFailures;
            std::printf("  world failed: %s\n", world.error().message.c_str());
            continue;
        }
        checkBsp(*world, totals);
        std::map<std::uint32_t, StreamedSector> byIndex;
        for (const coney::world::WorldSector& sector : world->sectors) {
            ++totals.sectors;
            const coney::world::SectorPluginData plugin = sector.plugin.value_or(coney::world::SectorPluginData{});
            if (plugin.streamedIndex >= 0) {
                ++totals.streamedSectors;
                byIndex[static_cast<std::uint32_t>(plugin.streamedIndex)] = StreamedSector{sector.box, plugin};
            }
        }
        for (std::uint32_t i = 1; i <= world->partCount; ++i) {
            auto file = readEntry(*wad, std::format("{}_ms{}.sec", name, i));
            if (!file) {
                ++totals.partFailures;
                continue;
            }
            ++totals.parts;
            checkPart(*file, i, byIndex, totals);
        }
    }

    std::printf("worlds: %llu (failed %llu), atomic sectors: %llu, streamed: %llu, parts: %llu (failed %llu)\n",
                static_cast<unsigned long long>(totals.worlds), static_cast<unsigned long long>(totals.worldFailures),
                static_cast<unsigned long long>(totals.sectors),
                static_cast<unsigned long long>(totals.streamedSectors), static_cast<unsigned long long>(totals.parts),
                static_cast<unsigned long long>(totals.partFailures));
    std::printf(
        "atomics: %llu, decoded and read with librw: %llu, failed: %llu, naming no sector of their part: %llu\n",
        static_cast<unsigned long long>(totals.atomics), static_cast<unsigned long long>(totals.decoded),
        static_cast<unsigned long long>(totals.failed), static_cast<unsigned long long>(totals.unknownSector));
    for (const auto& [key, count] : totals.pipelines) {
        std::printf("  right to render %#llx/%#llx: %llu atomics\n", static_cast<unsigned long long>(key >> 32),
                    static_cast<unsigned long long>(key & 0xFFFFFFFFU), static_cast<unsigned long long>(count));
    }
    std::printf("vertices (strip order, batches joined): %llu in %llu batches\n",
                static_cast<unsigned long long>(totals.vertices), static_cast<unsigned long long>(totals.batches));
    const std::array<const char*, kScalings> names{"unscaled", "scaled by 0x3F0 +0x00", "scaled by 0x3F0 +0x04",
                                                   "scaled by 0x3F0 +0x00, no margin"};
    for (std::size_t s = 0; s < kScalings; ++s) {
        std::printf("%s:\n", names[s]);
        printShare("vertices in their sector's box", totals.verticesInBox[s], totals.vertices);
        printShare("atomics wholly in the box", totals.atomicsInBox[s], totals.decoded + totals.failed);
        printShare("atomics whose vertex bounds are the box", totals.atomicsFillBox[s], totals.decoded + totals.failed);
        printShare("... of the atomics whose two floats differ", totals.fillWhenDiffer[s], totals.scalesDiffer);
    }
    std::printf("unpacked into librw geometry: %llu\n", static_cast<unsigned long long>(totals.unpacked));
    printShare("atomics wholly in the box, placed by their frame", totals.unpackedInBox, totals.unpacked);
    printShare("atomics whose triangle count matches the header", totals.trianglesMatch, totals.unpacked);
    std::printf("  triangles: %llu derived, %llu in the headers\n", static_cast<unsigned long long>(totals.triangles),
                static_cast<unsigned long long>(totals.headerTriangles));
    std::printf("  vertices: %llu distinct, %llu in the headers\n",
                static_cast<unsigned long long>(totals.distinctVertices),
                static_cast<unsigned long long>(totals.headerVertices));

    std::printf("BSP planes: %llu, left subtree below the right on the plane's axis: %llu\n",
                static_cast<unsigned long long>(totals.planes),
                static_cast<unsigned long long>(totals.planesLeftBelow));
    CHECK(totals.worldFailures == 0);
    CHECK(totals.planesLeftBelow == totals.planes);
    CHECK(totals.partFailures == 0);
    CHECK(totals.failed == 0);
    CHECK(totals.unpacked == totals.decoded);
}
