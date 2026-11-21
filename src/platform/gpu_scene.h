// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/error.h"
#include "graphics/asset_store.h"
#include "graphics/frame_description.h"
#include "graphics/scene_batch.h"
#include "graphics/upload_cache.h"
#include "platform/gpu_bone_palette.h"

// SDL's types, declared rather than included, so the engine's header stays free of SDL.
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPUCopyPass;
struct SDL_GPURenderPass;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUTransferBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPUSampler;
struct SDL_GPUShader;

namespace coney::platform {

/// Draws a frame description's scene passes (the streamed world's sectors, the world objects, the parked cars and the
/// humans, skinned: docs/engine/gpu-renderer.md#skinning) for the GPU renderer (GpuRenderer, gpu_renderer.h) with
/// Coney's own shaders, `scene3d.vert.hlsl` and `scene3d.frag.hlsl` (docs/engine/gpu-renderer.md#scene): each mesh and
/// texture of the asset store uploaded the first time a frame draws it and kept by id, then each sub-mesh drawn through
/// its pass's camera in the states, lights, colours, passes and texture level librw's GL3 device would give it
/// (graphics/scene_batch.h), so the two renderers draw the same picture.
///
/// A frame is drawn in steps the renderer interleaves with its own: prepare() reads the frame, layUploads(),
/// writeUploads() and recordUploads() put what is new on the GPU in the renderer's copy pass, and draw() draws one
/// scene pass in its render pass. A mesh or texture no frame has drawn for kKeepFrames frames is let go; ids are never
/// handed out twice, so one that is gone is simply never asked for again.
///
/// Coney's own: the original drew through RenderWare's PS2 pipelines and the GS; nothing here ports it.
class GpuScene {
  public:
    /// How many frames a mesh or texture no frame draws is kept: about ten seconds of frames at 60 a second.
    static constexpr std::uint64_t kKeepFrames = 600;

    /// Makes the shaders (in the format `device` takes) for a target of `colourFormat` with a depth buffer of
    /// `depthFormat` (`SDL_GPUTextureFormat`s); the pipelines are made as the states first need them.
    /// ErrorCode::PlatformFailure naming what SDL refused, or saying the build has no shaders.
    [[nodiscard]] static std::expected<std::unique_ptr<GpuScene>, Error>
    create(SDL_GPUDevice* device, std::uint32_t colourFormat, std::uint32_t depthFormat);

    /// Releases everything it made, after the GPU has finished with it.
    ~GpuScene();
    GpuScene(const GpuScene&) = delete;
    GpuScene& operator=(const GpuScene&) = delete;
    GpuScene(GpuScene&&) = delete;
    GpuScene& operator=(GpuScene&&) = delete;

    /// Reads `frame`'s scene passes with the meshes and textures of `store` (frame number `frameNumber`, the caches'
    /// clock): what to upload and every sub-mesh's draw. `white` is the texture an untextured material samples. A scene
    /// pass with no camera, a mesh the store does not hold and a sub-mesh SDL cannot make a buffer or pipeline for are
    /// left out, as a missing part of the picture; so is a posed draw whose mesh cannot be skinned by its pose
    /// (graphics::posingOf()). The frame's poses go into the bone palette (graphics::bonePaletteOf()) for the copy
    /// pass.
    void prepare(const graphics::FrameDescription& frame, const graphics::AssetStore& store, std::uint64_t frameNumber,
                 SDL_GPUTexture* white);
    /// Places prepare()'s uploads in the renderer's upload buffer from `offset` on and returns where they end.
    [[nodiscard]] std::uint32_t layUploads(std::uint32_t offset);
    /// Writes the uploads' bytes into the mapped upload buffer at their places.
    void writeUploads(std::uint8_t* mapped) const;
    /// Records the uploads from `upload` into the GPU's buffers and textures in `copy`.
    void recordUploads(SDL_GPUCopyPass* copy, SDL_GPUTransferBuffer* upload) const;
    /// Forgets what this frame meant to upload (a frame that failed before it could), so the next uploads it again.
    void forgetUploads();
    /// Draws the sub-meshes of the frame's pass `pass` (its index in FrameDescription::passes()) into `target`.
    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* target, std::size_t pass);

    /// How many meshes it keeps uploaded.
    [[nodiscard]] std::size_t meshCount() const { return m_meshes.size(); }
    /// How many textures it keeps uploaded.
    [[nodiscard]] std::size_t textureCount() const { return m_textures.size(); }

  private:
    /// A mesh on the GPU: its vertices and indices, and what its sub-meshes need of it.
    struct GpuMesh {
        SDL_GPUDevice* device = nullptr;
        SDL_GPUBuffer* vertices = nullptr;
        SDL_GPUBuffer* indices = nullptr;
        std::vector<bool> vertexAlpha; // per sub-mesh (graphics::vertexAlphaOf())
        std::size_t bonesNeeded = 0;   // graphics::bonesNeededBy()

        GpuMesh() = default;
        ~GpuMesh();
        GpuMesh(const GpuMesh&) = delete;
        GpuMesh& operator=(const GpuMesh&) = delete;
        GpuMesh(GpuMesh&& other) noexcept;
        GpuMesh& operator=(GpuMesh&& other) noexcept;
    };

    /// A texture on the GPU, every level of it, and whether it turns blending on (graphics::hasAlpha()).
    struct GpuTexture {
        SDL_GPUDevice* device = nullptr;
        SDL_GPUTexture* texture = nullptr;
        bool alpha = false;

