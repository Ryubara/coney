// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/error.h"
#include "graphics/frame_stats.h"
#include "graphics/render_device.h"
#include "graphics/screen.h"
#include "graphics/texture_grid.h"
#include "platform/window.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Camera;
struct Raster;
struct Texture;
} // namespace rw

namespace coney::platform {

/// Which renderer a RenderEngine runs.
enum class RenderBackend : std::uint8_t {
    OpenGl, ///< librw's GL3 device: a window with an OpenGL 3.3 core context (2.1 or GLES as fallbacks), drawn into.
    Null,   ///< librw's NULL device: no window, no GPU, nothing drawn; for CI, tests and console tools.
};

/// A frame read back from the screen by RenderEngine::requestCapture().
struct CapturedFrame {
    graphics::Extent size;
    graphics::FrameStats stats; ///< Against the colour the frame was cleared to.
    std::string path;           ///< Where the PNG was written.
};

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
/// librw keeps its engine in global state, so only one RenderEngine may run at a time; a second start() while one
/// runs fails at librw's init step. A RenderEngine does not move (librw keeps pointers into it), hence the unique_ptr.
class RenderEngine final : public graphics::RenderDevice {
  public:
    /// Brings librw up with `backend`; `desc` sizes and names the window (ignored by the NULL backend). Fails with
    /// ErrorCode::PlatformFailure naming the step that refused, with SDL's error text where there is one: SDL's video
    /// cannot start or sees no display, librw refuses a step, or no OpenGL context can be created. Everything that
    /// had started is stopped again before it returns.
    [[nodiscard]] static std::expected<std::unique_ptr<RenderEngine>, Error> start(RenderBackend backend,
                                                                                   const WindowDesc& desc);

    ~RenderEngine() override;

    /// The backend it runs.
    [[nodiscard]] RenderBackend backend() const { return m_backend; }
    /// Whether frames reach a screen: true for OpenGL. Textures can be converted for drawing only then.
    [[nodiscard]] bool drawsPixels() const { return m_backend == RenderBackend::OpenGl; }
    /// The window, for the OpenGL backend; nothing for NULL.
    [[nodiscard]] std::optional<Window> window() const;
    /// The SDL_Window* and its OpenGL context (an SDL_GLContext), opaque here, for the developer overlay's backends;
    /// nulls with the NULL backend.
    [[nodiscard]] std::pair<void*, void*> sdlWindowAndContext() const;
    /// The size of the area frames are drawn into, in pixels: the window's client area, or the requested size for
    /// the NULL backend.
    [[nodiscard]] graphics::Extent frameSize() const { return m_frameSize; }

    /// The shape the 3D view is drawn at across the frame, width over height: the display's 4:3 when the frame is the
    /// logical screen (WindowDesc::logicalFrame), else the frame's own, so its pixels stay square.
    [[nodiscard]] float viewAspect() const;

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
    /// Sets what present() draws over every frame just before it ends it: the debug menus' overlay
    /// (src/gui/debug_menu_view.h), which so draws over whatever mode runs. Empty for nothing. It is called with this
    /// device, still inside the frame, so it may draw quads.
    void setPresentOverlay(std::function<void(graphics::RenderDevice&)> overlay) {
        m_presentOverlay = std::move(overlay);
    }

    /// Asks for the frame shown by the `frameIndex`-th present() (counting from 0) to be read back, summarised and
    /// saved as a PNG at `path`. The result is in capture() once that frame has been presented.
    void requestCapture(std::uint64_t frameIndex, std::string path);
    /// The result of the requested capture: nothing if no frame has been captured yet, an Error when the frame could
    /// not be read (the NULL backend draws nothing) or the PNG could not be written.
    [[nodiscard]] const std::optional<std::expected<CapturedFrame, Error>>& capture() const { return m_capture; }

  private:
    // Only start() makes one, which then fills it in as librw comes up.
    RenderEngine(RenderBackend backend, const WindowDesc& desc);

    /// Clears the whole window to `clear` and starts the camera's update: the part both beginFrame()s share.
    void startFrame(graphics::Rgba clear);
    /// Draws quads already in window pixels with `raster` (null: flat colour) in the 2D states.
    void drawWindowQuads(rw::Raster* raster, std::span<const graphics::LogicalQuad> quads);
    // drawQuads() and drawWrappedQuads(): the texture's filter, `wrap` or clamped addressing, the quads mapped from
    // logical pixels to the window.
    void drawTexturedQuads(const graphics::Texture* texture, std::span<const graphics::LogicalQuad> quads, bool wrap);

    /// Where the logical screen goes in a frame of `size` (WindowDesc::logicalFrame).
    [[nodiscard]] graphics::ScreenRect logicalScreenIn(graphics::Extent size) const;
    /// Makes the camera and its frame and depth buffers at m_frameSize (OpenGL only).
    void createCamera();
    /// Destroys the camera and its buffers, if any.
    void destroyCamera() noexcept;
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
    bool m_inFrame = false;          // between beginFrame() and present()
    bool m_librwStarted = false;     // librw has reached Engine::start and must be stopped
    bool m_sdlStarted = false;       // this object holds a reference to SDL's video subsystem
    bool m_glStubbed = false;        // the NULL backend's stand-in for glDeleteTextures is installed
    bool m_vsync = true;             // present() waits for the vertical blank
    bool m_logicalFrame = false;     // the logical screen fills the frame (WindowDesc::logicalFrame)

    std::uint64_t m_presented = 0; // frames presented so far
    std::optional<std::uint64_t> m_captureFrame;
    std::string m_capturePath;
    std::optional<std::expected<CapturedFrame, Error>> m_capture;
    std::function<void(graphics::RenderDevice&)> m_presentOverlay;   // drawn at the end of every frame
    std::vector<std::function<void(RenderEngine&)>> m_frameOverlays; // drawn at the end of this frame only, in order
};

} // namespace coney::platform
