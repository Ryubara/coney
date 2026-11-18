// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the renderer's seam (docs/engine/renderer.md#frame-description); the original draws straight to its
// device
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "graphics/asset_store.h"
#include "graphics/light_manager.h"
#include "graphics/render_device.h"
#include "graphics/texture_data.h"
#include "graphics/texture_grid.h"

namespace coney::graphics {

/// A texture a frame's 2D draws sample, by its place in the frame's texture table (FrameDescription::textureOf()). Not
/// an AssetStore's TextureId, which the 3D draws name: the table holds the 2D side's own textures (sprite sheets,
/// movie frames) for one frame.
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

/// A light a 3D draw is lit by, by value: what the light manager chose for the mesh and how it is coloured this frame
/// (the lighting lab's layers applied), in RenderWare's axes (docs/research/lighting.md#select).
struct SceneLight {
    LightType type = LightType::Ambient;
    std::array<float, 3> colour{};                     ///< Red, green and blue in RenderWare's 0-1, which may pass 1.
    std::array<float, 3> position{};                   ///< Point and spot lights.
    std::array<float, 3> direction{0.0F, -1.0F, 0.0F}; ///< Spot and directional lights: the way the light travels.
    float radius = 0.0F;                               ///< Point and spot lights.
    float coneAngle = 0.0F;                            ///< Spot lights.

    friend bool operator==(const SceneLight&, const SceneLight&) = default;
};

/// Which faces a 3D draw drops.
enum class CullFaces : std::uint8_t {
    None,
    Back,
    Front,
};

/// Which fragments a 3D draw's alpha test keeps, against the reference (RenderWare's ALPHATESTFUNC).
enum class AlphaTest : std::uint8_t {
    Always,       ///< every one
    GreaterEqual, ///< alpha at or above the reference: a first pass (docs/research/rendering.md#shared-state)
    Less,         ///< alpha below it
};

/// The fixed states a 3D draw is drawn in. The world pass's: depth tested and written, back faces dropped, fogged in
/// the fog's colour, blended by source alpha (docs/research/world.md#a-frame).
struct SceneStates {
    bool depthTest = true;
    bool depthWrite = true;
    CullFaces cull = CullFaces::Back;
    bool fog = true;
    Rgba fogColour;
    bool alphaBlend = true; ///< blended by source alpha over what is below, else replacing it
    AlphaTest alphaTest = AlphaTest::Always;

    friend bool operator==(const SceneStates&, const SceneStates&) = default;
};

/// How a 3D draw changes the colours of its mesh's materials.
enum class MaterialColour : std::uint8_t {
    /// Every material's alpha set to Material3d::fade and kept, written only where it differs: a streamed sector's
    /// fade-in (World_RenderSectorAtomic, docs/research/world.md#a-frame).
    Fade,
    /// The first material's colour replaced by Material3d::tint for this draw alone, the other materials left as
    /// they are, and the mesh modulated by its material colours while the tint is not opaque white: a world object's
    /// tint and alpha, as ObjectRender_Draw writes them (docs/research/objects.md#tint).
    Tint,
    /// Every material's colour set to Material3d::tint and kept: a car part's paint, or white
    /// (docs/research/graphics.md#car-draw).
    Paint,
};

/// What a 3D draw changes about its mesh's own materials this frame.
struct Material3d {
    /// How the materials' colours change (MaterialColour).
    MaterialColour colour = MaterialColour::Fade;
    /// The alpha of every material colour under MaterialColour::Fade: a streamed sector's fade-in
    /// (World_RenderSectorAtomic, docs/research/world.md#a-frame), 255 once faded in.
    std::uint8_t fade = 255;
    /// The colour MaterialColour::Tint gives the first material and MaterialColour::Paint gives every one.
    Rgba tint = kWhite;
    /// Texture levels picked by the distance to the camera, as the original draws the streamed sectors alone
    /// (docs/research/rendering.md#world); level 0 otherwise.
    bool distanceLevels = false;

