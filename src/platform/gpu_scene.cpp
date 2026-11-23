// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_scene.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#include "graphics/effect_batch.h"
#include "platform/gpu_shader.h"

namespace coney::platform {

namespace {

// Where in the upload buffer a texture level's texels start: Direct3D 12 places texture data at multiples of 512 bytes.
constexpr std::uint32_t kTextureAlignment = 512;
// Where a buffer's bytes start: a multiple of 16, enough for every API's copies.
constexpr std::uint32_t kBufferAlignment = 16;

// The least size of the buffer the scene lines go in, in bytes: 1024 vertices.
constexpr std::uint32_t kMinLineBuffer = 1024 * sizeof(graphics::MeshVertex);

// The least size of each buffer the effects go in, in bytes: 1024 corners, 1024 indices.
constexpr std::uint32_t kMinEffectVertexBuffer = 1024 * sizeof(graphics::MeshVertex);
constexpr std::uint32_t kMinEffectIndexBuffer = 1024 * sizeof(std::uint16_t);

// `value` rounded up to a multiple of `step`.
std::uint32_t alignUp(std::uint32_t value, std::uint32_t step) { return (value + step - 1) / step * step; }

// Makes `buffer` at least `bytes` large for `usage`, grown by doubling (at least `least`) so a frame with more does not
// make a buffer every frame; false, with no buffer, when SDL refuses it.
bool growBuffer(SDL_GPUDevice* device, SDL_GPUBuffer*& buffer, std::uint32_t& capacity, std::uint32_t bytes,
                SDL_GPUBufferUsageFlags usage, std::uint32_t least) {
    if (buffer != nullptr && bytes <= capacity) {
        return true;
    }
    if (buffer != nullptr) {
        SDL_ReleaseGPUBuffer(device, buffer);
        buffer = nullptr;
    }
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = std::max({bytes, capacity * 2, least});
    buffer = SDL_CreateGPUBuffer(device, &info);
    capacity = buffer != nullptr ? info.size : 0;
    return buffer != nullptr;
}

// The failure of a step of create(), with SDL's error text.
std::unexpected<Error> failure(const std::string& what) {
    std::string message = "could not set up the GPU renderer's scene drawing: " + what;
    if (const char* sdl = SDL_GetError(); sdl != nullptr && *sdl != '\0') {
        message += std::string(": ") + sdl;
    }
    return std::unexpected(Error{ErrorCode::PlatformFailure, std::move(message)});
}

// A colour's channels as the shaders take them, 0 to 1.
std::array<float, 4> channels(graphics::Rgba colour) {
    return {static_cast<float>(colour.r) / 255.0F, static_cast<float>(colour.g) / 255.0F,
            static_cast<float>(colour.b) / 255.0F, static_cast<float>(colour.a) / 255.0F};
}

// Three floats and a fourth.
std::array<float, 4> four(const std::array<float, 3>& v, float w) { return {v[0], v[1], v[2], w}; }

// SDL's addressing for RenderWare's: a bordered texture is clamped (SDL's GPU API has no border colour; stand-in, the
// disc's textures are seen wrapped, mirrored or clamped).
SDL_GPUSamplerAddressMode sdlAddress(graphics::TextureAddress address) {
    switch (address) {
    case graphics::TextureAddress::Wrap:
        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    case graphics::TextureAddress::Mirror:
        return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    case graphics::TextureAddress::Clamp:
    case graphics::TextureAddress::Border:
        return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}

// SDL's cull mode for a draw's.
SDL_GPUCullMode sdlCull(graphics::CullFaces cull) {
    switch (cull) {
    case graphics::CullFaces::None:
        return SDL_GPU_CULLMODE_NONE;
    case graphics::CullFaces::Back:
        return SDL_GPU_CULLMODE_BACK;
    case graphics::CullFaces::Front:
        return SDL_GPU_CULLMODE_FRONT;
    }
    return SDL_GPU_CULLMODE_BACK;
}

// A light's type as the vertex shader reads it: 1 directional, 2 point, 3 spot (librw's setLights).
float lightType(graphics::LightType type) {
    switch (type) {
    case graphics::LightType::Directional:
        return 1.0F;
    case graphics::LightType::Point:
        return 2.0F;
    case graphics::LightType::Spot:
        return 3.0F;
    case graphics::LightType::Ambient:
        break;
    }
    return 0.0F;
}

} // namespace

// Gives the buffers back to the device, unless they were moved away (SDL waits for the GPU to finish with them).
GpuScene::GpuMesh::~GpuMesh() {
    if (vertices != nullptr) {
        SDL_ReleaseGPUBuffer(device, vertices);
    }
    if (indices != nullptr) {
        SDL_ReleaseGPUBuffer(device, indices);
    }
}

// Takes the other's buffers, leaving it empty so only one owner releases them.
GpuScene::GpuMesh::GpuMesh(GpuMesh&& other) noexcept
    : device(other.device), vertices(std::exchange(other.vertices, nullptr)),
      indices(std::exchange(other.indices, nullptr)), vertexAlpha(std::move(other.vertexAlpha)) {}

// Releases this mesh's buffers, then takes the other's, as the move constructor does.
GpuScene::GpuMesh& GpuScene::GpuMesh::operator=(GpuMesh&& other) noexcept {
    if (this != &other) {
        GpuMesh dropped(std::move(*this));
        device = other.device;
        vertices = std::exchange(other.vertices, nullptr);
        indices = std::exchange(other.indices, nullptr);
        vertexAlpha = std::move(other.vertexAlpha);
    }
    return *this;
}

// Gives the texture back to the device, unless it was moved away.
GpuScene::GpuTexture::~GpuTexture() {
    if (texture != nullptr) {
        SDL_ReleaseGPUTexture(device, texture);
    }
}

// Takes the other's texture, leaving it empty.
GpuScene::GpuTexture::GpuTexture(GpuTexture&& other) noexcept
    : device(other.device), texture(std::exchange(other.texture, nullptr)), alpha(other.alpha) {}

// Releases this texture, then takes the other's.
GpuScene::GpuTexture& GpuScene::GpuTexture::operator=(GpuTexture&& other) noexcept {
    if (this != &other) {
        GpuTexture dropped(std::move(*this));
        device = other.device;
        texture = std::exchange(other.texture, nullptr);
        alpha = other.alpha;
    }
    return *this;
}

std::expected<std::unique_ptr<GpuScene>, Error> GpuScene::create(SDL_GPUDevice* device, std::uint32_t colourFormat,
                                                                 std::uint32_t depthFormat) {
    std::unique_ptr<GpuScene> scene(new GpuScene(device));
    scene->m_colourFormat = colourFormat;
    scene->m_depthFormat = depthFormat;
    auto vertex = makeShader(device, "scene3d.vert");
    if (!vertex) {
        return std::unexpected(std::move(vertex.error()));
    }
    scene->m_vertexShader = *vertex;
    auto fragment = makeShader(device, "scene3d.frag");
    if (!fragment) {
        return std::unexpected(std::move(fragment.error()));
    }
    scene->m_fragmentShader = *fragment;
    // The bone palette's buffer exists from the start: every draw binds it, posed or not.
    if (!scene->m_bones.reserve(0)) {
        return failure("its bone palette");
    }
    // The world pass's most common states, made now so a device that refuses them is known at the start.
    if (scene->pipelineFor(PipelineKey{}) == nullptr) {
        return failure("its pipeline");
    }
    return scene;
}

GpuScene::~GpuScene() {
    SDL_WaitForGPUIdle(m_device);
    m_meshes.clear();
    m_textures.clear();
    for (const auto& [key, pipeline] : m_pipelines) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, pipeline);
    }
    for (const auto& [key, sampler] : m_samplers) {
        SDL_ReleaseGPUSampler(m_device, sampler);
    }
    for (SDL_GPUBuffer* buffer : {m_lineBuffer, m_effectVertexBuffer, m_effectIndexBuffer}) {
        if (buffer != nullptr) {
            SDL_ReleaseGPUBuffer(m_device, buffer);
        }
    }
    if (m_vertexShader != nullptr) {
        SDL_ReleaseGPUShader(m_device, m_vertexShader);
    }
    if (m_fragmentShader != nullptr) {
        SDL_ReleaseGPUShader(m_device, m_fragmentShader);
    }
}

