// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "graphics/frame_description.h"
#include "graphics/frame_stats.h"
#include "graphics/render_device.h"
#include "graphics/screen.h"
#include "graphics/texture_grid.h"
#include "platform/draw_stream.h"
#include "platform/gl3_renderer.h"
#include "platform/present_mode.h"
#include "platform/window.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Camera;
struct Raster;
struct Texture;
} // namespace rw

namespace coney::platform {

class GpuPresenter;

/// Which renderer a RenderEngine runs.
enum class RenderBackend : std::uint8_t {
    OpenGl, ///< librw's GL3 device: a window with an OpenGL 3.3 core context (2.1 or GLES as fallbacks), drawn into.
    Null,   ///< librw's NULL device: no window, no GPU, nothing drawn; for CI, tests and console tools.
    /// SDL3's GPU API (docs/engine/renderer.md#migration): a window Coney makes, SDL's GPU device and swapchain, and
    /// librw on its NULL device for data only, as for Null. Frames are cleared and presented; nothing else is drawn
    /// yet.
    Gpu,
};

/// A frame read back from the screen by RenderEngine::requestCapture().
struct CapturedFrame {
    graphics::Extent size;
    graphics::FrameStats stats; ///< Against the colour the frame was cleared to.
    std::string path;           ///< Where the PNG was written.
};

/// Whether windows SDL shows from now on take the keyboard focus (SDL's ACTIVATE_WHEN_SHOWN and ACTIVATE_WHEN_RAISED
/// hints). RenderEngine::start sets it from WindowDesc::activate before librw makes its window; it needs no display.
void setWindowActivation(bool activate);

/// librw's engine and, with the OpenGL backend, the window it draws into, started and stopped in the order librw
/// requires (init, open, start; then stop, close, term).
///
/// librw is built once, for its GL3 platform (cmake/deps.cmake). The NULL backend is chosen at run time by giving the
/// engine librw's NULL device in place of the GL3 one while it opens, so headless runs touch neither SDL's video nor
/// OpenGL; librw's file loaders and its PS2 raster code work the same on both. Only rasters for the current platform
/// (GL3) need the OpenGL backend. Every librw raster also carries GL3's extension, whose destructor deletes a GL
/// texture, so while a NULL engine runs it gives librw's OpenGL loader a glDeleteTextures that does nothing.
///
/// With the OpenGL backend, librw's GL3 device creates the window and the context itself and destroys them when the
/// engine stops: the engine owns the window, and window() hands out a non-owning view for the event loop.
///
/// The GPU backend (docs/engine/renderer.md#migration) opens librw exactly as the NULL backend does, for data only, and
/// makes the window itself through SDL3, with SDL's GPU device and swapchain (GpuPresenter, gpu_presenter.h); from
/// there on librw's GL3 device is used only by the OpenGL backend. Its frames are cleared to the frame's colour and
/// presented, and nothing else is drawn in them yet.
///
/// librw keeps its engine in global state, so only one RenderEngine may run at a time; a second start() while one
/// runs fails at librw's init step. A RenderEngine does not move (librw keeps pointers into it), hence the unique_ptr.
class RenderEngine final : public graphics::RenderDevice {
  public:
    /// Brings librw up with `backend`; `desc` sizes and names the window (ignored by the NULL backend). Fails with
    /// ErrorCode::PlatformFailure naming the step that refused, with SDL's error text where there is one: SDL's video
    /// cannot start or sees no display, librw refuses a step, no OpenGL context can be created, or (GPU) no GPU device
    /// opens, the message then saying `no GPU device`. Everything that had started is stopped again before it returns,
    /// SDL's video included, so a failed GPU start can be followed by an OpenGL one.
    [[nodiscard]] static std::expected<std::unique_ptr<RenderEngine>, Error> start(RenderBackend backend,
                                                                                   const WindowDesc& desc);

    ~RenderEngine() override;

