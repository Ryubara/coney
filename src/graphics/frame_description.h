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
    /// The first material's colour replaced by Material3d::tint with its red, green and blue times Material3d::shade
    /// (truncated) and its alpha Material3d::fade, and kept: a human's colour (HuColor, white unless a script sets it)
    /// times the shadow dimming and his distance fade, as HumanRender_Draw writes them
    /// (docs/research/graphics.md#human-draw, steps 3 and 4).
    Shade,
    /// The materials' colours as the mesh has them, nothing written: the level's background, which the original draws
    /// as loaded (LevelObject_RenderBackground, docs/research/level-loading.md#render-order).
    Own,
};

/// What a 3D draw changes about its mesh's own materials this frame.
struct Material3d {
    /// How the materials' colours change (MaterialColour).
    MaterialColour colour = MaterialColour::Fade;
    /// The alpha of every material colour under MaterialColour::Fade: a streamed sector's fade-in
    /// (World_RenderSectorAtomic, docs/research/world.md#a-frame), 255 once faded in.
    std::uint8_t fade = 255;
    /// The colour MaterialColour::Tint gives the first material, MaterialColour::Paint gives every one and
    /// MaterialColour::Shade dims for the first: an object's tint, a car's paint, a human's colour.
    Rgba tint = kWhite;
    /// What MaterialColour::Shade multiplies the human's red, green and blue by (0 to 1): a human's shadow dimming
    /// (graphics::ShadowDim::factor()), 1 for a human it does not dim.
    float shade = 1.0F;
    /// Texture levels picked by the distance to the camera, as the original draws the streamed sectors alone
    /// (docs/research/rendering.md#world); level 0 otherwise.
    bool distanceLevels = false;
    /// Lit by the draw's lights even where its mesh is not (RenderWare's LIGHT flag off, MeshData::lit): the level's
    /// background, whose models carry no light flag and which the original lights with the world's ambient all the
    /// same (docs/research/lighting.md#coneys-implementation).
    bool lit = false;

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

/// Which pass of the debug tools' mesh views a 3D draw is (docs/guides/debug-menu.md#mesh-views). The play mode records
/// a model's shaded draw as it always does and, while a view is on, the views' own draws of the same mesh beside it
/// (RenderEngine::drawScene()); the GL3 renderer draws the views through librw's lighting rig instead and never sees
/// them.
enum class ViewPass : std::uint8_t {
    Shaded, ///< The game's own draw.
    /// A flat, unlit fill: every material in the colour FrameDescription::viewColoursOf() gives it, no texture and no
    /// fog, the alpha test off.
    Flat,
    /// The triangles' edges in the first of FrameDescription::viewColoursOf(), over what is drawn: the fill off, depth
    /// not written and the lines pulled a little towards the camera so they sit on it.
    Wire,
    /// A shaded draw's dual layer alone, recorded after its Flat and Wire passes so it covers them as the GL3
    /// renderer's second atomic does (a human's blood layer).
    DualLayer,
};

/// How a 3D draw's dual layer is drawn over its mesh.
enum class DualPass : std::uint8_t {
    /// A world's or an object's dual layer (docs/research/rendering.md#dual): at the alpha of the mesh's first
    /// material, with no alpha test, in the draw's states.
    Layer,
    /// A human's blood layer (docs/research/rendering.md#characters, GS context 2): in the mesh's colours, blended by
    /// its texture's alpha (source alpha over inverse source alpha, the vertex alpha on) with no alpha test, Z tested
    /// and written; after it the alpha test is GREATEREQUAL again and the vertex alpha off.
    Blood,
};

/// One draw of a scene pass: a mesh of the asset store by id, where it is, what it changes of its materials, its
/// states and its lights; then, when it has one, its dual layer (docs/research/rendering.md#dual), a second mesh on
/// the same frame drawn right after it as `dualPass` says. A skinned mesh (a human) also carries its pose: the
/// skinning matrices, one per skin node, in the character's space, which `transform` (the character's placement) puts
/// into the world. The textures of each mesh's materials, in the order of MeshData::materials, the lights and the
/// pose are ranges of the frame's arrays (FrameDescription::texturesOf(), dualTexturesOf(), lightsOf(), bonesOf()),
/// set when the draw is added.
struct Draw3d {
    MeshId mesh;
    MeshId dualLayer; ///< invalid (0) for none
    DualPass dualPass = DualPass::Layer;
    Transform3d transform;
    Material3d material;
    SceneStates states;
    std::uint32_t firstBone = 0;
    std::uint32_t boneCount = 0; ///< 0: not skinned
    /// A posed mesh's sphere in its own space as the pose moved it (a human's, CharacterMesh::pose()), which the lights
    /// are chosen by in place of the mesh's own (graphics::boundsOf()); none for a mesh drawn as it is.
    std::optional<MeshBounds> bounds;
    std::uint32_t firstLight = 0;
    std::uint32_t lightCount = 0;
    std::uint32_t firstTexture = 0;
    std::uint32_t textureCount = 0;
    std::uint32_t firstDualTexture = 0;
    std::uint32_t dualTextureCount = 0;
    /// Which of the mesh views' passes this is (ViewPass::Shaded for every draw the game makes).
    ViewPass view = ViewPass::Shaded;
    std::uint32_t firstViewColour = 0;
    std::uint32_t viewColourCount = 0; ///< The colours of a Flat or Wire pass (FrameDescription::viewColoursOf()).

