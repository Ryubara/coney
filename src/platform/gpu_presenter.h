// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "graphics/asset_store.h"
#include "graphics/frame_description.h"
#include "graphics/render_device.h"
#include "graphics/texture_grid.h"
#include "platform/gpu_overlay.h"
#include "platform/present_mode.h"

// SDL's types, declared rather than included, so the engine's header stays free of SDL.
struct SDL_Window;
struct SDL_GPUDevice;

namespace coney::platform {

class GpuRenderer;

/// SDL3's GPU device and a window claimed for it: the GPU renderer's start (docs/engine/renderer.md#migration, item 3),
/// which draws each frame's 2D passes through its GpuRenderer (gpu_renderer.h, item 11), copies them to the window and
/// presents them (docs/engine/gpu-renderer.md#drawing).
///
/// The window is Coney's own, made here through SDL3 (not by librw's GL3 device), so the device and swapchain are
/// claimed on the thread that made it, the main thread, as SDL requires. The device is made first, before the window,
/// so a machine with no Vulkan, Direct3D 12 or Metal fails before anything shows. SDL's video must be running for the
/// object's whole life (RenderEngine holds a reference to it).
///
/// Coney's own: the original drew through the PS2's GS; nothing here ports it.
class GpuPresenter {
  public:
    /// What the window and its swapchain start with.
    struct Desc {
        std::string_view title;   ///< The title bar text; copied by SDL.
        int width = 0;            ///< Client area width in pixels.
        int height = 0;           ///< Client area height in pixels.
        bool vsync = true;        ///< `--vsync`, which with the cap chooses the present mode.
        std::uint32_t fpsCap = 0; ///< `--fps-cap`, 0 for none.
    };

    /// Makes SDL's GPU device (any driver that takes the shader formats SDL_shadercross makes: SPIR-V, DXIL, MSL; the
    /// `SDL_GPU_DRIVER` environment variable picks one), then the window, hidden (the caller places and shows it),
    /// claims it for the device, sets the present mode and makes the 2D drawing (GpuRenderer); a drawing SDL refuses
    /// is no failure: the frames are then only cleared, and drawingProblem() says why. Fails with
    /// ErrorCode::PlatformFailure, the message starting `no GPU device` when SDL makes none, with SDL's error text;
    /// everything made is destroyed again first.
    [[nodiscard]] static std::expected<std::unique_ptr<GpuPresenter>, Error> start(const Desc& desc);

    ~GpuPresenter();
    GpuPresenter(const GpuPresenter&) = delete;
    GpuPresenter& operator=(const GpuPresenter&) = delete;
    GpuPresenter(GpuPresenter&&) = delete;
    GpuPresenter& operator=(GpuPresenter&&) = delete;

    /// The window, as an opaque SDL_Window* for Window and the window placement in render_engine.cpp.
    [[nodiscard]] SDL_Window* window() const { return m_window; }
    /// SDL's name of the driver the device runs: `vulkan`, `direct3d12` or `metal`.
    [[nodiscard]] const std::string& driver() const { return m_driver; }
    /// The present modes the window offers besides Vsync.
    [[nodiscard]] PresentModes offered() const { return m_offered; }
    /// The present mode frames are shown with.
    [[nodiscard]] PresentMode mode() const { return m_mode; }

    /// Shows frames with the mode choosePresentMode() picks for `vsync` and `fpsCap` from the ones the window offers;
    /// does nothing when it is the mode in use. Call it between frames.
    void choosePresentMode(bool vsync, std::uint32_t fpsCap);

    /// Where something that draws over the frame (the developer overlay) gets the device and the target's format;
    /// nothing when the 2D drawing could not be made, since there is then no frame to draw over (drawingProblem()).
    [[nodiscard]] std::optional<GpuOverlayTarget> overlayTarget() const;

    /// Adds a drawing present() runs over each frame's picture after its 2D passes, before the copy to the window,
    /// after the ones added before it; returns the handle to remove it by (0, and nothing added, for an empty one). The
    /// caller removes it before whatever it calls goes away.
    GpuOverlayHandle addOverlay(GpuOverlayDraw overlay);
    /// Removes the drawing `handle` named; the others stay. Does nothing for a handle already removed or 0.
    void removeOverlay(GpuOverlayHandle handle);

    /// What present() did with a frame.
    struct Presented {
        bool shown = false; ///< the swapchain took an image: none while the window is minimised or hidden
        /// The frame read back, RGBA with rows top first at the frame's size, when present() was asked for it; an Error
        /// when it could not be.
        std::optional<std::expected<std::vector<std::uint8_t>, Error>> capture;
        /// The frame read back before the overlays drew over it, RGBA with rows top first at the frame's size, when
        /// present() was asked for it and could read it: a clip's frame.
        std::optional<std::vector<std::uint8_t>> game;
    };

    /// Draws `frame`'s 2D passes at `size` (GpuRenderer), copies the picture onto the window's next swapchain image and
    /// presents it, waiting for an image to be free as the present mode says; with `capture`, also reads the picture
    /// back, waiting for the GPU; with `game`, also reads back the picture as it is before the overlays draw over it.
    /// Without the 2D drawing, the window is cleared to the frame's colour and nothing can be read back.
    Presented present(const graphics::FrameDescription& frame, graphics::Extent size, bool capture,
                      const graphics::AssetStore* store = nullptr, bool game = false);

    /// Why the frames are only cleared (the 2D drawing could not be made: a build without the shaders, or SDL refused
    /// it); empty while it draws.
    [[nodiscard]] const std::string& drawingProblem() const { return m_drawingProblem; }
    /// Why the scene passes are not drawn while the 2D passes are (GpuRenderer::sceneProblem()); empty when they are
    /// drawn, or when there is no drawing at all (drawingProblem()).
    [[nodiscard]] std::string sceneProblem() const;

    /// Lets go of what the 2D drawing keeps of the motion blur's copy `id` (GpuRenderer::releaseScreenCopy()).
    void releaseScreenCopy(graphics::ScreenCopyId id);

    /// Clears the window's next swapchain image to `colour` and presents it, waiting for one to be free as the present
    /// mode says. Returns whether an image was shown: none is while the window is minimised or hidden, which is not an
    /// error.
    bool clearAndPresent(graphics::Rgba colour);

  private:
    GpuPresenter();

    SDL_GPUDevice* m_device = nullptr;
    SDL_Window* m_window = nullptr;
    bool m_claimed = false; // the window is claimed for the device and must be released from it
    std::string m_driver;
    PresentModes m_offered;
    PresentMode m_mode = PresentMode::Vsync;
    std::unique_ptr<GpuRenderer> m_renderer; // the 2D drawing; null when it could not be made
    std::string m_drawingProblem;            // why m_renderer is null
    std::vector<std::pair<GpuOverlayHandle, GpuOverlayDraw>> m_overlays; // drawn over each frame's picture, in order
    GpuOverlayHandle m_lastOverlay = 0;                                  // the last handle given
};

} // namespace coney::platform