SDL_GPUGraphicsPipeline* GpuScene::pipelineFor(const PipelineKey& key) {
    if (const auto found = m_pipelines.find(key); found != m_pipelines.end()) {
        return found->second;
    }
    // One vertex buffer of graphics::MeshVertex: the position, normal, prelighting, first texture coordinates and the
    // skinning's bones and weights (all 0 for a mesh that is not skinned).
    const SDL_GPUVertexBufferDescription buffer{0, sizeof(graphics::MeshVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
    const std::array<SDL_GPUVertexAttribute, 6> attributes{{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(graphics::MeshVertex, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(graphics::MeshVertex, normal)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(graphics::MeshVertex, prelit)},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(graphics::MeshVertex, texCoords)},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, offsetof(graphics::MeshVertex, bones)},
        {5, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(graphics::MeshVertex, weights)},
    }};
    // Blended by source alpha (librw's SRCALPHA over INVSRCALPHA, the alpha channel by the same factors), added
    // (SRCALPHA over ONE: an effect's light), or replacing what is below (ONE over ZERO).
    const SDL_GPUBlendFactor below = key.additive ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    SDL_GPUColorTargetDescription target{};
    target.format = static_cast<SDL_GPUTextureFormat>(m_colourFormat);
    target.blend_state.enable_blend = key.blend;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = below;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_alpha_blendfactor = below;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = m_vertexShader;
    info.fragment_shader = m_fragmentShader;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
    if (key.lines) {
        info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST;
    } else {
        info.primitive_type = key.strip ? SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    }
    // The mesh views' wireframe: the triangles' edges, pulled towards the camera by a depth bias of -1 and a slope of
    // -1 as OpenGL's polygon offset (-1, -1) pulls GL3's lines (platform/draw_calls.cpp's setWireframe()), so they sit
    // on the fill they are drawn over.
    info.rasterizer_state.fill_mode = key.wire ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    if (key.wire) {
        info.rasterizer_state.enable_depth_bias = true;
        info.rasterizer_state.depth_bias_constant_factor = -1.0F;
        info.rasterizer_state.depth_bias_slope_factor = -1.0F;
    }
    info.rasterizer_state.cull_mode = sdlCull(key.cull);
    // OpenGL's default: counter-clockwise on a screen whose y is up is the front, which SDL's clip space shares.
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    // What lies past the far clip (or nearer than the near clip) is cut away, as OpenGL and librw do; SDL's default,
    // a clamped depth, would draw it on the far plane instead.
    info.rasterizer_state.enable_depth_clip = true;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    // librw's depth: tested less or equal, or always while the test is off but depth is still written.
    info.depth_stencil_state.enable_depth_test = key.depthTest || key.depthWrite;
    info.depth_stencil_state.enable_depth_write = key.depthWrite;
    info.depth_stencil_state.compare_op = key.depthTest ? SDL_GPU_COMPAREOP_LESS_OR_EQUAL : SDL_GPU_COMPAREOP_ALWAYS;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    info.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(m_depthFormat);
    info.target_info.has_depth_stencil_target = true;
    SDL_GPUGraphicsPipeline* made = SDL_CreateGPUGraphicsPipeline(m_device, &info);
    if (made != nullptr) {
        m_pipelines.emplace(key, made);
    }
    return made;
}

