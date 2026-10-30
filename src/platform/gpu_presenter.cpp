// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_presenter.h"

#include <string>
#include <utility>

#include <SDL3/SDL.h>

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
    return presenter;
}

// The reverse of start(): the GPU finishes with the swapchain before the window leaves the device.
GpuPresenter::~GpuPresenter() {
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
    // One render pass that only clears: drawing comes with the shaders (renderer.md#order, items 4 and 11).
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
