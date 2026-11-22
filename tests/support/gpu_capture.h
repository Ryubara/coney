// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the tests that draw on the GPU renderer's hidden window and read the frame back pixel by pixel share: whether
// this machine has a display and a GPU device, and a pixel of the frame (RenderEngine::setFrameTap(): RGBA rows, top
// first) compared with a colour. A capture's PNG would do, but the engine writes a picture of 16 colours or fewer as a
// palette PNG, which librw's reader does not read.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include "graphics/render_device.h"

namespace coney::test {

/// Whether SDL's video starts here, sees a display and has a GPU driver that takes Coney's shader formats. Leaves the
/// video stopped again.
inline bool gpuHere() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return false;
    }
    int displays = 0;
    SDL_DisplayID* list = SDL_GetDisplays(&displays);
    SDL_free(list);
    const bool ready =
        displays > 0 && SDL_GPUSupportsShaderFormats(
                            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, nullptr);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return ready;
}

/// The colour of pixel (`x`, `y`) of an RGBA image `width` wide, rows top first.
inline graphics::Rgba pixelAt(const std::vector<std::uint8_t>& rgba, int width, int x, int y) {
    const std::size_t at = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + x) * 4;
    return graphics::Rgba{rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

/// Whether `got` is `want` give or take `slack` in each colour channel (a GPU rounds its own way).
inline bool near(graphics::Rgba got, graphics::Rgba want, int slack) {
    const auto close = [slack](int a, int b) { return a - b <= slack && b - a <= slack; };
    return close(got.r, want.r) && close(got.g, want.g) && close(got.b, want.b);
}

} // namespace coney::test
