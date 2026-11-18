// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <vector>

#include "core/error.h"
#include "graphics/frame_description.h"
#include "graphics/overlay_batch.h"
#include "graphics/texture_grid.h"
#include "graphics/upload_cache.h"

// SDL's types, declared rather than included, so the engine's header stays free of SDL.
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;

namespace coney::platform {

/// Draws a frame description's 2D passes (graphics::FrameDescription: the front end, the legal screens, the movies, the
/// HUD) through SDL3's GPU API with Coney's own shaders, `overlay2d.vert.hlsl` and `overlay2d.frag.hlsl`
/// (docs/engine/gpu-renderer.md#drawing), into a target of its own the frame's size, which the presenter then copies to
/// the window and can read back (GpuPresenter, gpu_presenter.h). It draws what the GL3 renderer draws: the window
/// cleared to black round the logical screen (or the whole window to the frame's colour), each 2D draw blended by
/// source alpha with no depth test, culling or fog, the alpha test as GL3 has it (graphics::alphaRangeOf()), and each
/// texture sampled with its own filter, clamped or repeated as the draw says.
///
/// Not yet: the screen passes (the blur pulse, the line blend, the motion blur) and the 3D scene, which come with items
/// 13, 14 and 20 of docs/engine/renderer.md#order; the developer overlay (item 12).
///
/// Textures are uploaded the first time a frame draws them and kept by Texture::serial(): a sprite sheet's pixels once
/// (SheetTexture::toImage()), a PixelTexture's again at each new revision (a movie's frame), and a texture no frame has
/// drawn for kTextureKeepFrames frames is let go.
///
/// Coney's own: the original drew through the PS2's GS; nothing here ports it.
class GpuRenderer {
  public:
    /// How many frames a texture no frame draws is kept: about ten seconds of frames at 60 a second.
    static constexpr std::uint64_t kTextureKeepFrames = 600;

    /// Makes the shaders (in the format `device` takes), the 2D pipeline, its samplers and the white texture of flat
    /// colour. ErrorCode::PlatformFailure naming what SDL refused, or saying the build has no shaders
    /// (`-DCONEY_SHADERS=OFF`, docs/guides/shaders.md).
    [[nodiscard]] static std::expected<std::unique_ptr<GpuRenderer>, Error> create(SDL_GPUDevice* device);

    /// Releases everything it made, after the GPU has finished with it.
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;
    GpuRenderer(GpuRenderer&&) = delete;
    GpuRenderer& operator=(GpuRenderer&&) = delete;

    /// Records the drawing of `frame` at `size` into `commands`: the textures and vertices uploaded, then one render
    /// pass into the target. Returns the target, which holds the frame once the commands have run and stays valid until
    /// the next record(); null when `size` is empty or SDL refused to make the target at that size.
    SDL_GPUTexture* record(SDL_GPUCommandBuffer* commands, const graphics::FrameDescription& frame,
                           graphics::Extent size);

    /// Records a copy of the target, as the last record() drew it, into memory the CPU reads: call it after record() in
    /// the same command buffer, submit with a fence and wait for it, then take the pixels with readBack(). False when
    /// there is no target or SDL refused the buffer.
    bool recordReadBack(SDL_GPUCommandBuffer* commands);

    /// The pixels recordReadBack() copied, RGBA with rows top first, at the last record()'s size, once the GPU has run
    /// the commands; empty when there were none or SDL refused to map them.
    [[nodiscard]] std::vector<std::uint8_t> readBack();

    /// How many textures it keeps uploaded.
    [[nodiscard]] std::size_t textureCount() const { return m_textures.size(); }

  private:
    /// A texture the renderer made, released through the device when it goes.
    struct OwnedTexture {
        SDL_GPUDevice* device = nullptr;
        SDL_GPUTexture* texture = nullptr;
        int width = 0;
        int height = 0;
        bool linear = true; // sampled blending the four nearest texels, else the nearest one

        OwnedTexture() = default;
        OwnedTexture(SDL_GPUDevice* owner, SDL_GPUTexture* made, int w, int h, bool blend)
            : device(owner), texture(made), width(w), height(h), linear(blend) {}
        ~OwnedTexture();
        OwnedTexture(const OwnedTexture&) = delete;
        OwnedTexture& operator=(const OwnedTexture&) = delete;
        OwnedTexture(OwnedTexture&& other) noexcept;
        OwnedTexture& operator=(OwnedTexture&& other) noexcept;
    };

    /// A texture's pixels to upload this frame.
    struct PendingUpload {
        SDL_GPUTexture* texture = nullptr;
        std::uint64_t serial = 0; // the cache's key, forgotten when the upload fails (0 for the white texture)
        std::vector<std::uint8_t> pixels;
        int width = 0;
        int height = 0;
    };

    explicit GpuRenderer(SDL_GPUDevice* device) : m_device(device) {}

    /// Makes (or keeps) the target at `size`; false when SDL refuses it.
    bool ensureTarget(graphics::Extent size);
    /// Makes `buffer` at least `bytes` large for `usage` (SDL_GPUBufferUsageFlags), growing by doubling; false when SDL
    /// refuses it.
    bool ensureBuffer(SDL_GPUBuffer*& buffer, std::uint32_t& capacity, std::uint32_t bytes, std::uint32_t usage);
    /// Makes the upload buffer at least `bytes` large; false when SDL refuses it.
    bool ensureUploadBuffer(std::uint32_t bytes);
    /// The uploaded texture `texture` (a frame's texture, never null) draws with this frame, adding its pixels to
    /// `uploads` when it is new or has a new revision; null when it cannot be drawn (a kind of texture the GPU renderer
    /// does not take, or SDL refused it).
    const OwnedTexture* textureFor(const graphics::Texture* texture, std::vector<PendingUpload>& uploads);
    /// The sampler for `linear` filtering (else nearest) and `address`.
    SDL_GPUSampler* samplerFor(bool linear, graphics::TextureAddress address) const;

    SDL_GPUDevice* m_device;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_samplers[4] = {}; // nearest clamp, nearest repeat, linear clamp, linear repeat
    OwnedTexture m_white;               // one white texel: flat colour
    bool m_whiteUploaded = false;
    OwnedTexture m_target; // what record() draws into
    SDL_GPUBuffer* m_vertices = nullptr;
    std::uint32_t m_vertexCapacity = 0;
    SDL_GPUBuffer* m_indices = nullptr;
    std::uint32_t m_indexCapacity = 0;
    SDL_GPUTransferBuffer* m_upload = nullptr;
    std::uint32_t m_uploadCapacity = 0;
    SDL_GPUTransferBuffer* m_download = nullptr;
    std::uint32_t m_downloadCapacity = 0;
    graphics::Extent m_downloadSize; // the size recordReadBack() copied, while its pixels wait to be read
    graphics::UploadCache<OwnedTexture> m_textures;
    std::uint64_t m_frame = 0; // record()s so far: the cache's clock
};

} // namespace coney::platform
