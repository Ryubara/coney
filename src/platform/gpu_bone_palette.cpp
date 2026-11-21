// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_bone_palette.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <SDL3/SDL.h>

namespace coney::platform {

namespace {

// The palette's first size: four 50-bone humans before it grows.
constexpr std::size_t kFirstBoneCapacity = 256;
// Where the matrices start in the upload buffer: a multiple of 16, enough for every API's copies.
constexpr std::uint32_t kAlignment = 16;

// The bytes of `count` matrices.
std::uint32_t bytesOf(std::size_t count) { return static_cast<std::uint32_t>(count * sizeof(graphics::Matrix4)); }

} // namespace

GpuBonePalette::~GpuBonePalette() {
    if (m_buffer != nullptr) {
        SDL_ReleaseGPUBuffer(m_device, m_buffer);
    }
}

bool GpuBonePalette::reserve(std::size_t bones) {
    if (bones <= m_capacity && m_buffer != nullptr) {
        return true;
    }
    // Doubled until it fits, so a growing crowd makes few buffers; SDL keeps the old one until the GPU is done with it.
    std::size_t capacity = std::max(m_capacity, kFirstBoneCapacity);
    while (capacity < bones) {
        capacity *= 2;
    }
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    info.size = bytesOf(capacity);
    SDL_GPUBuffer* made = SDL_CreateGPUBuffer(m_device, &info);
    if (made == nullptr) {
        return false;
    }
    if (m_buffer != nullptr) {
        SDL_ReleaseGPUBuffer(m_device, m_buffer);
    }
    m_buffer = made;
    m_capacity = capacity;
    return true;
}

bool GpuBonePalette::set(std::vector<graphics::Matrix4> matrices) {
    if (!reserve(matrices.size())) {
        m_matrices.clear();
        return false;
    }
    m_matrices = std::move(matrices);
    return true;
}

std::uint32_t GpuBonePalette::lay(std::uint32_t offset) {
    if (m_matrices.empty()) {
        return offset;
    }
    m_offset = (offset + kAlignment - 1) / kAlignment * kAlignment;
    return m_offset + bytesOf(m_matrices.size());
}

void GpuBonePalette::write(std::uint8_t* mapped) const {
    if (!m_matrices.empty()) {
        std::memcpy(mapped + m_offset, m_matrices.data(), bytesOf(m_matrices.size()));
    }
}

void GpuBonePalette::record(SDL_GPUCopyPass* copy, SDL_GPUTransferBuffer* upload) const {
    if (m_matrices.empty()) {
        return;
    }
    const SDL_GPUTransferBufferLocation from{upload, m_offset};
    const SDL_GPUBufferRegion to{m_buffer, 0, bytesOf(m_matrices.size())};
    SDL_UploadToGPUBuffer(copy, &from, &to, true);
}

} // namespace coney::platform