    friend bool operator==(const Draw3d&, const Draw3d&) = default;
};

/// The camera a scene pass's draws are seen through, as the world pass places librw's camera
/// (platform/world_renderer.cpp): where it stands and its axes, its view window and its clip and fog planes. A renderer
/// makes its view and projection from it (graphics::viewMatrixOf(), projectionOf()); the GL3 renderer draws through
/// librw's camera, placed the same way, and does not read it.
struct SceneCamera {
    /// Where the camera stands and its axes in RenderWare's world: `right` the screen's right, `up` its up and `at`
    /// the way it looks.
    Transform3d frame;
    float halfWidth = 0.5F;  ///< librw's view window: half the view's width at a distance of 1.
    float halfHeight = 0.5F; ///< Half its height at a distance of 1.
    float nearClip = 0.1F;   ///< The near clip plane's distance.
    float farClip = 100.0F;  ///< The far clip plane's distance: fully fogged there.
    float fogStart = 50.0F;  ///< The distance fog starts at (librw's fog plane).

    friend bool operator==(const SceneCamera&, const SceneCamera&) = default;
};

/// One end of a line a scene pass draws: a point in RenderWare's world and its colour.
struct SceneLineVertex {
    std::array<float, 3> position{};
    Rgba colour = kWhite;

    friend bool operator==(const SceneLineVertex&, const SceneLineVertex&) = default;
};

/// A pass of 3D draws through its camera, in their order: the streamed world's sectors (item 7 of
/// docs/engine/renderer.md#order), the world objects and the parked cars (item 8) and the humans (item 9); the
/// effects are the draws of EffectPass.
struct ScenePass {
    /// The camera its draws are seen through (FrameDescription::setSceneCamera()); none when the frame set none before
    /// them, and a renderer that needs one draws nothing of the pass.
    std::optional<SceneCamera> camera;
    /// The depth buffer cleared to the far plane before its draws, the colour left as it is
    /// (FrameDescription::clearSceneDepth()): the Z-only clear after the level's background, so the world covers it
    /// (LevelObject_RenderBackground, docs/research/level-loading.md#render-order).
    bool clearDepth = false;
    std::vector<Draw3d> draws;
    /// Lines drawn after the pass's draws, two vertices to a line: depth tested, not written, never fogged, never
    /// culled, in their own colours (FrameDescription::addSceneLines()): the Debug draw page's lines.
    std::vector<SceneLineVertex> lines;
};

/// How an effect's colour joins what is below it.
enum class EffectBlend : std::uint8_t {
    Alpha,    ///< Source alpha over inverse source alpha: the sprite's alpha says how much of it shows.
    Additive, ///< Source alpha added to what is below: a light (a glow, a flash, a flame, a spark).
};

/// The states an effect draw sets before it draws; a state left unset stays as the draws before it left it (as the 2D
/// side's Material2d::alphaRef is optional). Which states a draw sets is part of what the draw-stream hashes pin, so
/// each kind of effect sets the ones it always did.
struct EffectStates {
    std::optional<bool> depthTest;
    std::optional<bool> depthWrite;
    std::optional<bool> fog; ///< Distance fog on, in the fog colour and planes the scene pass has.
    std::optional<CullFaces> cull;
    std::optional<bool> vertexAlpha; ///< The vertices' alpha used (RenderWare's VERTEXALPHA), not only the texture's.
    std::optional<EffectBlend> blend;
    std::optional<TextureFilter> filter;
    std::optional<TextureAddress> address;

