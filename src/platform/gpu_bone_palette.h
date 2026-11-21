// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "graphics/scene_batch.h"

// SDL's types, declared rather than included, so the engine's header stays free of SDL.
struct SDL_GPUDevice;
struct SDL_GPUBuffer;
struct SDL_GPUCopyPass;
struct SDL_GPUTransferBuffer;

namespace coney::platform {

/// The frame's bone palette on the GPU: the matrices of every posed human of the frame (graphics::bonePaletteOf()), in
/// one storage buffer the scene's vertex shader indexes by a draw's first bone (docs/engine/gpu-renderer.md#skinning).
/// GpuScene holds one and runs it in the steps of its frame: set() when it reads the frame, lay(), write() and record()
/// with the renderer's uploads, and the buffer bound for each draw.
///
/// Coney's own: the original skins on the PS2's vector unit; the palette's layout is GL3's skinned pipeline's
/// (platform/character_skin.h).
class GpuBonePalette {
  public:
    /// An empty palette for `device`, holding no buffer until reserve() is called.
    explicit GpuBonePalette(SDL_GPUDevice* device) : m_device(device) {}
    /// Releases the buffer.
    ~GpuBonePalette();
    GpuBonePalette(const GpuBonePalette&) = delete;
    GpuBonePalette& operator=(const GpuBonePalette&) = delete;
    GpuBonePalette(GpuBonePalette&&) = delete;
    GpuBonePalette& operator=(GpuBonePalette&&) = delete;

    /// Makes the buffer hold at least `bones` bones, growing it (never below a first size of four 50-bone humans);
    /// false when SDL refused a larger one, and the buffer is as it was.
    [[nodiscard]] bool reserve(std::size_t bones);
    /// Takes this frame's `matrices` for the copy pass, growing the buffer to hold them; false when it cannot, with the
    /// frame's palette left empty so the posed draws are left out.
    [[nodiscard]] bool set(std::vector<graphics::Matrix4> matrices);
    /// Whether this frame has any matrices to upload.
    [[nodiscard]] bool empty() const { return m_matrices.empty(); }
    /// Places the matrices in the upload buffer from `offset` on and returns where they end (`offset` when empty).
    [[nodiscard]] std::uint32_t lay(std::uint32_t offset);
    /// Writes the matrices' bytes into the mapped upload buffer where lay() put them.
    void write(std::uint8_t* mapped) const;
    /// Records the copy of the matrices from `upload` into the buffer in `copy`, cycled: the buffer the last frame's
    /// draws may still read is left alone.
    void record(SDL_GPUCopyPass* copy, SDL_GPUTransferBuffer* upload) const;
    /// Forgets this frame's matrices (a frame that failed before it could upload them).
    void forget() { m_matrices.clear(); }
    /// The storage buffer the vertex shader reads; null only before reserve() has first succeeded.
    [[nodiscard]] SDL_GPUBuffer* buffer() const { return m_buffer; }

  private:
    SDL_GPUDevice* m_device;
    SDL_GPUBuffer* m_buffer = nullptr;
    std::size_t m_capacity = 0;                // bones the buffer holds
    std::vector<graphics::Matrix4> m_matrices; // this frame's, for the copy pass
    std::uint32_t m_offset = 0;                // where they are in the upload buffer
};

} // namespace coney::platform