SDL_GPUSampler* GpuScene::samplerFor(const graphics::TextureSampler& sampler) {
    const std::uint32_t key = static_cast<std::uint32_t>(sampler.filter) |
                              (static_cast<std::uint32_t>(sampler.addressU) << 8U) |
                              (static_cast<std::uint32_t>(sampler.addressV) << 16U);
    if (const auto found = m_samplers.find(key); found != m_samplers.end()) {
        return found->second;
    }
    // librw's GL3 filters (gl3device.cpp's filterConvMap_MIP and _NoMIP), by RenderWare's numbering: the
    // minification's texel and level, the magnification's texel, and no level past 0 for the two filters without mip
    // levels.
    struct Filter {
        bool minLinear;
        bool mipLinear;
        bool magLinear;
        bool mipmapped;
    };
    Filter filter{false, false, false, false};
    switch (sampler.filter) {
    case graphics::TextureFilter::Nearest:
        filter = {false, false, false, false};
        break;
    case graphics::TextureFilter::Linear:
        filter = {true, false, true, false};
        break;
    case graphics::TextureFilter::MipNearest:
        filter = {false, false, false, true};
        break;
    case graphics::TextureFilter::MipLinear:
        filter = {true, false, true, true};
        break;
    case graphics::TextureFilter::LinearMipNearest:
        filter = {false, true, false, true};
        break;
    case graphics::TextureFilter::LinearMipLinear:
        filter = {true, true, true, true};
        break;
    }
    SDL_GPUSamplerCreateInfo info{};
    info.min_filter = filter.minLinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mag_filter = filter.magLinear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    info.mipmap_mode = filter.mipLinear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = sdlAddress(sampler.addressU);
    info.address_mode_v = sdlAddress(sampler.addressV);
    info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    info.min_lod = 0.0F;
    info.max_lod = filter.mipmapped ? 1000.0F : 0.0F;
    SDL_GPUSampler* made = SDL_CreateGPUSampler(m_device, &info);
    if (made != nullptr) {
        m_samplers.emplace(key, made);
    }
    return made;
}

const GpuScene::GpuMesh* GpuScene::meshFor(graphics::MeshId id, const graphics::MeshData& mesh) {
    if (auto* entry = m_meshes.find(id.value, m_frame); entry != nullptr) {
        return &entry->value;
    }
    // New: its buffers, made at the mesh's size, and its bytes queued for this frame's copy pass.
    GpuMesh made;
    made.device = m_device;
    const auto vertexBytes = static_cast<std::uint32_t>(mesh.vertices.size() * sizeof(graphics::MeshVertex));
    const auto indexBytes = static_cast<std::uint32_t>(mesh.indices.size() * sizeof(std::uint16_t));
    if (vertexBytes == 0 || indexBytes == 0) {
        return nullptr;
    }
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    info.size = vertexBytes;
    made.vertices = SDL_CreateGPUBuffer(m_device, &info);
    info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
    info.size = alignUp(indexBytes, 4); // some APIs copy whole words
    made.indices = SDL_CreateGPUBuffer(m_device, &info);
    if (made.vertices == nullptr || made.indices == nullptr) {
        return nullptr;
    }
    for (const graphics::SubMesh& sub : mesh.subMeshes) {
        made.vertexAlpha.push_back(graphics::vertexAlphaOf(mesh, sub));
    }
    made.bonesNeeded = graphics::bonesNeededBy(mesh);
    m_bufferUploads.push_back(BufferUpload{made.vertices, mesh.vertices.data(), vertexBytes, 0, id.value});
    m_bufferUploads.push_back(BufferUpload{made.indices, mesh.indices.data(), indexBytes, 0, id.value});
    return &m_meshes.put(id.value, 0, m_frame, std::move(made)).value;
}