    friend bool operator==(const EffectStates&, const EffectStates&) = default;
};

/// What an effect draw puts back after it, for the solid draws that follow (the opaque passes expect them): a set flag
/// says its state returns to the scene pass's own value.
struct EffectRestore {
    bool fog = false;        ///< The fog as it was before the draw.
    bool blend = false;      ///< Blended by alpha, whatever the draw added.
    bool depthWrite = false; ///< Depth written.
    bool cull = false;       ///< Back faces dropped.

    friend bool operator==(const EffectRestore&, const EffectRestore&) = default;
};

/// One corner of an effect's triangle or line: a place in RenderWare's world axes, a colour and a texture coordinate
/// (RenderWare's 0-255 colour, as the original's sprite vertices give it: docs/research/gui.md#sprite-colours).
struct EffectVertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    Rgba colour;
    float u = 0.0F;
    float v = 0.0F;

    friend bool operator==(const EffectVertex&, const EffectVertex&) = default;
};

/// What an effect draw's primitives are.
enum class EffectShape : std::uint8_t {
    Triangles, ///< a list of triangles, three corners each (or, with indices, three indices each), textured
    Lines,     ///< a list of lines, two corners each, untextured: the debug tools' lines
};

/// One draw of an effect pass: a sprite batch, a ribbon, a surface or a set of lines in the world, in the scene pass's
/// depth and fog, lit by nothing (the vertex colours are what it shows). `vertexCount` corners of the frame's effect
/// vertices from `firstVertex` on, and when `indexCount` is not 0 that many of its effect indices from `firstIndex`,
/// each naming a corner of the draw's own (counted from `firstVertex`). The ranges are set when the draw is added.
struct EffectDraw {
    EffectShape shape = EffectShape::Triangles;
    /// The texture, by the frame's table (FrameDescription::texture()); kNoFrameTexture for none.
    FrameTextureId texture = kNoFrameTexture;
    EffectStates states;
    EffectRestore restore;
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0; ///< 0: drawn in the order of the corners

    friend bool operator==(const EffectDraw&, const EffectDraw&) = default;
};

/// A pass of effect draws through the camera of the scene before it, in their order: the particles, the glints, the
/// ground fog, the water, the throw arc, the see-through panes, the blob shadows, the health rings, the coronas and
/// the debug tools' lines (item 10 of docs/engine/renderer.md#order). A pass of its own, not part of a ScenePass: an
/// effect is not a mesh of the asset store, and a frame alternates between the draws of the scene and of the effects,
/// each pass holding one run of a kind.
struct EffectPass {
    /// The camera its draws are seen through, as ScenePass::camera.
    std::optional<SceneCamera> camera;
    std::vector<EffectDraw> draws;
};

