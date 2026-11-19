// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

#include "graphics/texture_grid.h"

// SDL's types, declared rather than included, so the engine's header stays free of SDL.
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;

namespace coney::platform {

/// What something that draws over the GPU renderer's frame needs to build its own pipelines: the device, and the
/// format of the target the frame is drawn into (an `SDL_GPUTextureFormat`, as an integer so no SDL enum is needed
/// here). Handed out by RenderEngine::gpuOverlayTarget().
struct GpuOverlayTarget {
    SDL_GPUDevice* device = nullptr;
    std::uint32_t colourFormat = 0;
};

/// A drawing over the GPU renderer's frame, called by GpuPresenter::present() once the frame's 2D passes are recorded
/// into `commands`, with the target holding them (at `size`, in the format of GpuOverlayTarget) and no render pass
/// open: it records its own, loading what is there. Several may be added at once (the developer overlay, the console
/// bar), each called in the order added and each making its own Dear ImGui context current before it draws; the
/// picture they add is in the frame a capture reads back and in the window.
/// @coney-only the GPU renderer's own; the original had no developer overlay
using GpuOverlayDraw =
    std::function<void(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target, graphics::Extent size)>;

/// What adding a GpuOverlayDraw gave back, to remove it by; 0 is none.
using GpuOverlayHandle = std::uint64_t;

} // namespace coney::platform
