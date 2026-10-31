// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the renderer's seam (docs/engine/renderer.md#frame-description); the original draws straight to its
// device
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "graphics/render_device.h"
#include "graphics/texture_data.h"
#include "graphics/texture_grid.h"

namespace coney::graphics {

/// A texture a frame's draws sample, by its place in the frame's texture table (FrameDescription::textureOf()). Not
/// an AssetStore's TextureId: the table holds the 2D side's own textures (sprite sheets, movie frames) for one frame.
using FrameTextureId = std::uint32_t;
/// No texture: the draw is flat colour.
inline constexpr FrameTextureId kNoFrameTexture = 0;

/// A copy of the view that a renderer keeps from one frame to the next for a screen pass (the motion blur's last
/// frame), by a number the RenderEngine hands out. The renderer owns what the copy is held in.
using ScreenCopyId = std::uint32_t;

/// What a 2D draw is drawn with beyond the 2D pass's fixed states (no depth test or write, no culling, no fog,
/// blended by source alpha over what is below: docs/research/graphics.md#2d-drawing). The texture is sampled with its
/// own filter.
struct Material2d {
    FrameTextureId texture = kNoFrameTexture;
    /// Past the 0-1 range: Clamp, so a rectangle reaching the texture's border picks up nothing from the other side,
    /// or Wrap, which repeats it (the room smoke's overlay, docs/research/graphics.md#room-smoke).
    TextureAddress address = TextureAddress::Clamp;
    /// The GS alpha test: fragments with alpha below it (0 to 1) are dropped, 0 keeps every one. None: the draw
    /// leaves the frame's alpha test as it stands, as the 2D quads always have (the GL3 renderer draws them with
    /// librw's current test); the world's move onto the description (item 7) gives every draw its own.
    std::optional<float> alphaRef;

    friend bool operator==(const Material2d&, const Material2d&) = default;
};

/// One corner of a 2D triangle, in window pixels from the top left, with its texture coordinate and colour.
struct ScreenVertex {
    float x = 0.0F;
    float y = 0.0F;
    float u = 0.0F;
    float v = 0.0F;
    Rgba colour;

    friend bool operator==(const ScreenVertex&, const ScreenVertex&) = default;
};

/// What a 2D draw's primitives are.
enum class Shape2d : std::uint8_t {
    Quads,     ///< rectangles (FrameDescription::quads(), in window pixels), each two triangles
    Triangles, ///< a list of triangles, three corners each (FrameDescription::vertices())
};

/// One draw of an overlay pass: `count` quads or vertices of the frame's arrays from `first` on, in one material.
struct Draw2d {
    Shape2d shape = Shape2d::Quads;
    Material2d material;
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    friend bool operator==(const Draw2d&, const Draw2d&) = default;
};

/// A pass of 2D draws over what is already drawn, in their order: the front end, the HUD, the washes, the debug menus.
struct Overlay2dPass {
    std::vector<Draw2d> draws;
};

/// The blur pulse's blur: the whole view replaced by a blurred copy of itself, `passes` times the half-size image
/// drawn onto itself with one edge moved in by half of (`offsetU`, `offsetV`) of the original's 512 x 256 blur texture
/// (device slot `+0x108`, docs/research/graphics.md#blur-pulse).
struct BlurPulse {
    int passes = 0;
    float offsetU = 0.0F;
    float offsetV = 0.0F;

    friend bool operator==(const BlurPulse&, const BlurPulse&) = default;
};

/// The PS2 video output's softening: each line the mean of itself and the line `lineShift` of the view's height
/// below it, one of the original's 448 lines (graphics::originalLineShare(), docs/research/rendering.md#output).
struct LineBlend {
    float lineShift = 0.0F;

    friend bool operator==(const LineBlend&, const LineBlend&) = default;
};

/// The motion blur (Coney's stand-in, platform/motion_blur_pass.h): the copy `copy` kept from the last frame laid
/// over the view in `colour`, its alpha the strength, then the view kept in `copy` for the next frame. An alpha of 0
/// lays nothing and forgets the kept frame, so the next blur starts afresh; so does a view of another size.
struct MotionBlurOverlay {
    ScreenCopyId copy = 0;
    Rgba colour;

    friend bool operator==(const MotionBlurOverlay&, const MotionBlurOverlay&) = default;
};

/// A pass that reads what is drawn so far and draws over it, covering `area` (window pixels: the whole 3D view,
/// which in a window wider than 4:3 is wider than the logical screen).
struct ScreenPass {
    ScreenRect area;
    std::variant<BlurPulse, LineBlend, MotionBlurOverlay> effect;