        GpuTexture() = default;
        ~GpuTexture();
        GpuTexture(const GpuTexture&) = delete;
        GpuTexture& operator=(const GpuTexture&) = delete;
        GpuTexture(GpuTexture&& other) noexcept;
        GpuTexture& operator=(GpuTexture&& other) noexcept;
    };

    /// Bytes for one of the GPU's buffers this frame.
    struct BufferUpload {
        SDL_GPUBuffer* buffer = nullptr;
        const void* bytes = nullptr; // held by the asset store until the frame is recorded
        std::uint32_t size = 0;
        std::uint32_t offset = 0;
        std::uint64_t mesh = 0; // the cache's key, forgotten when the upload fails
    };

    /// One level of a texture this frame.
    struct LevelUpload {
        SDL_GPUTexture* texture = nullptr;
        const graphics::TextureLevel* level = nullptr; // held by the asset store until the frame is recorded
        std::uint32_t mip = 0;
        std::uint32_t offset = 0;
        std::uint64_t key = 0; // the cache's key, forgotten when the upload fails
    };

    /// The states a pipeline is made for.
    struct PipelineKey {
        bool strip = false;
        bool blend = false;
        bool depthTest = true;
        bool depthWrite = true;
        graphics::CullFaces cull = graphics::CullFaces::Back;

        friend auto operator<=>(const PipelineKey&, const PipelineKey&) = default;
    };

    /// The vertex shader's uniform buffer (scene3d.vert.hlsl's Draw), 720 bytes.
    struct VertexUniform {
        graphics::Matrix4 world{};
        graphics::Matrix4 viewProjection{};
        std::array<float, 4> ambient{};
        std::array<std::array<float, 4>, graphics::kMaxDrawLights> lightParams{};
        std::array<std::array<float, 4>, graphics::kMaxDrawLights> lightPosition{};
        std::array<std::array<float, 4>, graphics::kMaxDrawLights> lightDirection{};
        std::array<std::array<float, 4>, graphics::kMaxDrawLights> lightColour{};
        std::array<float, 4> materialColour{};
        std::array<float, 4> surface{};
        std::array<float, 4> fogData{};
        std::array<std::uint32_t, 4> skin{}; // the draw's first bone, 1 when it is skinned
    };

    /// The fragment shader's uniform buffer (scene3d.frag.hlsl's Fragment).
    struct FragmentUniform {
        float alphaLow = -1000.0F;
        float alphaHigh = 1000.0F;
        float unused[2] = {0.0F, 0.0F};
        std::array<float, 4> levels{};
        std::array<float, 4> fogColour{};
    };

    /// One pass of one sub-mesh, ready to draw.
    struct SubDraw {
        SDL_GPUGraphicsPipeline* pipeline = nullptr;
        SDL_GPUBuffer* vertices = nullptr;
        SDL_GPUBuffer* indices = nullptr;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        SDL_GPUTexture* texture = nullptr;
        SDL_GPUSampler* sampler = nullptr;
        VertexUniform vertex;
        FragmentUniform fragment;
    };

    explicit GpuScene(SDL_GPUDevice* device) : m_device(device), m_bones(device) {}

    /// What one mesh of a draw is drawn with: its colours, its materials' textures, its states, and whether it is a
    /// dual layer and skinned by the draw's pose.
    struct MeshLook {
        const graphics::DrawColours* colours = nullptr;
        std::span<const graphics::TextureId> textures;
        const graphics::SceneStates* states = nullptr;
        bool layer = false;
        bool skinned = false;
    };

    /// Adds the sub-draws of `mesh`, uploaded as `uploaded` (a draw's mesh, or its dual layer), to the list, lit by
    /// `lights` and drawn as `look` says.
    void prepareMesh(const GpuMesh& uploaded, const graphics::MeshData& mesh, const graphics::Draw3d& draw,
                     const graphics::SceneCamera& camera, std::span<const graphics::SceneLight> lights,
                     const MeshLook& look, const graphics::AssetStore& store, SDL_GPUTexture* white);
    /// The uploaded mesh `id`, queuing its upload when it is new; null when SDL refused its buffers.
    const GpuMesh* meshFor(graphics::MeshId id, const graphics::MeshData& mesh);
    /// The uploaded texture `id`, queuing its upload when it is new; null when SDL refused it.
    const GpuTexture* textureFor(graphics::TextureId id, const graphics::TextureData& texture);
    /// The pipeline for `key`, made the first time; null when SDL refuses it.
    SDL_GPUGraphicsPipeline* pipelineFor(const PipelineKey& key);
    /// The sampler for `sampler`, made the first time; null when SDL refuses it.
    SDL_GPUSampler* samplerFor(const graphics::TextureSampler& sampler);

    SDL_GPUDevice* m_device;
    SDL_GPUShader* m_vertexShader = nullptr; // kept for the pipelines made later
    SDL_GPUShader* m_fragmentShader = nullptr;
    std::uint32_t m_colourFormat = 0;
    std::uint32_t m_depthFormat = 0;
    std::map<PipelineKey, SDL_GPUGraphicsPipeline*> m_pipelines;
    std::map<std::uint32_t, SDL_GPUSampler*> m_samplers; // by filter and addressing
    graphics::UploadCache<GpuMesh> m_meshes;             // by MeshId
    graphics::UploadCache<GpuTexture> m_textures;        // by TextureId
    std::uint64_t m_frame = 0;
    std::vector<BufferUpload> m_bufferUploads;
    std::vector<LevelUpload> m_levelUploads;
    std::vector<SubDraw> m_draws;
    GpuBonePalette m_bones;                                       // the frame's bone palette, cycled at each upload
    std::vector<std::pair<std::size_t, std::size_t>> m_passDraws; // per pass: the first sub-draw and how many
};

} // namespace coney::platform
