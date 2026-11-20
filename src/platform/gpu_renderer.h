// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "core/error.h"
#include "graphics/asset_store.h"
#include "graphics/frame_description.h"
#include "graphics/overlay_batch.h"
#include "graphics/texture_grid.h"
#include "graphics/upload_cache.h"
#include "platform/gpu_scene.h"

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
/// Its scene passes (the streamed world's sectors, the world objects and the parked cars: item 13 of
/// docs/engine/renderer.md#order) are drawn between the 2D passes in their order by GpuScene (gpu_scene.h), from the
/// asset store's meshes and textures, with a depth buffer the target's size. Not yet: the screen passes (the blur
/// pulse, the line blend, the motion blur) and the humans, which come with items 14 and 20. The developer overlay (item
/// 12) draws over the target afterwards, through GpuPresenter (GpuOverlayDraw).
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

    /// Makes the shaders (in the format `device` takes), the 2D pipeline, its samplers, the white texture of flat
    /// colour and the scene drawing (GpuScene). ErrorCode::PlatformFailure naming what SDL refused for the 2D drawing,
    /// or saying the build has no shaders (`-DCONEY_SHADERS=OFF`, docs/guides/shaders.md); a scene drawing SDL refuses
    /// leaves the 2D drawing working and says why in sceneProblem().
    [[nodiscard]] static std::expected<std::unique_ptr<GpuRenderer>, Error> create(SDL_GPUDevice* device);

    /// The format of the target record() draws into (an `SDL_GPUTextureFormat`): what a drawing over the frame
    /// (GpuOverlayDraw) makes its pipeline for.
    [[nodiscard]] static std::uint32_t targetFormat();

    /// Releases everything it made, after the GPU has finished with it.
    ~GpuRenderer();
    GpuRenderer(const GpuRenderer&) = delete;
    GpuRenderer& operator=(const GpuRenderer&) = delete;
    GpuRenderer(GpuRenderer&&) = delete;
    GpuRenderer& operator=(GpuRenderer&&) = delete;

    /// Records the drawing of `frame` at `size` into `commands`: the textures, vertices and the scene passes' new
    /// meshes and textures (from `store`; null draws no scene pass) uploaded, then one render pass into the target.
    /// Returns the target, which holds the frame once the commands have run and stays valid until the next record();
    /// null when `size` is empty or SDL refused to make the target at that size.
    SDL_GPUTexture* record(SDL_GPUCommandBuffer* commands, const graphics::FrameDescription& frame,
                           graphics::Extent size, const graphics::AssetStore* store = nullptr);

    /// Where in a frame a read-back is taken: each has its own buffer, so one frame can be read at both.
    enum class ReadBack : std::uint8_t {
        Picture, ///< the frame as the window shows it, the overlays included: a screenshot
        Game,    ///< the frame before the overlays draw over it: a clip's frame
    };

    /// Records a copy of the target, as the commands recorded so far leave it, into `at`'s memory the CPU reads: call
    /// it after record() in the same command buffer, submit with a fence and wait for it, then take the pixels with
    /// readBack(). False when there is no target or SDL refused the buffer.
    bool recordReadBack(SDL_GPUCommandBuffer* commands, ReadBack at = ReadBack::Picture);

    /// The pixels recordReadBack() copied for `at`, RGBA with rows top first, at the last record()'s size, once the GPU
    /// has run the commands; empty when there were none or SDL refused to map them.
    [[nodiscard]] std::vector<std::uint8_t> readBack(ReadBack at = ReadBack::Picture);

    /// How many textures it keeps uploaded for the 2D passes.
    [[nodiscard]] std::size_t textureCount() const { return m_textures.size(); }

    /// Why the scene passes are not drawn (SDL refused their shaders or pipeline); empty when they are.
    [[nodiscard]] const std::string& sceneProblem() const { return m_sceneProblem; }

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
        std::vector<std::uint8_t> pixels;
        int width = 0;
        int height = 0;
        std::uint64_t serial = 0; // the cache's key, forgotten when the upload fails (0 for the white texture)
    };

    explicit GpuRenderer(SDL_GPUDevice* device) : m_device(device) {}

    /// Makes (or keeps) the target and its depth buffer at `size`; false when SDL refuses them.
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
    /// Forgets what `uploads` meant to put on the GPU (a frame that failed before it could): the white texel and each
    /// texture's cache entry, so the next frame uploads them again.
    void forgetUploads(const std::vector<PendingUpload>& uploads);
    /// The sampler for `linear` filtering (else nearest) and `address`.
    SDL_GPUSampler* samplerFor(bool linear, graphics::TextureAddress address) const;

    SDL_GPUDevice* m_device;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_samplers[4] = {}; // nearest clamp, nearest repeat, linear clamp, linear repeat
    OwnedTexture m_white;               // one white texel: flat colour
    bool m_whiteUploaded = false;
    OwnedTexture m_target;             // what record() draws into
    OwnedTexture m_depth;              // its depth buffer, the same size
    std::uint32_t m_depthFormat = 0;   // an SDL_GPUTextureFormat: 24-bit where the device has it, else 32-bit float
    std::unique_ptr<GpuScene> m_scene; // the scene passes' drawing; null when SDL refused it
    std::string m_sceneProblem;        // why, then
    SDL_GPUBuffer* m_vertices = nullptr;
    std::uint32_t m_vertexCapacity = 0;
    SDL_GPUBuffer* m_indices = nullptr;
    std::uint32_t m_indexCapacity = 0;
    SDL_GPUTransferBuffer* m_upload = nullptr;
    std::uint32_t m_uploadCapacity = 0;
    /// A buffer the CPU reads a copy of the target from.
    struct Download {
        SDL_GPUTransferBuffer* buffer = nullptr;
        std::uint32_t capacity = 0;
        graphics::Extent size; // the size recordReadBack() copied, while its pixels wait to be read
    };
    std::array<Download, 2> m_downloads; // one for each ReadBack
    graphics::UploadCache<OwnedTexture> m_textures;
    std::uint64_t m_frame = 0; // record()s so far: the cache's clock
};

} // namespace coney::platform