/// One pass of a frame.
using FramePass = std::variant<Overlay2dPass, ScreenPass, ScenePass, EffectPass>;

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
/// It holds the 2D side, the screen passes, the streamed world's sectors, the world objects, the parked cars and the
/// humans with their poses (scene passes of 3D draws, each a mesh and its textures by AssetStore id), and the effects
/// (effect passes of sprite batches, ribbons, surfaces and lines, each with its vertices and its texture by the frame's
/// table: item 10 of docs/engine/renderer.md#order). The camera still goes to librw directly, and the GL3 renderer
/// draws each part as it is added rather than the whole frame at its end. Draws are given in logical pixels with the
/// window rectangle they are mapped onto, or in window pixels, and are kept in window pixels.
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
    /// Adds the 3D draw `draw` lit by `lights`, its mesh's material `textures`, its dual layer's `dualTextures` and,
    /// for a skinned mesh, its pose `bones` (the ranges in `draw` are set here) to the scene pass the last pass is, or
    /// to a new one; the next 2D draw starts a new overlay pass.
    void addSceneDraw(Draw3d draw, std::span<const SceneLight> lights, std::span<const TextureId> textures,
                      std::span<const TextureId> dualTextures, std::span<const Transform3d> bones = {},
                      std::span<const Rgba> viewColours = {});
    /// Adds `lines`, two vertices to a line, to the scene pass the last pass is, or to a new one, to be drawn after its
    /// 3D draws; an odd vertex left over is dropped, and nothing is added for no whole line.
    void addSceneLines(std::span<const SceneLineVertex> lines);
    /// Adds the effect draw `draw` with its corners `vertices`, its `indices` into them (none: drawn in the order of
    /// the corners) and its `texture` (null: none; it must outlive the frame) to the effect pass the last pass is, or
    /// to a new one seen through the camera set last; the next 2D or 3D draw starts a new pass. The ranges in `draw`
    /// and its texture are set here. Nothing is added for no vertex.
    void addEffectDraw(EffectDraw draw, std::span<const EffectVertex> vertices, std::span<const std::uint16_t> indices,
                       const Texture* texture);
    /// Sees the 3D draws added after it through `camera`: they start a new scene pass with it (the last pass takes
    /// it instead when it is a scene pass with no draw yet), and any later scene pass of the frame keeps it until the
    /// next call.
    void setSceneCamera(const SceneCamera& camera);
    /// Clears the depth buffer before the 3D draws added after it: they start a new scene pass that clears it first,
    /// seen through the camera set last (the last pass clears instead when it is a scene pass with no draw yet).
    void clearSceneDepth();

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
    /// The colours of `draw`, a Flat or Wire pass of this frame: one for each of its mesh's materials in their order
    /// (Flat), or the one every edge is drawn in (Wire); empty for a Shaded draw.
    [[nodiscard]] std::span<const Rgba> viewColoursOf(const Draw3d& draw) const;
    /// The corners of `draw`, an effect draw of this frame.
    [[nodiscard]] std::span<const EffectVertex> effectVerticesOf(const EffectDraw& draw) const;
    /// The indices of `draw`, an effect draw of this frame; empty when it has none.
    [[nodiscard]] std::span<const std::uint16_t> effectIndicesOf(const EffectDraw& draw) const;
    /// The pose of `draw`, a 3D draw of this frame: its skinning matrices in skin node order; empty when it is not
    /// skinned.
    [[nodiscard]] std::span<const Transform3d> bonesOf(const Draw3d& draw) const;
    /// Every skinned 3D draw's pose, in the order the draws were added: Draw3d::firstBone indexes it, so a renderer
    /// can upload the frame's poses at once (docs/engine/gpu-renderer.md#skinning).
    [[nodiscard]] std::span<const Transform3d> bones() const { return m_bones; }

  private:
    // The overlay pass the next draw joins: the last pass when it is one, else a new one.
    Overlay2dPass& overlay();
    // The scene pass the next 3D draw joins, as overlay() for 2D draws.
    ScenePass& scene();
    // The effect pass the next effect draw joins, as scene() for 3D draws.
    EffectPass& effects();

    FrameStart m_start;
    std::vector<FramePass> m_passes;
    std::vector<const Texture*> m_textures; // id - 1 is the place
    std::vector<LogicalQuad> m_quads;
    std::vector<ScreenVertex> m_vertices;
    std::vector<SceneLight> m_lights;
    std::vector<TextureId> m_sceneTextures; // the 3D draws' textures, then each one's dual layer's
    std::vector<Transform3d> m_bones;       // the skinned 3D draws' poses
    std::vector<Rgba> m_viewColours;        // the mesh views' passes' colours
    std::vector<EffectVertex> m_effectVertices;
    std::vector<std::uint16_t> m_effectIndices;
    std::optional<SceneCamera> m_camera;    // the camera setSceneCamera() last set this frame
};

} // namespace coney::graphics
