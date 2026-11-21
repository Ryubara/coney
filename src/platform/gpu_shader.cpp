// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_shader.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#if defined(CONEY_GPU_SHADERS)
#include "platform/shaders/embedded_shaders.h"
#endif

namespace coney::platform {

namespace {

// A failure to make shader `name`, with SDL's error text when it has one.
std::unexpected<Error> shaderFailure(std::string_view name, std::string_view why) {
    std::string message = "could not make the shader " + std::string(name) + ": " + std::string(why);
    if (const char* sdl = SDL_GetError(); sdl != nullptr && *sdl != '\0') {
        message += std::string(": ") + sdl;
    }
    return std::unexpected(Error{ErrorCode::PlatformFailure, std::move(message)});
}

} // namespace

std::expected<SDL_GPUShader*, Error> makeShader(SDL_GPUDevice* device, std::string_view name) {
#if !defined(CONEY_GPU_SHADERS)
    (void)device;
    return std::unexpected(Error{ErrorCode::PlatformFailure, "the GPU renderer needs the shaders, and this build has "
                                                             "none (-DCONEY_SHADERS=OFF)"});
#else
    const shaders::Shader* shader = shaders::findShader(name);
    if (shader == nullptr) {
        return std::unexpected(
            Error{ErrorCode::PlatformFailure, "could not make the shader " + std::string(name) + ": not in the build"});
    }
    const SDL_GPUShaderFormat offered = SDL_GetGPUShaderFormats(device);
    struct Choice {
        SDL_GPUShaderFormat sdl;
        shaders::Format ours;
    };
    // A device takes the format of its own API: SPIR-V (Vulkan), DXIL (Direct3D 12) or MSL (Metal).
    constexpr std::array<Choice, 3> choices{{{SDL_GPU_SHADERFORMAT_SPIRV, shaders::Format::Spirv},
                                             {SDL_GPU_SHADERFORMAT_DXIL, shaders::Format::Dxil},
                                             {SDL_GPU_SHADERFORMAT_MSL, shaders::Format::Msl}}};
    for (const Choice& choice : choices) {
        if ((offered & choice.sdl) == 0) {
            continue;
        }
        const std::span<const std::uint8_t> code = shader->codeFor(choice.ours);
        const std::string entry(shaders::entryPoint(choice.ours));
        SDL_GPUShaderCreateInfo info{};
        info.code_size = code.size();
        info.code = code.data();
        info.entrypoint = entry.c_str();
        info.format = choice.sdl;
        info.stage =
            shader->stage == shaders::Stage::Vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = shader->resources.samplers;
        info.num_storage_textures = shader->resources.storageTextures;
        info.num_storage_buffers = shader->resources.storageBuffers;
        info.num_uniform_buffers = shader->resources.uniformBuffers;
        SDL_GPUShader* made = SDL_CreateGPUShader(device, &info);
        if (made == nullptr) {
            return shaderFailure(name, "SDL refused it");
        }
        return made;
    }
    return std::unexpected(Error{ErrorCode::PlatformFailure, "could not make the shader " + std::string(name) +
                                                                 ": the device takes none of SPIR-V, DXIL and MSL"});
#endif
}

} // namespace coney::platform