    friend bool operator==(const Material3d&, const Material3d&) = default;
};

/// Where a mesh is drawn: its axes and position in the world, RenderWare's axes (a frame's matrix).
struct Transform3d {
    std::array<float, 3> right{1.0F, 0.0F, 0.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    std::array<float, 3> at{0.0F, 0.0F, 1.0F};
    std::array<float, 3> position{};

    friend bool operator==(const Transform3d&, const Transform3d&) = default;
};

/// One draw of a scene pass: a mesh of the asset store by id, where it is, what it changes of its materials, its
/// states and its lights; then, when it has one, its dual layer (docs/research/rendering.md#dual), a second mesh on
/// the same frame drawn right after it at the same fade with no alpha test. The textures of each mesh's materials, in
/// the order of MeshData::materials, and the lights are ranges of the frame's arrays (FrameDescription::texturesOf(),
/// dualTexturesOf(), lightsOf()), set when the draw is added.
struct Draw3d {
    MeshId mesh;
    MeshId dualLayer; ///< invalid (0) for none
    Transform3d transform;
    Material3d material;
    SceneStates states;
    std::uint32_t firstLight = 0;
    std::uint32_t lightCount = 0;
    std::uint32_t firstTexture = 0;
    std::uint32_t textureCount = 0;
    std::uint32_t firstDualTexture = 0;
    std::uint32_t dualTextureCount = 0;

    friend bool operator==(const Draw3d&, const Draw3d&) = default;
};

/// A pass of 3D draws through the frame's camera, in their order: the streamed world's sectors (item 7 of
/// docs/engine/renderer.md#order), the world objects and the parked cars (item 8); humans and effects join it with
/// items 9 and 10.
struct ScenePass {
    std::vector<Draw3d> draws;
};

/// One pass of a frame.
using FramePass = std::variant<Overlay2dPass, ScreenPass, ScenePass>;

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
/// It holds the 2D side, the screen passes, the streamed world's sectors, the world objects and the parked cars (scene
/// passes of 3D draws, each a mesh and its textures by AssetStore id). The rest of the 3D scene (the camera, the
/// level's background, humans and effects) still goes to librw directly between them (items 9 and 10 of
/// docs/engine/renderer.md#order move it here), so the GL3 renderer draws each part as it is added rather than the
/// whole frame at its end. Draws are given in logical pixels with the window rectangle they are mapped onto, or in
/// window pixels, and are kept in window pixels.
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
    /// Adds the 3D draw `draw` lit by `lights`, its mesh's material `textures` and its dual layer's `dualTextures` (the
    /// ranges in `draw` are set here) to the scene pass the last pass is, or to a new one; the next 2D draw starts a
    /// new overlay pass.
    void addSceneDraw(Draw3d draw, std::span<const SceneLight> lights, std::span<const TextureId> textures,
                      std::span<const TextureId> dualTextures);

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
    /// The lights of `draw`, a 3D draw of this frame.
    [[nodiscard]] std::span<const SceneLight> lightsOf(const Draw3d& draw) const;
    /// The textures of `draw`'s mesh's materials, in their order (invalid where a material has none, or its texture
    /// is not in the store).
    [[nodiscard]] std::span<const TextureId> texturesOf(const Draw3d& draw) const;
    /// The textures of `draw`'s dual layer's materials, as texturesOf().
    [[nodiscard]] std::span<const TextureId> dualTexturesOf(const Draw3d& draw) const;

  private:
    // The overlay pass the next draw joins: the last pass when it is one, else a new one.
    Overlay2dPass& overlay();
    // The scene pass the next 3D draw joins, as overlay() for 2D draws.
    ScenePass& scene();

    FrameStart m_start;
    std::vector<FramePass> m_passes;
    std::vector<const Texture*> m_textures; // id - 1 is the place
    std::vector<LogicalQuad> m_quads;
    std::vector<ScreenVertex> m_vertices;
    std::vector<SceneLight> m_lights;
    std::vector<TextureId> m_sceneTextures; // the 3D draws' textures, then each one's dual layer's
};

} // namespace coney::graphics
