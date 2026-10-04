// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/sheet_viewer_mode.h"

#include <cmath>
#include <cstddef>
#include <utility>

#include "graphics/screen.h"
#include "graphics/texture_grid.h"

namespace coney {

namespace {

// A rectangle's size in texels, rounded as the original's font code rounds a glyph's (docs/research/gui.md#text).
graphics::Extent texelSize(const graphics::UvRect& rect, const graphics::Texture* texture) {
    if (texture == nullptr) {
        return graphics::Extent{1, 1};
    }
    const float width = (rect.u1 - rect.u0) * static_cast<float>(texture->width());
    const float height = (rect.v1 - rect.v0) * static_cast<float>(texture->height());
    return graphics::Extent{static_cast<int>(std::floor(width + 0.5F)), static_cast<int>(std::floor(height + 0.5F))};
}

// Lays the sheet's rectangles out in a grid of logical pixels, then turns each cell into a sprite at the GUI depth, so
// that the camera projects it back onto that cell.
std::vector<graphics::Sprite> layoutSheet(const graphics::SpriteSheet& sheet, const graphics::OverlayCamera& camera) {
    std::vector<graphics::Extent> sizes;
    sizes.reserve(sheet.page.rects.size());
    for (const graphics::UvRect& rect : sheet.page.rects) {
        sizes.push_back(texelSize(rect, sheet.texture.get()));
    }
    const graphics::Extent screen{static_cast<int>(graphics::kLogicalWidth),
                                  static_cast<int>(graphics::kLogicalHeight)};
    const graphics::GridLayout grid = graphics::layoutGrid(sizes, screen, SheetViewerMode::kMargin);
    constexpr float kDepth = graphics::OverlayCamera::kGuiDepth;
    std::vector<graphics::Sprite> sprites;
    sprites.reserve(grid.quads.size());
    for (std::size_t i = 0; i < grid.quads.size(); ++i) {
        const graphics::ScreenRect& cell = grid.quads[i];
        const auto width = static_cast<float>(cell.width);
        const auto height = static_cast<float>(cell.height);
        const graphics::OverlayPoint centre = camera.unproject(
            graphics::LogicalPoint{static_cast<float>(cell.x) + width / 2, static_cast<float>(cell.y) + height / 2},
            kDepth);
        const graphics::LogicalPoint size = camera.unprojectSize(graphics::LogicalPoint{width, height}, kDepth);
        sprites.push_back(graphics::Sprite{centre, size.x, size.y, sheet.page.rect(i), graphics::kWhite});
    }
    return sprites;
}

} // namespace

SheetViewerMode::SheetViewerMode(graphics::RenderDevice& device, const graphics::SpriteSheet& sheet)
    : m_device(device), m_batch(sheet, sheet.page.rects.size(), 0.0F), m_layout(layoutSheet(sheet, m_camera)) {}

// One frame: clear, add every rectangle's sprite to the batch, draw it in the 2D pass, present.
ModeResult SheetViewerMode::update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) {
    m_device.beginFrame(kClearColour);
    for (const graphics::Sprite& sprite : m_layout) {
        m_batch.addSprite(sprite);
    }
    m_pass.queue(m_batch);
    m_pass.render(m_device, m_camera);
    m_device.present();
    return ModeResult::Stay;
}

} // namespace coney
