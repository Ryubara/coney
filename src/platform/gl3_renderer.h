// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "graphics/frame_description.h"
#include "graphics/render_device.h"
#include "graphics/texture_grid.h"
#include "platform/character_skin.h"
#include "platform/lit_atomic.h"
#include "platform/scene_assets.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Camera;
struct Raster;
} // namespace rw

namespace coney::platform {

/// Draws a frame description (graphics::FrameDescription, docs/engine/renderer.md#frame-description) through librw's
/// GL3 device and the draw layer (platform/draw_calls.h), with the draws the 2D side and the screen passes made before
/// the description existed, so the draw-stream hashes do not move. It owns the screen passes' copies of the frame. One
/// difference: the motion blur keeps its frame as a bare raster, with no librw texture around it, and its draw binds
/// the raster and sets the filter as render states; the picture is the same.
///
/// The 3D draws (the streamed world's sectors, item 7; the objects and cars, item 8; the humans, item 9) are drawn as
/// they were drawn before: each mesh's librw atomic, found in the scene assets, with the draw's states, colours and
/// lights, through drawLitAtomic(); a human with his pose (the skinned pipeline's palette) and his blood pass. The
/// effects (item 10) are librw's 3D immediate mode: each draw's vertices and indices with the states it sets, then
/// the ones it puts back, as the sprite, ribbon and surface drawing set them before the description held them.
///
/// While the camera and the level's background still go to librw directly, the RenderEngine calls drawNew() after
/// each part it adds, so the parts draw in their place among the 3D drawing. The 2D, 3D and effect draws are made with
/// a camera or while the draw layer records (the NULL backend records what it would draw); the screen passes, which
/// copy the frame, only with a camera.
class Gl3Renderer {
  public:
    /// What a frame is drawn into: the RenderEngine's camera (null with the NULL and GPU backends: the GPU
    /// backend has no librw camera, so no screen pass runs for it) and its frame's size; and the scene assets the 3D
    /// draws' meshes are found in (null: no 3D draw is made).
    struct Target {
        rw::Camera* camera = nullptr;
        graphics::Extent frameSize;
        const SceneAssets* assets = nullptr;
    };

    Gl3Renderer() = default;
    Gl3Renderer(const Gl3Renderer&) = delete;
    Gl3Renderer& operator=(const Gl3Renderer&) = delete;
    Gl3Renderer(Gl3Renderer&&) = delete;
    Gl3Renderer& operator=(Gl3Renderer&&) = delete;
    /// Drops every copy it holds (releaseAll()); runs before librw stops.
    ~Gl3Renderer();

    /// Starts drawing a new description from its first pass.
    void beginFrame();
    /// Draws what `frame` holds past what this frame has drawn already, into `target`.
    void drawNew(const graphics::FrameDescription& frame, const Target& target);

    /// A new number for a copy kept from frame to frame (the motion blur's last frame); the copy is made when a screen
    /// pass first uses it.
    [[nodiscard]] graphics::ScreenCopyId newScreenCopy();
    /// Drops the copy `id` and forgets its number.
    void releaseScreenCopy(graphics::ScreenCopyId id);

    /// Drops the copies made again for each pass at the view's size (the line blend's, the blur pulse's), when the
    /// window's buffers are remade.
    void releaseViewCopies();
    /// Drops every copy, the kept ones too, and the 3D draws' lights, before librw stops (its raster count must then
    /// be 0).
    void releaseAll();

  private:
    /// A copy kept from frame to frame.
    struct KeptCopy {
        graphics::ScreenCopyId id = 0;
        rw::Raster* raster = nullptr;
        int width = 0;
        int height = 0;
        bool kept = false; // whether it holds the last frame
    };

    // One 2D draw of `frame`.
    void drawOverlay(const graphics::FrameDescription& frame, const graphics::Draw2d& draw, const Target& target);
    // Clears librw's camera's depth buffer for a scene pass that clears it first (graphics::ScenePass::clearDepth).
    static void clearDepth(const graphics::FrameDescription& frame, const Target& target);
    // One effect draw of `frame`: its states set, its corners and indices drawn through librw's 3D immediate mode with
    // its texture's raster, then the states it puts back.
    static void drawEffect(const graphics::FrameDescription& frame, const graphics::EffectDraw& draw,
                           const Target& target);
    // One screen pass.
    void drawScreenPass(const graphics::ScreenPass& pass, const Target& target);
    // One 3D draw of `frame`: a sector's, an object's, a car part's or a human's atomic as its draw function records
    // it, its colours changed as the draw says (Material3d), lit by the draw's lights, then its dual layer (a human's
    // through drawHuman()).
    void drawScene(const graphics::FrameDescription& frame, const graphics::Draw3d& draw, const Target& target);
    // A human's draw, after drawScene() set its states, colours and lights: his body with the draw's pose, then his
    // blood layer `blood` (null: none) as PlayLighting::drawHumanPasses drew it through the rig.
    void drawHuman(const graphics::FrameDescription& frame, const graphics::Draw3d& draw, rw::Atomic& atomic,
                   rw::Atomic* blood);
    /// The blur pulse's drawing (device slot `+0x108`, docs/research/graphics.md#blur-pulse).
    /// @orig 0x00193d80 RwDevice_BlurPass (unknown)
    void drawBlurPulse(const graphics::BlurPulse& blur, const graphics::ScreenRect& viewport, const Target& target);
    // The PS2 video output's line blend over `viewport`.
    void drawLineBlend(const graphics::LineBlend& blend, const graphics::ScreenRect& viewport, const Target& target);
    // The motion blur's stand-in over `viewport`.
    void drawMotionBlur(const graphics::MotionBlurOverlay& blur, const graphics::ScreenRect& viewport,
                        const Target& target);

    // Binds `texture`'s filter and addressing and returns its raster; null for no texture.
    static rw::Raster* bindTexture(const graphics::Texture* texture, graphics::TextureAddress address);
    // Sets the 2D pass's render states with `raster` (null: flat colour); `opaque` replaces what is below (ONE / ZERO)
    // instead of blending by alpha.
    static void set2dStates(rw::Raster* raster, bool opaque);
    /// Draws quads in window pixels with `raster` (null: flat colour) in the 2D states.
    static void drawWindowQuads(rw::Raster* raster, std::span<const graphics::LogicalQuad> quads, const Target& target,
                                bool opaque = false);
    /// Copies the window rectangle (`x`, `y`, `width`, `height`, from the top left) into `raster`, (re)made at that
    /// size when it is missing or another size; false when it cannot be made.
    static bool copyToRaster(rw::Raster*& raster, int x, int y, int width, int height, const Target& target);
    // The kept copy `id`; null when the number is not one this renderer gave out.
    KeptCopy* keptCopy(graphics::ScreenCopyId id);

    std::size_t m_pass = 0;             // the pass drawNew() is in
    std::size_t m_draw = 0;             // the first draw of that pass not drawn yet
    bool m_passStarted = false;         // whether drawNew() has begun that pass (a scene pass clears once)
    rw::Raster* m_lineRaster = nullptr; // the line blend's copy of the view
    rw::Raster* m_blurRaster = nullptr; // the blur pulse's copies: the view, then its half-size image
    rw::Raster* m_halfRaster = nullptr;
    std::vector<KeptCopy> m_kept;
    graphics::ScreenCopyId m_nextCopy = 1;
    std::unique_ptr<LightRig> m_lights;     // the 3D draws' lights, made with the first one
    std::unique_ptr<BonePalette> m_palette; // a human's pose for the skinned pipeline, made with the first
};

} // namespace coney::platform