    friend bool operator==(const ScreenPass&, const ScreenPass&) = default;
};

/// One pass of a frame.
using FramePass = std::variant<Overlay2dPass, ScreenPass>;

/// How a frame starts: the colour it is cleared to, and over what.
struct FrameStart {
    Rgba clear = kBlack;
    /// The whole window cleared to `clear` (Coney's tools that lay out in window pixels), rather than the window
    /// cleared to black and the logical screen filled with `clear` by the first overlay draw.
    bool wholeWindow = false;

    friend bool operator==(const FrameStart&, const FrameStart&) = default;
};

/// A backend-neutral description of what a frame draws, the seam between the game's drawing and a renderer
/// (docs/engine/renderer.md#frame-description): the frame's start, then its passes in order, each an overlay pass
/// (a list of 2D draws, each a shape, a material and its primitives) or a screen pass (an effect over the view so
/// far). Textures are held by id, so a description names no graphics API: the GL3 renderer draws it through librw
/// (platform/gl3_renderer.h) and the SDL3 GPU renderer will draw the same description.
///
/// It holds the 2D side and the screen passes. The 3D scene still goes to librw directly between them (items 7 to 10
/// of docs/engine/renderer.md#order move it here), so the GL3 renderer draws each part as it is added rather than
/// the whole frame at its end. Draws are given in logical pixels with the window rectangle they are mapped onto, or
/// in window pixels, and are kept in window pixels.
class FrameDescription {
  public:
    /// Forgets the last frame's passes, textures and primitives and starts a new frame as `start` says.
    void begin(const FrameStart& start);

    /// How the frame starts.
    [[nodiscard]] const FrameStart& start() const { return m_start; }

    /// The id of `texture` in this frame's table, adding it the first time; kNoFrameTexture for null.
    FrameTextureId texture(const Texture* texture);
    /// The texture of `id`; null for kNoFrameTexture or an id this frame has not given out.
    [[nodiscard]] const Texture* textureOf(FrameTextureId id) const;
    /// How many textures the frame's table holds.
    [[nodiscard]] std::size_t textureCount() const { return m_textures.size(); }

    /// Adds `quads`, in logical pixels, mapped onto `onto` (window pixels: the logical screen's place, or the whole
    /// view for passes over the whole picture) with graphics::logicalToWindow(). Nothing is added for no quads.
    void addQuads(const Material2d& material, std::span<const LogicalQuad> quads, const ScreenRect& onto);
    /// Adds `quads` already in window pixels. Nothing is added for no quads.
    void addWindowQuads(const Material2d& material, std::span<const LogicalQuad> quads);
    /// Adds `vertices`, in logical pixels mapped onto `onto`, as a list of triangles; a remainder of fewer than three
    /// is dropped, and nothing is added for no whole triangle.
    void addTriangles(const Material2d& material, std::span<const LogicalVertex> vertices, const ScreenRect& onto);
    /// Adds a screen pass after everything added so far; the next 2D draw starts a new overlay pass.
    void addScreenPass(const ScreenPass& pass);

    /// The passes, in the order they draw.
    [[nodiscard]] std::span<const FramePass> passes() const { return m_passes; }
    /// Every quad of the frame's Quads draws, in window pixels.
    [[nodiscard]] std::span<const LogicalQuad> quads() const { return m_quads; }
    /// Every corner of the frame's Triangles draws, in window pixels.
    [[nodiscard]] std::span<const ScreenVertex> vertices() const { return m_vertices; }
    /// The quads of `draw`, a Quads draw of this frame.
    [[nodiscard]] std::span<const LogicalQuad> quadsOf(const Draw2d& draw) const;
    /// The corners of `draw`, a Triangles draw of this frame.
    [[nodiscard]] std::span<const ScreenVertex> verticesOf(const Draw2d& draw) const;

  private:
    // The overlay pass the next draw joins: the last pass when it is one, else a new one.
    Overlay2dPass& overlay();

    FrameStart m_start;
    std::vector<FramePass> m_passes;
    std::vector<const Texture*> m_textures; // id - 1 is the place
    std::vector<LogicalQuad> m_quads;
    std::vector<ScreenVertex> m_vertices;
};

} // namespace coney::graphics
