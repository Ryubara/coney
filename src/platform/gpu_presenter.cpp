// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_presenter.h"

#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "platform/gpu_renderer.h"

namespace coney::platform {

namespace {

// The shader formats SDL_shadercross compiles Coney's HLSL into (docs/engine/renderer.md#migration, item 4): a device
// is wanted only on a driver that takes one of them, so the shaders that come later always have a form it loads.
constexpr SDL_GPUShaderFormat kShaderFormats =
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;

// SDL's present mode for ours.
SDL_GPUPresentMode toSdl(PresentMode mode) {
    switch (mode) {
    case PresentMode::Mailbox:
        return SDL_GPU_PRESENTMODE_MAILBOX;
    case PresentMode::Immediate:
        return SDL_GPU_PRESENTMODE_IMMEDIATE;
    case PresentMode::Vsync:
        break;
    }
    return SDL_GPU_PRESENTMODE_VSYNC;
}

// SDL's error text after `what`, or `what` alone when SDL says nothing.
std::string withSdlError(std::string what) {
    const char* sdl = SDL_GetError();
    if (sdl != nullptr && *sdl != '\0') {
        what += std::string(": ") + sdl;
    }
    return what;
}

// The failure of a step of the start.
std::unexpected<Error> failure(std::string what) {
    return std::unexpected(Error{ErrorCode::PlatformFailure, withSdlError(std::move(what))});
}

} // namespace

GpuPresenter::GpuPresenter() = default;

std::expected<std::unique_ptr<GpuPresenter>, Error> GpuPresenter::start(const Desc& desc) {
    // The constructor is private, so make_unique cannot reach it. From here on the destructor undoes whatever was made.
    std::unique_ptr<GpuPresenter> presenter(new GpuPresenter());
    // No debug layer: it is not installed on a player's machine, and SDL's validation is for its own development.
    presenter->m_device = SDL_CreateGPUDevice(kShaderFormats, false, nullptr);
    if (presenter->m_device == nullptr) {
        return failure("no GPU device opened (SDL's GPU API needs Vulkan, Direct3D 12 or Metal)");
    }
    const char* driver = SDL_GetGPUDeviceDriver(presenter->m_device);
    presenter->m_driver = driver != nullptr ? driver : "";

    // Hidden until the caller has placed it on its display (render_engine.cpp), resizable as librw's GL3 window is.
    const std::string title(desc.title);
    presenter->m_window =
        SDL_CreateWindow(title.c_str(), desc.width, desc.height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (presenter->m_window == nullptr) {
        return failure("SDL_CreateWindow");
    }
    // On this thread, the one that made the window, as SDL requires; the swapchain starts as SDR with VSYNC.
    if (!SDL_ClaimWindowForGPUDevice(presenter->m_device, presenter->m_window)) {
        return failure("claiming the window for the GPU device (" + presenter->m_driver + ")");
    }
    presenter->m_claimed = true;
    presenter->m_offered.mailbox =
        SDL_WindowSupportsGPUPresentMode(presenter->m_device, presenter->m_window, SDL_GPU_PRESENTMODE_MAILBOX);
    presenter->m_offered.immediate =
        SDL_WindowSupportsGPUPresentMode(presenter->m_device, presenter->m_window, SDL_GPU_PRESENTMODE_IMMEDIATE);
    presenter->choosePresentMode(desc.vsync, desc.fpsCap);
    // The 2D drawing: without it the window is still shown and cleared, as before the drawing existed.
    if (auto renderer = GpuRenderer::create(presenter->m_device); renderer) {
        presenter->m_renderer = std::move(*renderer);
    } else {
        presenter->m_drawingProblem = renderer.error().message;
    }
    return presenter;
}

// The reverse of start(): the drawing first, then the GPU finishes with the swapchain before the window leaves the
// device.
GpuPresenter::~GpuPresenter() {
    m_renderer.reset();
    if (m_device != nullptr) {
        SDL_WaitForGPUIdle(m_device);
        if (m_claimed) {
            SDL_ReleaseWindowFromGPUDevice(m_device, m_window);
        }
        SDL_DestroyGPUDevice(m_device);
    }
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
    }
}

GpuOverlayHandle GpuPresenter::addOverlay(GpuOverlayDraw overlay) {
    if (!overlay) {
        return 0;
    }
    m_overlays.emplace_back(++m_lastOverlay, std::move(overlay));
    return m_lastOverlay;
}

void GpuPresenter::removeOverlay(GpuOverlayHandle handle) {
    std::erase_if(m_overlays, [handle](const auto& entry) { return entry.first == handle; });
}

std::optional<GpuOverlayTarget> GpuPresenter::overlayTarget() const {
    if (!m_renderer) {
        return std::nullopt;
    }
    return GpuOverlayTarget{m_device, GpuRenderer::targetFormat()};
}

void GpuPresenter::choosePresentMode(bool vsync, std::uint32_t fpsCap) {
    const PresentMode wanted = platform::choosePresentMode(vsync, fpsCap, m_offered);
    if (wanted == m_mode) {
        return;
    }
    // A mode the window said it offers; SDL refuses only one it does not, so on a refusal the old mode stays.
    if (SDL_SetGPUSwapchainParameters(m_device, m_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, toSdl(wanted))) {
        m_mode = wanted;
    }
}

std::string GpuPresenter::sceneProblem() const { return m_renderer ? m_renderer->sceneProblem() : std::string(); }

void GpuPresenter::releaseScreenCopy(graphics::ScreenCopyId id) {
    if (m_renderer) {
        m_renderer->releaseScreenCopy(id);
    }
}

GpuPresenter::Presented GpuPresenter::present(const graphics::FrameDescription& frame, graphics::Extent size,
                                              bool capture, const graphics::AssetStore* store, bool game) {
    Presented presented;
    if (!m_renderer) {
        presented.shown = clearAndPresent(frame.start().clear);
        if (capture) {
            presented.capture = std::unexpected(Error{ErrorCode::PlatformFailure, m_drawingProblem});
        }
        return presented;
    }
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(m_device);
    if (commands == nullptr) {
        if (capture) {
            presented.capture = failure("acquiring a GPU command buffer");
        }
        return presented;
    }
    // 1. The frame, drawn into the renderer's own target, then whatever draws over the picture (the developer overlay)
    // into the same target, so the window and a capture both hold it.
    SDL_GPUTexture* drawn = m_renderer->record(commands, frame, size, store);
    // A clip's frame is copied before the overlays draw over it (a clip shows the game alone).
    const bool gameRead = game && drawn != nullptr && m_renderer->recordReadBack(commands, GpuRenderer::ReadBack::Game);
    if (drawn != nullptr) {
        for (const auto& [handle, overlay] : m_overlays) {
            overlay(commands, drawn, size);
        }
    }
    // 2. Onto the window: waits as the present mode says for an image; none comes back while the window cannot show
    // one, and the frame is drawn all the same, so it can still be read back.
    SDL_GPUTexture* image = nullptr;
    Uint32 imageWidth = 0;
    Uint32 imageHeight = 0;
    if (SDL_WaitAndAcquireGPUSwapchainTexture(commands, m_window, &image, &imageWidth, &imageHeight) &&
        image != nullptr) {
        presented.shown = true;
        if (drawn != nullptr) {
            SDL_GPUBlitInfo blit{};
            blit.source.texture = drawn;
            blit.source.w = static_cast<Uint32>(size.width);
            blit.source.h = static_cast<Uint32>(size.height);
            blit.destination.texture = image;
            blit.destination.w = imageWidth;
            blit.destination.h = imageHeight;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = SDL_GPU_FILTER_NEAREST; // the same size, but for a window resized since the frame began
            SDL_BlitGPUTexture(commands, &blit);
        } else {
            // Nothing drawn (an empty frame size): the window black.
            SDL_GPUColorTargetInfo target{};
            target.texture = image;
            target.clear_color = SDL_FColor{0.0F, 0.0F, 0.0F, 1.0F};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            if (SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr); pass != nullptr) {
                SDL_EndGPURenderPass(pass);
            }
        }
    }
    // 3. The read-backs: the commands submitted with a fence and waited for, so the copies can be read.
    const bool pictureRead = capture && drawn != nullptr && m_renderer->recordReadBack(commands);
    if (pictureRead || gameRead) {
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
        if (fence == nullptr) {
            if (capture) {
                presented.capture = failure("submitting the frame to read it back");
            }
            return presented;
        }
        SDL_WaitForGPUFences(m_device, true, &fence, 1);
        SDL_ReleaseGPUFence(m_device, fence);
        if (gameRead) {
            if (std::vector<std::uint8_t> rgba = m_renderer->readBack(GpuRenderer::ReadBack::Game); !rgba.empty()) {
                presented.game = std::move(rgba);
            }
        }
        if (pictureRead) {
            std::vector<std::uint8_t> rgba = m_renderer->readBack();
            if (rgba.empty()) {
                presented.capture = failure("reading the frame back");
            } else {
                presented.capture = std::move(rgba);
            }
        } else if (capture) {
            presented.capture = failure("copying the frame to read it back");
        }
        return presented; // submitted with the fence
    }
    if (capture) {
        presented.capture =
            std::unexpected(Error{ErrorCode::PlatformFailure, "the GPU renderer drew no frame to read"});
    }
    SDL_SubmitGPUCommandBuffer(commands);
    return presented;
}

bool GpuPresenter::clearAndPresent(graphics::Rgba colour) {
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(m_device);
    if (commands == nullptr) {
        return false;
    }
    // Waits as the present mode says for an image to be free; none comes back while the window cannot show one. The
    // command buffer is submitted empty then: SDL allows no cancel once a swapchain image was asked for.
    SDL_GPUTexture* image = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, m_window, &image, nullptr, nullptr) || image == nullptr) {
        SDL_SubmitGPUCommandBuffer(commands);
        return false;
    }
    // One render pass that only clears: the stand-in where the 2D drawing could not be made (gpu-renderer.md#capture).
    SDL_GPUColorTargetInfo target{};
    target.texture = image;
    target.clear_color = SDL_FColor{static_cast<float>(colour.r) / 255.0F, static_cast<float>(colour.g) / 255.0F,
                                    static_cast<float>(colour.b) / 255.0F, static_cast<float>(colour.a) / 255.0F};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    if (pass != nullptr) {
        SDL_EndGPURenderPass(pass);
    }
    return SDL_SubmitGPUCommandBuffer(commands);
}

} // namespace coney::platform