const GpuScene::GpuTexture* GpuScene::textureFor(graphics::TextureId id, const graphics::TextureData& texture) {
    if (auto* entry = m_textures.find(id.value, m_frame); entry != nullptr) {
        return &entry->value;
    }
    if (texture.levels.empty()) {
        return nullptr;
    }
    // New: every level, RGBA bytes as the store holds them (rows top first, the GPU's own order).
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = texture.levels[0].width;
    info.height = texture.levels[0].height;
    info.layer_count_or_depth = 1;
    info.num_levels = static_cast<Uint32>(texture.levels.size());
    GpuTexture made;
    made.device = m_device;
    made.texture = SDL_CreateGPUTexture(m_device, &info);
    if (made.texture == nullptr) {
        return nullptr;
    }
    made.alpha = graphics::hasAlpha(texture);
    for (std::size_t mip = 0; mip < texture.levels.size(); ++mip) {
        m_levelUploads.push_back(
            LevelUpload{made.texture, &texture.levels[mip], static_cast<std::uint32_t>(mip), 0, id.value});
    }
    return &m_textures.put(id.value, 0, m_frame, std::move(made)).value;
}

void GpuScene::prepare(const graphics::FrameDescription& frame, const graphics::AssetStore& store,
                       std::uint64_t frameNumber, SDL_GPUTexture* white,
                       std::span<const EffectTexture> effectTextures) {
    m_frame = frameNumber;
    m_meshes.evict(m_frame, kKeepFrames);
    m_textures.evict(m_frame, kKeepFrames);
    m_bufferUploads.clear();
    m_levelUploads.clear();
    m_draws.clear();
    m_lineVertices.clear();
    m_effectVertices.clear();
    m_effectIndices.clear();
    m_effectDraws.clear();
    m_white = white;
    // The frame's poses, in one palette for the copy pass; posed draws are left out when it finds no room.
    const bool posesHeld = m_bones.set(graphics::bonePaletteOf(frame.bones()));
    const std::span<const graphics::FramePass> passes = frame.passes();
    m_passDraws.assign(passes.size(), {0, 0});
    m_passLines.assign(passes.size(), LinePass{});
    for (std::size_t p = 0; p < passes.size(); ++p) {
        // An effect pass: its sprites, surfaces and lines through the camera it keeps.
        if (const auto* effects = std::get_if<graphics::EffectPass>(&passes[p]);
            effects != nullptr && effects->camera.has_value()) {
            const std::size_t first = m_draws.size();
            prepareEffects(*effects, *effects->camera, frame, effectTextures);
            m_passDraws[p] = {first, m_draws.size() - first};
            continue;
        }
        const auto* scene = std::get_if<graphics::ScenePass>(&passes[p]);
        if (scene == nullptr || !scene->camera) {
            continue;
        }
        const graphics::SceneCamera& camera = *scene->camera;
        const std::size_t first = m_draws.size();
        for (const graphics::Draw3d& draw : scene->draws) {
            const graphics::MeshData* mesh = store.mesh(draw.mesh);
            const GpuMesh* body = mesh != nullptr ? meshFor(draw.mesh, *mesh) : nullptr;
            if (body == nullptr) {
                continue;
            }
            // A posed draw (a human) is skinned by his pose, or left out with his blood layer rather than stand in
            // his bind pose.
            const graphics::Posing posing = graphics::posingOf(frame, draw, *mesh, body->bonesNeeded);
            if (posing == graphics::Posing::LeftOut || (posing == graphics::Posing::Skinned && !posesHeld)) {
                continue;
            }
            // A mesh views' pass (Draw3d::view) is the mesh again in the view's colours, lit by white alone, with no
            // texture, fog or second layer (docs/engine/gpu-renderer.md#mesh-views).
            if (draw.view == graphics::ViewPass::Flat || draw.view == graphics::ViewPass::Wire) {
                const graphics::DrawColours viewColours = graphics::viewColoursOf(*mesh, frame.viewColoursOf(draw));
                const graphics::SceneStates viewStates = graphics::viewStatesOf(draw.states, draw.view);
                prepareMesh(*body, *mesh, draw, camera, {},
                            MeshLook{.colours = &viewColours,
                                     .textures = {},
                                     .states = &viewStates,
                                     .skinned = posing == graphics::Posing::Skinned,
                                     .view = draw.view},
                            store, white);
                continue;
            }
            // The base mesh, then its dual layer right after it: a world's or object's at the base's first alpha, a
            // human's blood layer in his colours and the blood pass's states. A DualLayer draw is the layer alone,
            // which the engine records after the body's view passes so it covers them as GL3's second atomic does.
            const graphics::DrawColours colours = graphics::coloursOf(*mesh, draw.material);
            if (draw.view != graphics::ViewPass::DualLayer) {
                prepareMesh(*body, *mesh, draw, camera, frame.lightsOf(draw),
                            MeshLook{.colours = &colours,
                                     .textures = frame.texturesOf(draw),
                                     .states = &draw.states,
                                     .skinned = posing == graphics::Posing::Skinned},
                            store, white);
            }
            const graphics::MeshData* layer = store.mesh(draw.dualLayer);
            const GpuMesh* uploadedLayer = layer != nullptr ? meshFor(draw.dualLayer, *layer) : nullptr;
            if (uploadedLayer == nullptr) {
                continue;
            }
            const graphics::Posing layerPosing = graphics::posingOf(frame, draw, *layer, uploadedLayer->bonesNeeded);
            if (layerPosing == graphics::Posing::LeftOut) {
                continue;
            }
            const graphics::DrawColours layerColours = graphics::dualColoursOf(draw, *layer, colours);
            const graphics::SceneStates layerStates = graphics::dualStatesOf(draw);
            prepareMesh(*uploadedLayer, *layer, draw, camera, frame.lightsOf(draw),
                        MeshLook{.colours = &layerColours,
                                 .textures = frame.dualTexturesOf(draw),
                                 .states = &layerStates,
                                 .layer = true,
                                 .skinned = layerPosing == graphics::Posing::Skinned},
                        store, white);
        }
        m_passDraws[p] = {first, m_draws.size() - first};
        prepareLines(p, *scene, camera);
    }
    uploadLines();
    uploadEffects();
}

