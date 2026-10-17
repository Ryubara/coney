#include "platform/platform.hpp"

#include <SDL3/SDL.h>

namespace coney::platform {

Result<std::unique_ptr<GpuContext>, std::string> GpuContext::create(Window& window) {
    // No shaders yet: accept any backend SDL can offer.
    constexpr SDL_GPUShaderFormat kFormats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                             SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
    SDL_GPUDevice* device = SDL_CreateGPUDevice(kFormats, false, nullptr);
    if (device == nullptr) {
        return Failure{std::string("SDL_CreateGPUDevice failed: ") + SDL_GetError()};
    }
    if (!SDL_ClaimWindowForGPUDevice(device, window.sdl_window())) {
        std::string error = std::string("SDL_ClaimWindowForGPUDevice failed: ") + SDL_GetError();
        SDL_DestroyGPUDevice(device);
        return Failure{std::move(error)};
    }
    std::string driver = SDL_GetGPUDeviceDriver(device);
    window.platform().logger().info(core::log_category::kGpu, "GPU device on the {} driver", driver);
    return std::unique_ptr<GpuContext>(new GpuContext(window, device, std::move(driver)));
}

GpuContext::~GpuContext() {
    SDL_WaitForGPUIdle(device_);
    SDL_ReleaseWindowFromGPUDevice(device_, window_.sdl_window());
    SDL_DestroyGPUDevice(device_);
}

bool GpuContext::clear(const core::Colour& colour) {
    core::Logger& logger = window_.platform().logger();
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device_);
    if (commands == nullptr) {
        logger.error(core::log_category::kGpu, "SDL_AcquireGPUCommandBuffer failed: {}", SDL_GetError());
        return false;
    }
    SDL_GPUTexture* target = nullptr;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window_.sdl_window(), &target, nullptr, nullptr)) {
        logger.error(core::log_category::kGpu, "SDL_WaitAndAcquireGPUSwapchainTexture failed: {}", SDL_GetError());
        SDL_CancelGPUCommandBuffer(commands);
        return false;
    }
    if (target != nullptr) {
        SDL_GPUColorTargetInfo info{};
        info.texture = target;
        info.clear_color = SDL_FColor{colour.r, colour.g, colour.b, colour.a};
        info.load_op = SDL_GPU_LOADOP_CLEAR;
        info.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &info, 1, nullptr);
        SDL_EndGPURenderPass(pass);
    }
    if (!SDL_SubmitGPUCommandBuffer(commands)) {
        logger.error(core::log_category::kGpu, "SDL_SubmitGPUCommandBuffer failed: {}", SDL_GetError());
        return false;
    }
    return true;
}

} // namespace coney::platform
