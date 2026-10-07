// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/sprite_batch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace coney::graphics {

namespace {

// The quad of an unturned sprite: centred on its position, its size shrinking with depth as the camera's perspective
// does.
LogicalQuad quadOf(const Sprite& sprite, const OverlayCamera& camera) {
    const LogicalPoint centre = camera.project(sprite.position);
    const LogicalPoint size = camera.projectSize(sprite.width, sprite.height, sprite.position.z);
    return LogicalQuad{centre.x - size.x / 2, centre.y - size.y / 2, size.x, size.y, sprite.uv, sprite.colour};
}

// A sprite turned by `rotation` about its centre as two triangles (top left, top right, bottom right; top left, bottom
// right, bottom left on screen): turned in the overlay camera's plane (y up), then each corner projected, so a turned
// square stays square on screen.
void appendTurned(std::vector<LogicalVertex>& out, const Sprite& sprite, float rotation, const OverlayCamera& camera) {
    const float c = std::cos(rotation);
    const float s = -std::sin(rotation); // clockwise on screen is negative in the overlay plane (y up)
    const float hw = sprite.width / 2.0F;
    const float hh = sprite.height / 2.0F;
    // Top left, top right, bottom right, bottom left on screen (overlay y is up).
    const std::array<std::pair<float, float>, 4> offsets{{{-hw, hh}, {hw, hh}, {hw, -hh}, {-hw, -hh}}};
    // The texture coordinates of the same corners.
    const std::array<std::pair<float, float>, 4> uvs{{{sprite.uv.u0, sprite.uv.v0},
                                                      {sprite.uv.u1, sprite.uv.v0},
                                                      {sprite.uv.u1, sprite.uv.v1},
                                                      {sprite.uv.u0, sprite.uv.v1}}};
    std::array<LogicalVertex, 4> corners{};
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const auto [dx, dy] = offsets.at(i);
        const LogicalPoint p = camera.project(OverlayPoint{sprite.position.x + (dx * c) - (dy * s),
                                                           sprite.position.y + (dx * s) + (dy * c), sprite.position.z});
        corners.at(i) = LogicalVertex{p.x, p.y, uvs.at(i).first, uvs.at(i).second, sprite.colour};
    }
    for (const std::size_t i : {0U, 1U, 2U, 0U, 2U, 3U}) {
        out.push_back(corners.at(i));
    }
}

} // namespace

SpriteBatch::SpriteBatch(SpriteSheet sheet, std::size_t capacity, float depth)
    : m_sheet(std::move(sheet)), m_capacity(capacity), m_depth(depth) {
    m_sprites.reserve(capacity);
    m_rotations.reserve(capacity);
}

bool SpriteBatch::addSprite(const Sprite& sprite) { return addSprite(sprite, 0.0F); }

bool SpriteBatch::addSprite(const Sprite& sprite, float rotation) {
    if (m_sprites.size() >= m_capacity) {
        return false;
    }
    m_sprites.push_back(sprite);
    m_rotations.push_back(rotation);
    m_mostSprites = std::max(m_mostSprites, m_sprites.size());
    return true;
}

void SpriteBatch::addTriangle(const OverlayVertex& a, const OverlayVertex& b, const OverlayVertex& c) {
    if (a.position.z <= 0.0F || b.position.z <= 0.0F || c.position.z <= 0.0F) {
        return;
    }
    m_triangles.insert(m_triangles.end(), {a, b, c});
}

void SpriteBatch::render(RenderDevice& device, const OverlayCamera& camera) const {
    renderSprites(device, camera);
    if (m_triangles.empty()) {
        return;
    }
    // The corners projected as the sprites' centres are; the texture coordinates and colours pass through.
    std::vector<LogicalVertex> vertices;
    vertices.reserve(m_triangles.size());
    for (const OverlayVertex& corner : m_triangles) {
        const LogicalPoint at = camera.project(corner.position);
        vertices.push_back(LogicalVertex{at.x, at.y, corner.u, corner.v, corner.colour});
    }
    device.drawTriangles(m_sheet.texture.get(), vertices, m_triangleStates);
}

void SpriteBatch::renderSprites(RenderDevice& device, const OverlayCamera& camera) const {
    if (m_sprites.empty()) {
        return;
    }
    // Runs of unturned and of turned sprites, each run one draw, so the sprites keep the order they were added in. A
    // sprite at or behind the camera cannot be projected and is left out.
    std::vector<LogicalQuad> quads;
    std::vector<LogicalVertex> turned;
    const auto flush = [&] {
        if (!quads.empty()) {
            device.drawQuads(m_sheet.texture.get(), quads);
            quads.clear();
        }
        if (!turned.empty()) {
            device.drawTriangles(m_sheet.texture.get(), turned, TriangleStates{});
            turned.clear();
        }
    };
    for (std::size_t i = 0; i < m_sprites.size(); ++i) {
        const Sprite& sprite = m_sprites[i];
        if (sprite.position.z <= 0.0F) {
            continue;
        }
        const float rotation = m_rotations[i];
        if (rotation == 0.0F) {
            if (!turned.empty()) {
                flush();
            }
            quads.push_back(quadOf(sprite, camera));
        } else {
            if (!quads.empty()) {
                flush();
            }
            appendTurned(turned, sprite, rotation, camera);
        }
    }
    flush();
}

void OverlayPass::queue(SpriteBatch& batch, float key) { m_queue.push_back(Entry{&batch, key}); }

void OverlayPass::render(RenderDevice& device, const OverlayCamera& camera) {
    draw(device, camera);
    empty();
}

void OverlayPass::draw(RenderDevice& device, const OverlayCamera& camera) {
    // Ascending key: the smallest is drawn first and the largest ends on top. The sort is stable, so sorting again on
    // the next draw of the same queue changes nothing.
    std::ranges::stable_sort(m_queue, [](const Entry& a, const Entry& b) { return a.key < b.key; });
    for (const Entry& entry : m_queue) {
        entry.batch->render(device, camera);
    }
}

void OverlayPass::empty() {
    for (const Entry& entry : m_queue) {
        entry.batch->clear();
    }
    m_queue.clear();
}

} // namespace coney::graphics
