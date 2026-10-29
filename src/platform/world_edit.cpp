// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the debug tools' world edits; the original has none (see world_edit.h).
#include "platform/world_edit.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <limits>
#include <numbers>
#include <span>
#include <sstream>
#include <utility>

#include <rw.h>

#include "platform/scene_lighting.h"
#include "platform/world_set.h"
#include "raycast/collision_mesh.h"
#include "world/streamed_world.h"

namespace coney::platform {

namespace {

// A point in RenderWare's axes (y up) in the game's (z up): (x, -z, y), the inverse of toRenderWare().
debug::Vec3 gameOf(rw::V3d v) { return debug::Vec3{v.x, -v.z, v.y}; }

// A game-axes offset in RenderWare's axes: (x, z, -y).
world::Vec3 renderWareOf(debug::Vec3 v) { return world::Vec3{v.x, v.z, -v.y}; }

// Whether two boxes overlap (touching counts).
bool overlaps(const debug::GameBox& a, const debug::GameBox& b) {
    return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y && a.min.z <= b.max.z &&
           b.min.z <= a.max.z;
}

// The sectors of every world whose box meets `box`: {world, sector} pairs.
std::vector<std::pair<std::size_t, std::uint32_t>> sectorsUnder(const WorldSet& worlds, const debug::GameBox& box) {
    std::vector<std::pair<std::size_t, std::uint32_t>> found;
    const std::span<world::StreamedWorld* const> all = worlds.worlds();
    for (std::size_t w = 0; w < all.size(); ++w) {
        const std::vector<world::StreamedSector>& sectors = all[w]->sectors();
        for (std::uint32_t k = 0; k < sectors.size(); ++k) {
            if (overlaps(gameBoxOf(sectors[k].box), box)) {
                found.emplace_back(w, k);
            }
        }
    }
    return found;
}

// Places `atomic`'s frame: a uniform `scale` then `position` (RenderWare's axes).
void placeFrame(WorldAtomic& atomic, float scale, world::Vec3 position) {
    atomic.setTransform(world::FrameMatrix{
        .right = {scale, 0.0F, 0.0F}, .up = {0.0F, scale, 0.0F}, .at = {0.0F, 0.0F, scale}, .position = position});
}

} // namespace

GameMesh gameMeshOf(const rw::Atomic* atomic) {
    GameMesh mesh;
    const rw::Geometry* geometry = atomic != nullptr ? atomic->geometry : nullptr;
    if (geometry == nullptr || (geometry->flags & rw::Geometry::NATIVE) != 0 || geometry->triangles == nullptr ||
        geometry->morphTargets[0].vertices == nullptr) {
        return mesh;
    }
    // Sector atomics' frames are a translation (the sector plugin's position); anything else is not read here.
    const rw::V3d origin = const_cast<rw::Atomic*>(atomic)->getFrame()->getLTM()->pos;
    mesh.positions.reserve(static_cast<std::size_t>(geometry->numVertices));
    for (rw::int32 i = 0; i < geometry->numVertices; ++i) {
        const rw::V3d& v = geometry->morphTargets[0].vertices[i];
        mesh.positions.push_back(gameOf(rw::V3d{v.x + origin.x, v.y + origin.y, v.z + origin.z}));
    }
    mesh.triangles.reserve(static_cast<std::size_t>(geometry->numTriangles));
    for (rw::int32 i = 0; i < geometry->numTriangles; ++i) {
        const rw::Triangle& t = geometry->triangles[i];
        mesh.triangles.push_back(debug::MeshTriangle{t.v[0], t.v[1], t.v[2]});
    }
    return mesh;
}

debug::GameBox gameBoxOf(const world::Box& box) {
    return debug::GameBox{debug::Vec3{box.min.x, -box.max.z, box.min.y}, debug::Vec3{box.max.x, -box.min.z, box.max.y}};
}

WorldEditor::WorldEditor(debug::WorldEdits edits, std::function<void(std::string_view)> print)
    : m_edits(std::move(edits)), m_print(std::move(print)), m_cutDone(m_edits.cuts.size(), false),
      m_cutCollision(m_edits.cuts.size()), m_offsets(m_edits.cuts.size()), m_roofCollision(m_edits.roofs.size()),
      m_roofDone(m_edits.roofs.size(), false) {}

bool WorldEditor::drawsWorld(std::size_t world) const { return world >= 2 || !m_edits.hidden.at(world); }

std::optional<world::DebugCamera> WorldEditor::viewerCamera() const {
    if (!m_edits.camera) {
        return std::nullopt;
    }
    const debug::WorldEdits::Camera& c = *m_edits.camera;
    // Heading 0 looks along the game's +y, which is RenderWare's -z; the debug camera's yaw 0 looks along +z and grows
    // towards +x, as the heading does: yaw = pi - heading.
    world::DebugCamera camera(renderWareOf(c.at));
    constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
    camera.setOrientation(std::numbers::pi_v<float> - c.heading * kRadians, c.pitch * kRadians);
    return camera;
}

void WorldEditor::placeSpread(WorldSet& worlds) {
    // Only called with a spread edit (apply()); a missing one has nothing to place.
    if (!m_edits.spread) {
        return;
    }
    const std::span<world::StreamedWorld* const> all = worlds.worlds();
    const debug::WorldEdits::Spread& spread = *m_edits.spread;
    if (m_spread.empty()) {
        // One list for the layout: the `s` world's sectors, the rest of their last row and one row empty, then the
        // `d` world's. A gap's box is empty, so it changes no cell's size.
        std::vector<debug::GameBox> boxes;
        for (std::size_t w = 0; w < all.size(); ++w) {
            if (w > 0) {
                const std::size_t columns = spread.columns;
                const std::size_t pad = (columns - boxes.size() % columns) % columns + columns;
                boxes.insert(boxes.end(), pad, debug::GameBox{});
            }
            m_spreadFirst.push_back(boxes.size());
            for (const world::StreamedSector& sector : all[w]->sectors()) {
                boxes.push_back(gameBoxOf(sector.box));
            }
        }
        m_spread = debug::spreadLayout(boxes, spread.columns, spread.gap, spread.scale, spread.origin, spread.cell);
        // Where the grid ends up, so a camera can be placed to see it: the last cell's centre.
        const debug::SpreadPlacement& last = m_spread.back();
        const debug::Vec3 end{last.centre.x + last.translation.x, last.centre.y + last.translation.y,
                              last.centre.z + last.translation.z};
        m_print(std::format("world edit: spread: {} cells in {} columns, from ({:.1f}, {:.1f}) to ({:.1f}, {:.1f})\n",
                            boxes.size(), spread.columns, spread.origin.x, spread.origin.y, end.x, end.y));
    }
    // Each atomic scaled about its box's centre, then moved to its cell, in RenderWare's axes. The atomic's own
    // vertices are relative to its sector's origin o, so p = o + v goes to c + (o + v - c) s + t = s v + (c (1 - s)
    // + o s + t). An atomic already there is left alone; one the streamer read again stands at its origin and is
    // placed again.
    if (m_placed.empty()) {
        for (const world::StreamedWorld* streamed : all) {
            m_placed.emplace_back(streamed->sectors().size(), false);
        }
    }
    for (std::size_t w = 0; w < all.size(); ++w) {
        const std::vector<world::StreamedSector>& sectors = all[w]->sectors();
        for (std::uint32_t k = 0; k < sectors.size(); ++k) {
            WorldAtomic* atomic = worlds.worldAtomic(w, k);
            if (atomic == nullptr) {
                continue;
            }
            const debug::SpreadPlacement& place = m_spread.at(m_spreadFirst.at(w) + k);
            const world::Vec3 c = renderWareOf(place.centre);
            const world::Vec3 t = renderWareOf(place.translation);
            const world::Vec3 o = sectors[k].origin;
            const float s = place.scale;
            const world::Vec3 at{c.x * (1.0F - s) + o.x * s + t.x, c.y * (1.0F - s) + o.y * s + t.y,
                                 c.z * (1.0F - s) + o.z * s + t.z};
            const rw::Matrix* now = atomic->atomic()->getFrame()->getLTM();
            if (now->right.x == s && now->pos.x == at.x && now->pos.y == at.y && now->pos.z == at.z) {
                continue;
            }
            placeFrame(*atomic, s, at);
            if (!m_placed[w][k]) {
                m_placed[w][k] = true;
                ++m_spreadPlaced;
            }
        }
    }
}

void WorldEditor::rememberCut(std::size_t world, std::uint32_t sector, const rw::Atomic& atomic) {
    const std::int32_t triangles = atomic.geometry->numTriangles;
    for (CutSector& known : m_cutSectors) {
        if (known.world == world && known.sector == sector) {
            known.atomic = &atomic;
            known.triangles = triangles;
            return;
        }
    }
    m_cutSectors.push_back(CutSector{.world = world, .sector = sector, .atomic = &atomic, .triangles = triangles});
}

void WorldEditor::recutReread(WorldSet& worlds) {
    for (CutSector& known : m_cutSectors) {
        WorldAtomic* atomic = worlds.worldAtomic(known.world, known.sector);
        // Freed (cut again when it is back), or as the cuts left it.
        if (atomic == nullptr ||
            (atomic->atomic() == known.atomic && atomic->atomic()->geometry->numTriangles == known.triangles)) {
            continue;
        }
        // Read again from the disc, whole: every cut made so far, in order, takes the same triangles as the first
        // time; the editor already holds their pieces, so these are dropped.
        for (std::size_t c = 0; c < m_edits.cuts.size(); ++c) {
            if (!m_cutDone[c] ||
                !overlaps(gameBoxOf(worlds.worlds()[known.world]->sectors()[known.sector].box), m_edits.cuts[c].box)) {
                continue;
            }
            const GameMesh mesh = gameMeshOf(atomic->atomic());
            (void)atomic->cut(
                debug::trianglesInBox(mesh.positions, mesh.triangles, m_edits.cuts[c].box, m_edits.cuts[c].rule));
        }
        known.atomic = atomic->atomic();
        known.triangles = atomic->atomic()->geometry->numTriangles;
        ++m_recut;
    }
}

void WorldEditor::makeCut(std::size_t index, WorldSet& worlds, raycast::CollisionMesh* collision) {
    const debug::WorldEdits::Cut& cut = m_edits.cuts[index];
    const auto under = sectorsUnder(worlds, cut.box);
    for (const auto& [w, k] : under) {
        if (worlds.loadedAtomic(w, k) == nullptr) {
            return; // not every sector under the box is loaded yet
        }
    }
    std::size_t pieces = 0;
    std::size_t triangles = 0;
    for (const auto& [w, k] : under) {
        WorldAtomic* atomic = worlds.worldAtomic(w, k);
        if (atomic == nullptr) {
            continue;
        }
        const GameMesh mesh = gameMeshOf(atomic->atomic());
        const std::vector<std::uint8_t> take = debug::trianglesInBox(mesh.positions, mesh.triangles, cut.box, cut.rule);
        std::optional<WorldAtomic> piece = atomic->cut(take);
        rememberCut(w, k, *atomic->atomic());
        if (!piece) {
            continue;
        }
        triangles += static_cast<std::size_t>(piece->atomic()->geometry->numTriangles);
        const rw::V3d home = piece->atomic()->getFrame()->getLTM()->pos;
        m_pieces.push_back(
            Piece{.cut = index, .world = w, .atomic = std::move(*piece), .home = {home.x, home.y, home.z}});
        ++pieces;
    }
    // Its collision: the triangles wholly in the box, kept as the disc has them, then switched off there.
    std::size_t collided = 0;
    if (cut.collision && collision != nullptr) {
        m_cutCollision[index] = debug::collisionInBox(*collision, cut.box, debug::Vec3{});
        collision->setEnabledInBox(cut.box.min, cut.box.max, false);
        collided = m_cutCollision[index].size();
        m_collisionDirty = true;
    }
    m_cutDone[index] = true;
    m_cutTriangles += triangles;
    m_print(
        std::format("world edit: cut {}: {} sectors under the box, {} pieces, {} triangles, {} collision triangles\n",
                    cut.name, under.size(), pieces, triangles, collided));
}

bool WorldEditor::makeRoof(std::size_t index, WorldSet& worlds) {
    const debug::WorldEdits::Roof& roof = m_edits.roofs[index];
    const auto under = sectorsUnder(worlds, roof.box);
    for (const auto& [w, k] : under) {
        if (worlds.loadedAtomic(w, k) == nullptr) {
            return false;
        }
    }
    // The `s` world's faces only: the detail world's are decals and small things nobody stands on.
    std::vector<raycast::BuildTriangle> built;
    for (const auto& [w, k] : under) {
        if (w != 0) {
            continue;
        }
        const GameMesh mesh = gameMeshOf(worlds.loadedAtomic(w, k));
        const std::vector<std::uint8_t> take =
            debug::trianglesInBox(mesh.positions, mesh.triangles, roof.box, debug::CutRule::Centroid);
        const std::vector<raycast::BuildTriangle> faces =
            debug::upFacingCollision(mesh.positions, mesh.triangles, take, 0.9F, roof.material);
        built.insert(built.end(), faces.begin(), faces.end());
    }
    // The heights the faces span, so an edit file's author can see which roof the box took.
    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();
    for (const raycast::BuildTriangle& t : built) {
        for (const raycast::Vec3& corner : t.corners) {
            low = std::min(low, corner.z);
            high = std::max(high, corner.z);
        }
    }
    m_roofCollision[index] = std::move(built);
    m_roofDone[index] = true;
    m_print(
        std::format("world edit: roof {}: {} sectors under the box, {} collision triangles from up-facing faces{}\n",
                    index + 1, under.size(), m_roofCollision[index].size(),
                    m_roofCollision[index].empty() ? std::string() : std::format(", z {:.2f} to {:.2f}", low, high)));
    return true;
}

void WorldEditor::rebuildCollision(raycast::CollisionMesh& collision) {
    std::vector<raycast::BuildTriangle> extra;
    for (const std::vector<raycast::BuildTriangle>& roof : m_roofCollision) {
        extra.insert(extra.end(), roof.begin(), roof.end());
    }
    for (std::size_t c = 0; c < m_cutCollision.size(); ++c) {
        const debug::Vec3 d = m_offsets[c];
        for (raycast::BuildTriangle t : m_cutCollision[c]) {
            for (raycast::Vec3& corner : t.corners) {
                corner = raycast::Vec3{corner.x + d.x, corner.y + d.y, corner.z + d.z};
            }
            extra.push_back(t);
        }
    }
    collision.clearExtraTriangles();
    if (extra.empty()) {
        return;
    }
    if (auto added = collision.addExtraTriangles(extra); !added) {
        m_print(std::format("world edit: the collision could not be added: {}\n", added.error().message));
    }
}

void WorldEditor::afterStep(std::uint64_t step, WorldSet& worlds, raycast::CollisionMesh* collision) {
    // The render draws a world's sectors itself unless it is hidden or spread: the spread's are drawn here
    // (drawAfterWorld()), every one wherever the view looks, as they no longer stand in their boxes.
    m_set = &worlds;
    for (std::size_t w = 0; w < worlds.worlds().size(); ++w) {
        worlds.setDrawn(w, drawsWorld(w) && !drawsEverySector());
    }
    if (m_edits.spread) {
        placeSpread(worlds);
    }
    recutReread(worlds);
    for (std::size_t c = 0; c < m_edits.cuts.size(); ++c) {
        if (!m_cutDone[c]) {
            makeCut(c, worlds, collision);
        }
        // The offset of this step; the collision follows when it changed.
        const debug::Vec3 now = debug::offsetAt(m_edits.cuts[c].keys, step);
        if (now.x != m_offsets[c].x || now.y != m_offsets[c].y || now.z != m_offsets[c].z) {
            m_offsets[c] = now;
            m_collisionDirty = m_collisionDirty || !m_cutCollision[c].empty();
        }
    }
    for (std::size_t r = 0; r < m_edits.roofs.size(); ++r) {
        if (!m_roofDone[r] && makeRoof(r, worlds)) {
            m_collisionDirty = true;
        }
    }
    if (m_collisionDirty && collision != nullptr) {
        rebuildCollision(*collision);
    }
    m_collisionDirty = false;
}

void WorldEditor::drawAfterWorld(std::size_t world, SceneLighting& lighting) {
    // The spread's sectors, every loaded one of a world that is not hidden.
    if (drawsEverySector() && drawsWorld(world) && m_set != nullptr && world < m_set->worlds().size()) {
        const std::size_t count = m_set->worlds()[world]->sectors().size();
        for (std::uint32_t k = 0; k < count; ++k) {
            if (rw::Atomic* atomic = m_set->loadedAtomic(world, k); atomic != nullptr) {
                lighting.drawWorldAtomic(atomic);
            }
        }
    }
    for (Piece& piece : m_pieces) {
        if (piece.world != world) {
            continue;
        }
        // Where the disc has it, moved by its cut's offset now.
        const world::Vec3 d = renderWareOf(m_offsets[piece.cut]);
        placeFrame(piece.atomic, 1.0F, world::Vec3{piece.home.x + d.x, piece.home.y + d.y, piece.home.z + d.z});
        lighting.drawWorldAtomic(piece.atomic.atomic());
    }
}

std::string WorldEditor::summary() const {
    std::size_t moved = 0;
    for (const auto& triangles : m_cutCollision) {
        moved += triangles.size();
    }
    std::size_t roofs = 0;
    for (const auto& triangles : m_roofCollision) {
        roofs += triangles.size();
    }
    return std::format("world edits: {} sectors spread, {} pieces cut ({} triangles), {} collision triangles moved, {} "
                       "added for roofs\n",
                       m_spreadPlaced, m_pieces.size(), m_cutTriangles, moved, roofs);
}

std::expected<std::shared_ptr<WorldEditor>, Error> loadWorldEditor(const std::string& path,
                                                                   std::function<void(std::string_view)> print) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("--world-edit: cannot read {}", path));
    }
    std::ostringstream text;
    text << file.rdbuf();
    auto edits = debug::parseWorldEdits(text.str());
    if (!edits) {
        return std::unexpected(std::move(edits.error()));
    }
    return std::make_shared<WorldEditor>(std::move(*edits), std::move(print));
}

} // namespace coney::platform
