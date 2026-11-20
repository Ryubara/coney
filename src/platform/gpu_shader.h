// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string_view>

#include "core/error.h"

// SDL's types, declared rather than included, so the engine's header stays free of SDL.
struct SDL_GPUDevice;
struct SDL_GPUShader;

namespace coney::platform {

/// The embedded shader `name` (`<name>.<stage>` of src/platform/shaders/, docs/guides/shaders.md) made for `device` in
/// the first of SPIR-V, DXIL and MSL it takes, with the resource counts the build recorded. ErrorCode::PlatformFailure
/// naming the shader and what SDL refused, or saying the build has no shaders (`-DCONEY_SHADERS=OFF`). The caller
/// releases it (SDL_ReleaseGPUShader).
[[nodiscard]] std::expected<SDL_GPUShader*, Error> makeShader(SDL_GPUDevice* device, std::string_view name);

} // namespace coney::platform
