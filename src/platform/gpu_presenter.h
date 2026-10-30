// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "core/error.h"
#include "graphics/render_device.h"
#include "platform/present_mode.h"

// SDL's types, declared rather than included, so the engine's header stays free of SDL.
struct SDL_Window;
struct SDL_GPUDevice;

namespace coney::platform {

/// SDL3's GPU device and a window claimed for it: the GPU renderer's start (docs/engine/renderer.md#migration, item 3).
/// It clears each frame and presents it; drawing comes with the frame description and the shaders (items 4 and 11).
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
    /// claims it for the device and sets the present mode. Fails with ErrorCode::PlatformFailure, the message starting
    /// `no GPU device` when SDL makes none, with SDL's error text; everything made is destroyed again first.
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

    /// Clears the window's next swapchain image to `colour` and presents it, waiting for one to be free as the present
    /// mode says. Returns whether an image was shown: none is while the window is minimised or hidden, which is not an
    /// error.
    bool clearAndPresent(graphics::Rgba colour);

  private:
    GpuPresenter() = default;

    SDL_GPUDevice* m_device = nullptr;
    SDL_Window* m_window = nullptr;
    bool m_claimed = false; // the window is claimed for the device and must be released from it
    std::string m_driver;
    PresentModes m_offered;
    PresentMode m_mode = PresentMode::Vsync;
};

} // namespace coney::platform