    /// The backend it runs.
    [[nodiscard]] RenderBackend backend() const { return m_backend; }
    /// Whether the game's picture is drawn: true for OpenGL only. Textures can be converted for drawing only then; the
    /// GPU backend shows frames but draws nothing in them yet.
    [[nodiscard]] bool drawsPixels() const { return m_backend == RenderBackend::OpenGl; }
    /// Whether librw runs its NULL device, reading RenderWare data and drawing nothing: the NULL and GPU backends.
    [[nodiscard]] bool librwDataOnly() const { return m_backend != RenderBackend::OpenGl; }
    /// The GPU backend's SDL driver (`vulkan`, `direct3d12`, `metal`); empty for the others.
    [[nodiscard]] std::string_view gpuDriver() const;
    /// The present mode the GPU backend's swapchain shows frames with (setVsync(), setFpsCap(), applied at the next
    /// present()); Vsync for the others.
    [[nodiscard]] PresentMode presentMode() const;
    /// The present modes the GPU backend's window offers besides Vsync; none for the others.
    [[nodiscard]] PresentModes offeredPresentModes() const;
    /// The frames present() has ended so far, on any backend.
    [[nodiscard]] std::uint64_t framesPresented() const { return m_presented; }
    /// Of framesPresented(), those shown on the screen: every one for OpenGL, those the GPU backend's swapchain took
    /// (a hidden or minimised window may take none), none for NULL.
    [[nodiscard]] std::uint64_t framesShown() const {
        return m_backend == RenderBackend::OpenGl ? m_presented : m_shown;
    }
    /// The window, for the OpenGL and GPU backends; nothing for NULL.
    [[nodiscard]] std::optional<Window> window() const;
    /// The SDL_Window* and its OpenGL context (an SDL_GLContext), opaque here, for the developer overlay's backends;
    /// nulls with the NULL backend, and the window with no context with the GPU backend (which has no overlay yet).
    [[nodiscard]] std::pair<void*, void*> sdlWindowAndContext() const;
    /// The size of the area frames are drawn into, in pixels: the window's client area, or the requested size for
    /// the NULL backend.
    [[nodiscard]] graphics::Extent frameSize() const { return m_frameSize; }

    /// The shape the 3D view is drawn at across the frame, width over height: the display's 4:3 when the frame is the
    /// logical screen (WindowDesc::logicalFrame), else the frame's own, so its pixels stay square.
    [[nodiscard]] float viewAspect() const;
    /// Whether the picture is shown as the original's 16:9 mode: the 3D view is wider than 4:3
    /// (graphics::isWideView()). The one place Coney decides it; the scenes' intro cards and the screen effects
    /// drawn over the whole picture (the room smoke's overlay camera) follow it.
    [[nodiscard]] bool widescreen() const { return graphics::isWideView(viewAspect()); }
    /// Where the 3D view is in the window this frame, in window pixels: the whole frame. It is the logical screen's
    /// place (logicalViewport()) in a 4:3 window or with WindowDesc::logicalFrame, and wider or taller than it in a
    /// window of another shape. Passes over the whole picture (the line blend, the blurs, the washes) cover this.
    [[nodiscard]] graphics::ScreenRect viewRect() const {
        return graphics::ScreenRect{0, 0, m_frameSize.width, m_frameSize.height};
    }

    /// Starts a frame: the window cleared to black and the logical screen (graphics::fitLogicalScreen()) filled with
    /// `clear`. Follows the window's size: when the window has been resized, the frame buffers are made again at the
    /// new size first.
    void beginFrame(graphics::Rgba clear) override;

    /// Starts a frame with the whole window cleared to `clear`, for Coney's tools that lay out in window pixels (the
    /// texture viewer). Otherwise as beginFrame().
    void beginWindowFrame(graphics::Rgba clear);

    /// The camera frames are cleared and drawn through, for 3D drawing (platform/world_renderer.h); null with the NULL
    /// backend. It may be made again when the window is resized, so do not keep it across frames.
    [[nodiscard]] rw::Camera* camera() const { return m_camera; }

    /// Where the logical screen is in the window this frame, in window pixels.
    [[nodiscard]] graphics::ScreenRect logicalViewport() const { return m_viewport; }