void GpuScene::prepareMesh(const GpuMesh& uploaded, const graphics::MeshData& mesh, const graphics::Draw3d& draw,
                           const graphics::SceneCamera& camera, std::span<const graphics::SceneLight> lights,
                           const MeshLook& look, const graphics::AssetStore& store, SDL_GPUTexture* white) {
    const graphics::DrawColours& colours = *look.colours;
    const std::span<const graphics::TextureId> textures = look.textures;
    const graphics::SceneStates& states = *look.states;
    // What every sub-mesh of the draw shares: where it is, the camera, its pose, its lights (by its posed sphere when
    // it has one) and the fog.
    VertexUniform shared;
    shared.world = graphics::worldMatrixOf(draw.transform);
    shared.viewProjection = graphics::multiply(graphics::projectionOf(camera), graphics::viewMatrixOf(camera));
    shared.skin = {draw.firstBone, look.skinned ? 1U : 0U, 0U, 0U};
    const graphics::DrawLights lit =
        look.view != graphics::ViewPass::Shaded
            ? graphics::viewLightsOf()
            : graphics::lightsFor(lights, mesh, draw.transform, graphics::boundsOf(draw, mesh), draw.material.lit);
    shared.ambient = four(lit.ambient, 1.0F);
    for (std::size_t i = 0; i < lit.count; ++i) {
        const graphics::DrawLight& light = lit.lights[i];
        // A spot light's cone is hard: RenderWare's SPOT, not its SOFTSPOT (librw's setLights).
        shared.lightParams[i] = {lightType(light.type), light.radius, light.minusCosAngle, 1.0F};
        shared.lightPosition[i] = four(light.position, 1.0F);
        shared.lightDirection[i] = four(light.direction, 0.0F);
        shared.lightColour[i] = four(light.colour, 1.0F);
    }
    shared.fogData = {camera.fogStart, camera.farClip, 1.0F / (camera.fogStart - camera.farClip),
                      states.fog ? 0.0F : 1.0F};
    const std::array<float, 4> fogColour = channels(states.fogColour);

    for (std::size_t s = 0; s < mesh.subMeshes.size(); ++s) {
        const graphics::SubMesh& sub = mesh.subMeshes[s];
        if (sub.indexCount == 0) {
            continue;
        }
        // The material: its colour this draw, its surface and its texture (white for none, or one not held).
        const graphics::Rgba colour =
            sub.material < colours.colours.size() ? colours.colours[sub.material] : graphics::kWhite;
        const graphics::MeshMaterial& material = mesh.materials[sub.material];
        const graphics::TextureId textureId =
            sub.material < textures.size() ? textures[sub.material] : graphics::TextureId{};
        const graphics::TextureData* data = store.texture(textureId);
        const GpuTexture* texture = data != nullptr ? textureFor(textureId, *data) : nullptr;
        VertexUniform vertex = shared;
        vertex.materialColour = channels(colours.modulate ? colour : graphics::kWhite);
        // A mesh views' pass takes the white ambient light whole, as the GL3 pass sets the material's ambient to 1.
        const float ambient = look.view != graphics::ViewPass::Shaded ? 1.0F : material.ambient;
        vertex.surface = {ambient, 0.0F, material.diffuse, mesh.attributes.prelit ? 1.0F : 0.0F};
        const graphics::TextureLevels levels = graphics::textureLevelsOf(data, draw.material.distanceLevels);
        FragmentUniform fragment;
        fragment.levels = {levels.k, levels.scale, levels.byDistance ? 1.0F : 0.0F, levels.gpuChooses ? 1.0F : 0.0F};
        fragment.fogColour = fogColour;
        SDL_GPUSampler* sampler = samplerFor(
            data != nullptr ? data->sampler
                            : graphics::TextureSampler{graphics::TextureFilter::Linear, graphics::TextureAddress::Clamp,
                                                       graphics::TextureAddress::Clamp});
        if (sampler == nullptr) {
            continue;
        }
        // Translucent: vertex alpha, a material colour that is not opaque or a texture with alpha (librw's
        // VERTEXALPHA and its raster's alpha), which turn blending and the alpha test on.
        const bool translucent = uploaded.vertexAlpha[s] || colour.a != 255 || (texture != nullptr && texture->alpha);
        for (const graphics::SceneDrawPass& pass : graphics::drawPassesOf(states, translucent, look.layer)) {
            SDL_GPUGraphicsPipeline* pipeline =
                pipelineFor(PipelineKey{.strip = mesh.topology == graphics::MeshTopology::TriangleStrip,
                                        .blend = pass.blend,
                                        .depthTest = states.depthTest,
                                        .depthWrite = pass.depthWrite,
                                        .cull = states.cull,
                                        .wire = look.view == graphics::ViewPass::Wire});
            if (pipeline == nullptr) {
                continue;
            }
            fragment.alphaLow = pass.alpha.low;
            fragment.alphaHigh = pass.alpha.high;
            m_draws.push_back(SubDraw{pipeline, uploaded.vertices, uploaded.indices, sub.firstIndex, sub.indexCount,
                                      texture != nullptr ? texture->texture : white, sampler, vertex, fragment, 0});
        }
    }
}

