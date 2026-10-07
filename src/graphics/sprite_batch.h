// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <vector>

#include "graphics/overlay_camera.h"
#include "graphics/particle_page.h"
#include "graphics/render_device.h"

namespace coney::graphics {

/// One sprite of a batch: what the original copies into its PTank for the position-and-size format (format 0).
struct Sprite {
    OverlayPoint position; ///< The sprite's centre, in the overlay camera's space.
    float width = 0.0F;    ///< Overlay-camera units.
    float height = 0.0F;   ///< Overlay-camera units.
    UvRect uv;             ///< The part of the sheet's texture shown: usually one of the sheet's rectangles.
    Rgba colour = kWhite;  ///< Multiplies the texture; alpha blends the sprite over what is below.
};

/// One corner of a triangle of a batch: the original's immediate-mode 2D vertices, which some HUD parts draw over a
/// batch's texture (the radar's disc, docs/research/hud.md#the-radar-on-screen).
struct OverlayVertex {
    OverlayPoint position; ///< In the overlay camera's space.
    float u = 0.0F;        ///< Texture coordinate across the whole texture.
    float v = 0.0F;        ///< Texture coordinate down.
    Rgba colour = kWhite;  ///< Multiplies the texture; alpha blends the triangle over what is below.
};

/// A batch of sprites over one sprite sheet, drawn with one texture: the original's resource-manager instance, whose
/// atomic is a PTank. Game code adds sprites to it every frame; the 2D pass (OverlayPass) draws it and empties it.
///
/// Only the original's format 0 (position and size) exists here; the rotated (1) and matrix (2) formats wait for a
/// user. The blend is always source alpha over inverse source alpha, the only one the instances use.
///
/// Research: docs/research/gui.md#resource-instances
class SpriteBatch {
  public:
    /// A batch over `sheet` that holds at most `capacity` sprites a frame and sorts by `depth` in the 2D pass (the
    /// float the original's instances get at creation, 8,000 to 11,000).
    SpriteBatch(SpriteSheet sheet, std::size_t capacity, float depth);

    /// Appends `sprite` for this frame. Returns false and drops it when the batch already holds `capacity` sprites.
    /// @orig 0x00182de0 Instance_AddSprite (unknown)
    bool addSprite(const Sprite& sprite);

    /// Appends one triangle for this frame, drawn with the sheet's texture after the sprites. A triangle with a corner
    /// at or behind the camera is dropped.
    void addTriangle(const OverlayVertex& a, const OverlayVertex& b, const OverlayVertex& c);

    /// Draws the batch's sprites through `camera` onto the logical screen, in the order they were added, as one
    /// drawQuads() call with the sheet's texture, then its triangles as one drawTriangles() call. Does nothing for an
    /// empty batch.
    /// @orig 0x00197168 Instance_Render (unknown)
    void render(RenderDevice& device, const OverlayCamera& camera) const;

    /// How the triangles sample and test (TriangleStates): clamped with no alpha test by default.
    void setTriangleStates(const TriangleStates& states) { m_triangleStates = states; }

    /// Removes this frame's sprites and triangles; the 2D pass does it after drawing.
    void clear() {
        m_sprites.clear();
        m_triangles.clear();
    }

    /// The sheet the sprites come from.
    [[nodiscard]] const SpriteSheet& sheet() const { return m_sheet; }
    /// The most sprites a frame holds.
    [[nodiscard]] std::size_t capacity() const { return m_capacity; }
    /// The sort key in the 2D pass.
    [[nodiscard]] float depth() const { return m_depth; }
    /// This frame's sprites, in the order they were added.
    [[nodiscard]] const std::vector<Sprite>& sprites() const { return m_sprites; }
    /// This frame's triangles, three corners each, in the order they were added.
    [[nodiscard]] const std::vector<OverlayVertex>& triangles() const { return m_triangles; }
    /// The largest number of sprites a frame has held (the original keeps it at `+0x74`).
    [[nodiscard]] std::size_t mostSprites() const { return m_mostSprites; }

  private:
    // The sprites' part of render(): one drawQuads() call.
    void renderSprites(RenderDevice& device, const OverlayCamera& camera) const;

    SpriteSheet m_sheet;
    std::size_t m_capacity;
    float m_depth;
    std::vector<Sprite> m_sprites;
    std::vector<OverlayVertex> m_triangles;
    TriangleStates m_triangleStates;
    std::size_t m_mostSprites = 0;
};

/// The 2D pass: the batches queued this frame, drawn in order of ascending key (the largest ends on top, since depth
/// is not tested), then emptied.
///
/// Coney's choices: batches with equal keys are drawn in the order they were queued (the original sorts with the C
/// library's qsort, which keeps no order among equals); and batches are queued explicitly, where the original finds
/// them by rendering its overlay world.
///
/// Research: docs/research/gui.md#draw-order, docs/research/graphics.md#2d-drawing
class OverlayPass {
  public:
    /// Queues `batch` for this frame with its depth as the key. The batch must stay alive until render().
    void queue(SpriteBatch& batch) { queue(batch, batch.depth()); }
    /// Queues `batch` for this frame with `key`. The batch must stay alive until render().
    void queue(SpriteBatch& batch, float key);

    /// Draws every queued batch through `camera` in ascending key order, then empties each batch and the queue: the
    /// original's pass, which draws a frame's sprites once. draw() and empty() are its two halves.
    void render(RenderDevice& device, const OverlayCamera& camera);

    /// Draws every queued batch through `camera` in ascending key order, keeping the batches and the queue, so a
    /// mode's render() can draw the sprites of its last step as often as the display asks
    /// (docs/guides/conventions.md#update-and-render).
    /// @orig 0x00185d20 ResourceMgr_RenderOverlay (unknown)
    /// @orig 0x00184890 ResourceMgr_CompareOverlayKeys (unknown)
    void draw(RenderDevice& device, const OverlayCamera& camera);

    /// Empties each queued batch and the queue: what a mode's update does before it lists the step's sprites.
    /// @orig 0x00185cc8 ResourceMgr_EmptyInstances (unknown)
    void empty();

    /// Batches queued so far this frame.
    [[nodiscard]] std::size_t queued() const { return m_queue.size(); }

  private:
    // One queued batch and its key.
    struct Entry {
        SpriteBatch* batch;
        float key;
    };
    std::vector<Entry> m_queue;
};

} // namespace coney::graphics