    /// Draws `quads`, given in logical pixels, mapped onto the logical screen's place in the window. `texture` must
    /// be a SheetTexture (src/platform/sprite_sheets.h) converted for drawing, a MovieTexture (movie_screen.h), or null
    /// for flat colour. The texture's own filter mode is used, with clamped addressing. Only between beginFrame() and
    /// present() (checked by CONEY_ASSERT); draws nothing with the NULL backend.
    void drawQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads) override;
    /// drawQuads() with wrapped addressing, for a texture whose coordinates run past its edges and repeat it (the
    /// room-smoke overlay, docs/research/graphics.md#room-smoke).
    void drawWrappedQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads);
    /// drawQuads() with the logical screen's 640 x 448 stretched over the whole 3D view (viewRect()) rather than
    /// fitted in it: the screen washes (graphics::ScreenFade::drawWash()). Same as drawQuads() in a 4:3 window.
    void drawViewQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads) override;
    /// drawViewQuads() with wrapped addressing: the room smoke, which in the 16:9 mode the original draws across its
    /// whole 16:9 picture (docs/research/graphics.md#room-smoke).
    void drawWrappedViewQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads);
    /// Draws `vertices`, given in logical pixels, as triangles mapped onto the logical screen's place in the window,
    /// with `texture` as drawQuads() takes it, wrapped or clamped and alpha tested as `states` says (the test is put
    /// back after). Only between beginFrame() and present(); draws nothing with the NULL backend.
    void drawTriangles(const graphics::Texture* texture, std::span<const graphics::LogicalVertex> vertices,
                       const graphics::TriangleStates& states) override;

    /// Draws flat-coloured `quads` given in window pixels (from the top left), blended by their alpha, in the 2D
    /// states. Only between beginFrame() and present() (checked by CONEY_ASSERT); draws nothing with the NULL backend.
    void drawWindowRects(std::span<const graphics::LogicalQuad> quads);

    /// Adds a layer present() draws over this frame only, before the present overlay: a mode's 2D over its own 3D pass
    /// when something else presents the frame (the play mode's HUD, then a cinematic's letterbox and fades, then what
    /// goes over them, such as the pause menu). The layers draw in the order they were added; present() clears them.
    void addFrameOverlay(std::function<void(RenderEngine&)> overlay) {
        if (overlay) {
            m_frameOverlays.push_back(std::move(overlay));
        }
    }

    /// Draws `texture` stretched over `rect` (screen pixels, from the top left), blended by its alpha. Only between
    /// beginFrame() and present(), and only with the OpenGL backend (both checked by CONEY_ASSERT); the texture's
    /// raster must have been converted for the current platform.
    void drawTexture(rw::Texture* texture, graphics::ScreenRect rect);

    /// Ends the frame and shows it, waiting for the vertical blank while vsync is on (setVsync()). Takes the capture
    /// requested for this frame, if any, just before showing it.
    void present() override;

    /// Chooses whether present() waits for the vertical blank (swap interval 1, the default) or shows the frame at
    /// once (swap interval 0, which may tear). Only the OpenGL backend has anything to wait for.
    void setVsync(bool on) { m_vsync = on; }
    /// Whether present() waits for the vertical blank.
    [[nodiscard]] bool vsync() const { return m_vsync; }
    /// Tells the renderer the frame pacer's cap (`--fps-cap`, 0 for none), which with vsync chooses the GPU backend's
    /// present mode (choosePresentMode()); OpenGL has only its swap interval and ignores it.
    void setFpsCap(std::uint32_t cap) { m_fpsCap = cap; }
    /// Chooses whether present() softens the frame as the PS2's video output does (off by default): each line becomes
    /// the mean of itself and the line one of the original's 448 below it, after the modes' 2D layers and before the
    /// debug menus (docs/research/rendering.md#output). Off shows the frame as drawn, sharper than the original.
    void setLineBlend(bool on) { m_lineBlend = on; }
    /// Whether present() blends neighbouring lines.
    [[nodiscard]] bool lineBlend() const { return m_lineBlend; }
    /// Replaces the whole view (viewRect()) with a blurred copy of itself, the blur pulse's drawing (device slot
    /// `+0x108`, docs/research/graphics.md#blur-pulse): the screen drawn at half size, that image drawn `passes` times
    /// onto itself with one edge moved in by half of (`offsetU`, `offsetV`) of the original's 512 x 256 blur texture,
    /// the side flipping each pass, then stretched back over the screen, opaque. 0 passes still leaves the half-size
    /// copy. Only between beginFrame() and present(); draws nothing with the NULL backend.
    /// Recorded as a screen pass (graphics::BlurPulse) and drawn by Gl3Renderer::drawBlurPulse(), the port of the
    /// original's blur pass.
    void blurScreen(int passes, float offsetU, float offsetV);

    /// A new copy of the view for a screen pass to keep from frame to frame (MotionBlurPass's last frame).
    [[nodiscard]] graphics::ScreenCopyId newScreenCopy() { return m_gl3.newScreenCopy(); }
    /// Gives the copy `id` back; before the engine stops.
    void releaseScreenCopy(graphics::ScreenCopyId id) { m_gl3.releaseScreenCopy(id); }
    /// Adds the motion blur's screen pass over the whole view (viewRect()): the copy `copy` laid over it in `colour`,
    /// then the view kept in it (graphics::MotionBlurOverlay). Only between beginFrame() and present().
    void addMotionBlur(graphics::ScreenCopyId copy, graphics::Rgba colour);
    /// Sets what present() draws over every frame just before it ends it: the debug menus' overlay
    /// (src/gui/debug_menu_view.h), which so draws over whatever mode runs. Empty for nothing. It is called with this
    /// device, still inside the frame, so it may draw quads.
    void setPresentOverlay(std::function<void(graphics::RenderDevice&)> overlay) {
        m_presentOverlay = std::move(overlay);
    }

    /// Hands every frame present() shows to `tap` (empty for none), read back from the frame buffer: the logical
    /// screen's rectangle (where WindowDesc::logicalFrame puts it), RGBA from the top left with alpha 255, after the
    /// modes' 2D layers and the line blend and before the present overlay (the debug menus), so no debug overlay is
    /// ever in it. For `--record` (ClipRecorder), Coney's own debug tooling; nothing with the NULL backend.
    void setFrameTap(std::function<void(std::span<const std::uint8_t>, graphics::Extent)> tap) {
        m_frameTap = std::move(tap);
    }

    /// Asks for the frame shown by the `frameIndex`-th present() (counting from 0) to be read back, summarised and
    /// saved as a PNG at `path`. The result is in capture() once that frame has been presented.
    void requestCapture(std::uint64_t frameIndex, std::string path);
    /// The result of the requested capture: nothing if no frame has been captured yet, an Error when the frame could
    /// not be read (the NULL backend draws nothing) or the PNG could not be written.
    [[nodiscard]] const std::optional<std::expected<CapturedFrame, Error>>& capture() const { return m_capture; }

    /// Asks for what the `frameIndex`-th frame (counting presents from 0) draws to be recorded through the draw layer
    /// (platform/draw_calls.h), from its beginFrame() to its present(), for `--draw-hash`
    /// (docs/engine/renderer.md#comparing). With the NULL backend the frame's 2D drawing is recorded too, though
    /// nothing is drawn. Turns on the raster digests (prepareDrawStreams()).
    void requestDrawStream(std::uint64_t frameIndex);
    /// Turns on the draw layer's raster digests, so the textures a recorded frame draws with are known by their
    /// pixels: call it before they load when the frame is chosen later (`--play-level` counts from its first frame of
    /// play).
    void prepareDrawStreams();
    /// The stream of the requested frame, once it has been presented; nothing before.
    [[nodiscard]] const std::optional<DrawStream>& drawStream() const { return m_drawStream; }

    /// What the current frame, or the last one presented until the next begins, has drawn of its 2D side and screen
    /// passes (docs/engine/renderer.md#frame-description). Built with all three backends, so headless tests read it.
    [[nodiscard]] const graphics::FrameDescription& frame() const { return m_frame; }

  private:
    // Only start() makes one, which then fills it in as librw comes up.
    RenderEngine(RenderBackend backend, const WindowDesc& desc);

    /// Clears the whole window to `clear` and starts the camera's update and the frame description as `start` says:
    /// the part both beginFrame()s share.
    void startFrame(graphics::Rgba clear, const graphics::FrameStart& start);
    /// Has the GL3 renderer draw what the frame description gained since it last drew.
    void drawDescribed();
    // drawQuads() and its kin: the texture's filter, `wrap` or clamped addressing, the quads mapped from logical pixels
    // onto the window rectangle `onto` (the logical screen's place, or the whole view).
    void drawTexturedQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads, bool wrap,
                           const graphics::ScreenRect& onto);

    /// Where the logical screen goes in a frame of `size` (WindowDesc::logicalFrame).
    [[nodiscard]] graphics::ScreenRect logicalScreenIn(graphics::Extent size) const;
    /// Places the GPU backend's window on its display, shows it unless `desc` keeps it hidden, and takes its size.
    void showGpuWindow(const WindowDesc& desc);
    /// Makes the camera and its frame and depth buffers at m_frameSize (OpenGL only).
    void createCamera();
    /// Destroys the camera and its buffers, if any.
    void destroyCamera() noexcept;
    /// Reads the logical screen's rectangle back and hands it to the frame tap (setFrameTap()).
    void tapFrame();
    /// Reads the back buffer, summarises it and writes it as a PNG, for requestCapture().
    [[nodiscard]] std::expected<CapturedFrame, Error> captureBackBuffer(const std::string& path) const;
    /// Stops librw and, with the OpenGL backend, SDL, if this object started them.
    void shutDown() noexcept;

    RenderBackend m_backend;
    std::string m_title;             // librw keeps a pointer to it until the window is made
    void* m_sdlWindow = nullptr;     // SDL_Window*, written by librw's GL3 device when it creates the window
    rw::Camera* m_camera = nullptr;  // what frames are cleared and drawn through (OpenGL only)
    graphics::Extent m_frameSize;    // size of the camera's buffers, or the requested size for NULL
    graphics::Rgba m_clearColour;    // of the current frame, for the capture's statistics
    graphics::ScreenRect m_viewport; // where the logical screen is in the window (graphics::fitLogicalScreen())
    std::uint32_t m_fpsCap = 0;      // the frame pacer's cap, for the GPU backend's present mode
    bool m_inFrame = false;          // between beginFrame() and present()
    bool m_librwStarted = false;     // librw has reached Engine::start and must be stopped
    bool m_sdlStarted = false;       // this object holds a reference to SDL's video subsystem
    bool m_glStubbed = false;        // the NULL backend's stand-in for glDeleteTextures is installed
    bool m_vsync = true;             // present() waits for the vertical blank
    bool m_logicalFrame = false;     // the logical screen fills the frame (WindowDesc::logicalFrame)
    bool m_textureLod = false;       // Coney's distance mip levels are in librw's pipeline (texture_lod.h)
    bool m_lineBlend = false;        // present() blends neighbouring lines (setLineBlend())

    std::unique_ptr<GpuPresenter> m_gpu; // the GPU backend's device, window and swapchain
    std::uint64_t m_presented = 0;       // frames presented so far
    std::uint64_t m_shown = 0;           // of those, the GPU backend's that reached the swapchain
    std::optional<std::uint64_t> m_captureFrame;
    std::string m_capturePath;
    std::optional<std::expected<CapturedFrame, Error>> m_capture;
    std::optional<std::uint64_t> m_drawStreamFrame; // requestDrawStream()
    std::optional<DrawStream> m_drawStream;
    graphics::FrameDescription m_frame; // the frame's 2D side and screen passes (frame())
    Gl3Renderer m_gl3;                  // draws m_frame through librw's GL3 device
    std::function<void(std::span<const std::uint8_t>, graphics::Extent)> m_frameTap; // setFrameTap()
    std::vector<std::uint8_t> m_tapRows;                                             // the tap's read-back, bottom up
    std::vector<std::uint8_t> m_tapFrame;                                            // the same, top down
    std::function<void(graphics::RenderDevice&)> m_presentOverlay;                   // drawn at the end of every frame
    std::vector<std::function<void(RenderEngine&)>> m_frameOverlays; // drawn at the end of this frame only, in order
};

} // namespace coney::platform