void GpuScene::prepareLines(std::size_t pass, const graphics::ScenePass& scene, const graphics::SceneCamera& camera) {
    if (scene.lines.empty()) {
        return;
    }
    // Tested against the depth, never written, never culled or blended: librw's im3d lines in the play mode's debug
    // line list (PlayLevelMode::drawSceneLines() on GL3).
    SDL_GPUGraphicsPipeline* pipeline = pipelineFor(
        PipelineKey{.depthTest = true, .depthWrite = false, .cull = graphics::CullFaces::None, .lines = true});
    if (pipeline == nullptr) {
        return;
    }
    if (m_lineSampler == nullptr) {
        m_lineSampler = samplerFor(graphics::TextureSampler{
            graphics::TextureFilter::Linear, graphics::TextureAddress::Clamp, graphics::TextureAddress::Clamp});
    }
    LinePass& out = m_passLines[pass];
    out.first = m_lineVertices.size();
    out.count = scene.lines.size();
    out.pipeline = pipeline;
    for (const graphics::SceneLineVertex& line : scene.lines) {
        graphics::MeshVertex vertex;
        vertex.position = line.position;
        vertex.prelit = line.colour;
        m_lineVertices.push_back(vertex);
    }
    // The vertex shader takes the prelit colour as it is: no light, a white material, no fog, no pose.
    out.vertex.world = graphics::worldMatrixOf(graphics::Transform3d{});
    out.vertex.viewProjection = graphics::multiply(graphics::projectionOf(camera), graphics::viewMatrixOf(camera));
    out.vertex.materialColour = channels(graphics::kWhite);
    out.vertex.surface = {1.0F, 0.0F, 1.0F, 1.0F};
    out.vertex.fogData = {camera.fogStart, camera.farClip, 1.0F / (camera.fogStart - camera.farClip), 1.0F};
}

void GpuScene::uploadLines() {
    if (m_lineVertices.empty()) {
        return;
    }
    const auto bytes = static_cast<std::uint32_t>(m_lineVertices.size() * sizeof(graphics::MeshVertex));
    if (!growBuffer(m_device, m_lineBuffer, m_lineCapacity, bytes, SDL_GPU_BUFFERUSAGE_VERTEX, kMinLineBuffer)) {
        // A missing part of the picture, as for a mesh SDL cannot make a buffer for.
        m_lineVertices.clear();
        std::fill(m_passLines.begin(), m_passLines.end(), LinePass{});
        return;
    }
    m_bufferUploads.push_back(BufferUpload{m_lineBuffer, m_lineVertices.data(), bytes, 0, 0, true});
}

