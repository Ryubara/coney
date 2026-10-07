// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/sprite_batch.h"

#include <algorithm>
#include <utility>

namespace coney::graphics {

SpriteBatch::SpriteBatch(SpriteSheet sheet, std::size_t capacity, float depth)
    : m_sheet(std::move(sheet)), m_capacity(capacity), m_depth(depth) {
    m_sprites.reserve(capacity);
}

bool SpriteBatch::addSprite(const Sprite& sprite) {
    if (m_sprites.size() >= m_capacity) {
        return false;
    }
    m_sprites.push_back(sprite);
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
    // Each sprite is centred on its position; its size shrinks with depth as the camera's perspective does. A sprite
    // at or behind the camera cannot be projected and is left out.
    std::vector<LogicalQuad> quads;
    quads.reserve(m_sprites.size());
    for (const Sprite& sprite : m_sprites) {
        if (sprite.position.z <= 0.0F) {
            continue;
        }
        const LogicalPoint centre = camera.project(sprite.position);
        const LogicalPoint size = camera.projectSize(sprite.width, sprite.height, sprite.position.z);
        quads.push_back(
            LogicalQuad{centre.x - size.x / 2, centre.y - size.y / 2, size.x, size.y, sprite.uv, sprite.colour});
    }
    device.drawQuads(m_sheet.texture.get(), quads);
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