void GpuScene::prepareEffects(const graphics::EffectPass& effects, const graphics::SceneCamera& camera,
                              const graphics::FrameDescription& frame, std::span<const EffectTexture> textures) {
    // What every draw of the pass shares: no transform (the corners are in the world), the camera, no light, a white
    // material and the corners' own colours taken as the prelighting, as librw's im3d.vert passes them through.
    VertexUniform shared;
    shared.world = graphics::worldMatrixOf(graphics::Transform3d{});
    shared.viewProjection = graphics::multiply(graphics::projectionOf(camera), graphics::viewMatrixOf(camera));
    shared.materialColour = channels(graphics::kWhite);
    shared.surface = {0.0F, 0.0F, 0.0F, 1.0F};
    for (const graphics::EffectDraw& draw : effects.draws) {
        const std::span<const graphics::EffectVertex> corners = frame.effectVerticesOf(draw);
        if (corners.empty()) {
            continue;
        }
        const bool lines = draw.shape == graphics::EffectShape::Lines;
        // Its texture: none for a line list, which librw draws untextured; one the renderer cannot draw leaves the draw
        // out, as a missing part of the picture.
        EffectTexture texture{m_white, false, true};
        if (!lines && draw.texture != graphics::kNoFrameTexture) {
            if (draw.texture >= textures.size() || textures[draw.texture].texture == nullptr) {
                continue;
            }
            texture = textures[draw.texture];
        }
        const graphics::EffectLook look = graphics::effectLookOf(draw, texture.alpha);
        SDL_GPUGraphicsPipeline* pipeline =
            pipelineFor(PipelineKey{.blend = look.blend != graphics::BlendInForce::Replace,
                                    .depthTest = look.depthTest,
                                    .depthWrite = look.depthWrite,
                                    .cull = look.cull,
                                    .lines = lines,
                                    .additive = look.blend == graphics::BlendInForce::Additive});
        // Sampled with the filter the draw sets, else the texture's own (stand-in: GL3 samples with the filter the
        // render states hold, docs/engine/gpu-renderer.md#effects), clamped unless the draw says otherwise.
        const graphics::TextureFilter filter = draw.states.filter.value_or(
            texture.linear ? graphics::TextureFilter::Linear : graphics::TextureFilter::Nearest);
        const graphics::TextureAddress address = draw.states.address.value_or(graphics::TextureAddress::Clamp);
        SDL_GPUSampler* sampler = samplerFor(graphics::TextureSampler{filter, address, address});
        if (pipeline == nullptr || sampler == nullptr) {
            continue;
        }
        VertexUniform vertex = shared;
        vertex.fogData = {camera.fogStart, camera.farClip, 1.0F / (camera.fogStart - camera.farClip),
                          look.fog ? 0.0F : 1.0F};
        FragmentUniform fragment;
        fragment.alphaLow = look.alpha.low;
        fragment.alphaHigh = look.alpha.high;
        fragment.levels = {0.0F, 1.0F, 0.0F, 1.0F}; // the GPU chooses: a 2D texture has one level
        fragment.fogColour = channels(look.fogColour);
        // The corners after those of the draws before it, and its indices (or the corners in order) after theirs.
        const auto firstVertex = static_cast<std::int32_t>(m_effectVertices.size());
        const auto firstIndex = static_cast<std::uint32_t>(m_effectIndices.size());
        for (const graphics::EffectVertex& corner : corners) {
            m_effectVertices.push_back(graphics::meshVertexOf(corner));
        }
        if (draw.indexCount != 0) {
            const std::span<const std::uint16_t> indices = frame.effectIndicesOf(draw);
            m_effectIndices.insert(m_effectIndices.end(), indices.begin(), indices.end());
        } else {
            for (std::size_t i = 0; i < corners.size(); ++i) {
                m_effectIndices.push_back(static_cast<std::uint16_t>(i));
            }
        }
        const auto indexCount = static_cast<std::uint32_t>(m_effectIndices.size()) - firstIndex;
        m_effectDraws.push_back(m_draws.size());
        m_draws.push_back(SubDraw{pipeline, nullptr, nullptr, firstIndex, indexCount, texture.texture, sampler, vertex,
                                  fragment, firstVertex});
    }
}

void GpuScene::uploadEffects() {
    if (m_effectDraws.empty()) {
        return;
    }
    const auto vertexBytes = static_cast<std::uint32_t>(m_effectVertices.size() * sizeof(graphics::MeshVertex));
    const auto indexBytes = static_cast<std::uint32_t>(m_effectIndices.size() * sizeof(std::uint16_t));
    const bool held = growBuffer(m_device, m_effectVertexBuffer, m_effectVertexCapacity, vertexBytes,
                                 SDL_GPU_BUFFERUSAGE_VERTEX, kMinEffectVertexBuffer) &&
                      growBuffer(m_device, m_effectIndexBuffer, m_effectIndexCapacity, alignUp(indexBytes, 4),
                                 SDL_GPU_BUFFERUSAGE_INDEX, kMinEffectIndexBuffer);
    for (const std::size_t at : m_effectDraws) {
        // No buffer: the effects are a missing part of the picture, skipped by draw().
        m_draws[at].vertices = held ? m_effectVertexBuffer : nullptr;
        m_draws[at].indices = held ? m_effectIndexBuffer : nullptr;
        if (!held) {
            m_draws[at].indexCount = 0;
        }
    }
    if (!held) {
        return;
    }
    m_bufferUploads.push_back(BufferUpload{m_effectVertexBuffer, m_effectVertices.data(), vertexBytes, 0, 0, true});
    m_bufferUploads.push_back(BufferUpload{m_effectIndexBuffer, m_effectIndices.data(), indexBytes, 0, 0, true});
}

std::uint32_t GpuScene::layUploads(std::uint32_t offset) {
    std::uint32_t at = offset;
    for (BufferUpload& upload : m_bufferUploads) {
        at = alignUp(at, kBufferAlignment);
        upload.offset = at;
        at += alignUp(upload.size, 4); // an index buffer's copy is whole words
    }
    at = m_bones.lay(at);
    for (LevelUpload& upload : m_levelUploads) {
        at = alignUp(at, kTextureAlignment);
        upload.offset = at;
        at += static_cast<std::uint32_t>(upload.level->texels.size());
    }
    return at;
}

void GpuScene::writeUploads(std::uint8_t* mapped) const {
    for (const BufferUpload& upload : m_bufferUploads) {
        std::memcpy(mapped + upload.offset, upload.bytes, upload.size);
    }
    m_bones.write(mapped);
    for (const LevelUpload& upload : m_levelUploads) {
        std::memcpy(mapped + upload.offset, upload.level->texels.data(), upload.level->texels.size());
    }
}

void GpuScene::recordUploads(SDL_GPUCopyPass* copy, SDL_GPUTransferBuffer* upload) const {
    for (const BufferUpload& bytes : m_bufferUploads) {
        const SDL_GPUTransferBufferLocation from{upload, bytes.offset};
        const SDL_GPUBufferRegion to{bytes.buffer, 0, alignUp(bytes.size, 4)};
        SDL_UploadToGPUBuffer(copy, &from, &to, bytes.cycle);
    }
    m_bones.record(copy, upload);
    for (const LevelUpload& level : m_levelUploads) {
        const SDL_GPUTextureTransferInfo from{upload, level.offset, level.level->width, level.level->height};
        SDL_GPUTextureRegion to{};
        to.texture = level.texture;
        to.mip_level = level.mip;
        to.w = level.level->width;
        to.h = level.level->height;
        to.d = 1;
        SDL_UploadToGPUTexture(copy, &from, &to, false);
    }
}

void GpuScene::forgetUploads() {
    for (const BufferUpload& upload : m_bufferUploads) {
        m_meshes.forget(upload.mesh);
    }
    for (const LevelUpload& upload : m_levelUploads) {
        m_textures.forget(upload.key);
    }
    m_bufferUploads.clear();
    m_levelUploads.clear();
    m_bones.forget();
    m_draws.clear();
    m_lineVertices.clear();
    m_effectVertices.clear();
    m_effectIndices.clear();
    m_effectDraws.clear();
    std::fill(m_passLines.begin(), m_passLines.end(), LinePass{});
    std::fill(m_passDraws.begin(), m_passDraws.end(), std::pair<std::size_t, std::size_t>{0, 0});
}

void GpuScene::draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* target, std::size_t pass) {
    if (pass >= m_passDraws.size()) {
        return;
    }
    const auto [first, count] = m_passDraws[pass];
    SDL_GPUGraphicsPipeline* bound = nullptr;
    for (std::size_t i = first; i < first + count; ++i) {
        const SubDraw& sub = m_draws[i];
        if (sub.indexCount == 0) {
            continue; // an effect draw whose buffers SDL refused
        }
        if (sub.pipeline != bound) {
            SDL_BindGPUGraphicsPipeline(target, sub.pipeline);
            bound = sub.pipeline;
            // The palette with every pipeline: the vertex shader declares it whether or not the draw is posed.
            SDL_GPUBuffer* palette = m_bones.buffer();
            SDL_BindGPUVertexStorageBuffers(target, 0, &palette, 1);
        }
        const SDL_GPUBufferBinding vertices{sub.vertices, 0};
        SDL_BindGPUVertexBuffers(target, 0, &vertices, 1);
        const SDL_GPUBufferBinding indices{sub.indices, 0};
        SDL_BindGPUIndexBuffer(target, &indices, SDL_GPU_INDEXELEMENTSIZE_16BIT);
        const SDL_GPUTextureSamplerBinding binding{sub.texture, sub.sampler};
        SDL_BindGPUFragmentSamplers(target, 0, &binding, 1);
        SDL_PushGPUVertexUniformData(commands, 0, &sub.vertex, sizeof(sub.vertex));
        SDL_PushGPUFragmentUniformData(commands, 0, &sub.fragment, sizeof(sub.fragment));
        SDL_DrawGPUIndexedPrimitives(target, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
    }
    // Then the pass's lines, as a line list from the shared buffer.
    if (pass < m_passLines.size() && m_passLines[pass].count != 0 && m_lineBuffer != nullptr &&
        m_lineSampler != nullptr) {
        const LinePass& lines = m_passLines[pass];
        SDL_BindGPUGraphicsPipeline(target, lines.pipeline);
        SDL_GPUBuffer* palette = m_bones.buffer();
        SDL_BindGPUVertexStorageBuffers(target, 0, &palette, 1);
        const SDL_GPUBufferBinding vertices{m_lineBuffer, 0};
        SDL_BindGPUVertexBuffers(target, 0, &vertices, 1);
        const SDL_GPUTextureSamplerBinding binding{m_white, m_lineSampler};
        SDL_BindGPUFragmentSamplers(target, 0, &binding, 1);
        const FragmentUniform fragment;
        SDL_PushGPUVertexUniformData(commands, 0, &lines.vertex, sizeof(lines.vertex));
        SDL_PushGPUFragmentUniformData(commands, 0, &fragment, sizeof(fragment));
        SDL_DrawGPUPrimitives(target, static_cast<Uint32>(lines.count), 1, static_cast<Uint32>(lines.first), 0);
    }
}

} // namespace coney::platform
