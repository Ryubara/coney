// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_renderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <variant>

#include <SDL3/SDL.h>
#include <rw.h>

#include "platform/gpu_scene.h"
#include "platform/gpu_shader.h"
#include "platform/gpu_uniforms.h"
#include "platform/pixel_texture.h"
#include "platform/sprite_sheets.h"

namespace coney::platform {

namespace {

// What the target is drawn in: 8 bits a channel, no sRGB conversion, as the GL3 renderer's default frame buffer.
constexpr SDL_GPUTextureFormat kTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
// Where in the upload buffer a texture's pixels start: Direct3D 12 places texture data at multiples of 512 bytes.
constexpr std::uint32_t kTextureAlignment = 512;

// The failure of a step of create(), with SDL's error text.
std::unexpected<Error> failure(const std::string& what) {
    std::string message = "could not set up the GPU renderer's 2D drawing: " + what;
    if (const char* sdl = SDL_GetError(); sdl != nullptr && *sdl != '\0') {
        message += std::string(": ") + sdl;
    }
    return std::unexpected(Error{ErrorCode::PlatformFailure, std::move(message)});
}

// `value` rounded up to a multiple of `step`.
std::uint32_t alignUp(std::uint32_t value, std::uint32_t step) { return (value + step - 1) / step * step; }

// Whether a texture of `filter` blends the four nearest texels: RenderWare's filters but the two that take the nearest
// texel. The 2D side's textures have no mip levels the GPU renderer uploads (stand-in: GL3 would sample a sheet's own
// levels for a MIP filter; 2D draws are near their texture's size, where level 0 is the one taken).
bool blends(graphics::TextureFilter filter) {
    return filter != graphics::TextureFilter::Nearest && filter != graphics::TextureFilter::MipNearest;
}

// RenderWare's filter of a librw texture as Coney's (rw::Texture::FilterMode counts from NEAREST = 1).
graphics::TextureFilter filterOf(rw::Texture& texture) {
    switch (texture.getFilter()) {
    case rw::Texture::NEAREST:
        return graphics::TextureFilter::Nearest;
    case rw::Texture::MIPNEAREST:
        return graphics::TextureFilter::MipNearest;
    case rw::Texture::MIPLINEAR:
        return graphics::TextureFilter::MipLinear;
    case rw::Texture::LINEARMIPNEAREST:
        return graphics::TextureFilter::LinearMipNearest;
    case rw::Texture::LINEARMIPLINEAR:
        return graphics::TextureFilter::LinearMipLinear;
    default:
        return graphics::TextureFilter::Linear;
    }
}

} // namespace

// Gives the texture back to the device, unless it was moved away.
GpuRenderer::OwnedTexture::~OwnedTexture() {
    if (texture != nullptr) {
        SDL_ReleaseGPUTexture(device, texture); // SDL waits for the GPU to finish with it
    }
}

// Takes the other's texture, leaving it empty so only one owner releases it.
GpuRenderer::OwnedTexture::OwnedTexture(OwnedTexture&& other) noexcept
    : device(other.device), texture(std::exchange(other.texture, nullptr)), width(other.width), height(other.height),
      linear(other.linear) {}

// Releases this texture, then takes the other's, as the move constructor does.
GpuRenderer::OwnedTexture& GpuRenderer::OwnedTexture::operator=(OwnedTexture&& other) noexcept {
    if (this != &other) {
        if (texture != nullptr) {
            SDL_ReleaseGPUTexture(device, texture);
        }
        device = other.device;
        texture = std::exchange(other.texture, nullptr);
        width = other.width;
        height = other.height;
        linear = other.linear;
    }
    return *this;
}

std::expected<std::unique_ptr<GpuRenderer>, Error> GpuRenderer::create(SDL_GPUDevice* device) {
#if !defined(CONEY_GPU_SHADERS)
    (void)device;
    return std::unexpected(Error{ErrorCode::PlatformFailure,
                                 "the GPU renderer's 2D drawing needs the shaders, and this build has none "
                                 "(-DCONEY_SHADERS=OFF)"});
#else
    std::unique_ptr<GpuRenderer> renderer(new GpuRenderer(device));
    auto madeVertex = makeShader(device, "overlay2d.vert");
    auto madeFragment = madeVertex ? makeShader(device, "overlay2d.frag") : std::unexpected(madeVertex.error());
    if (!madeVertex || !madeFragment) {
        if (madeVertex) {
            SDL_ReleaseGPUShader(device, *madeVertex);
        }
        return failure("its shaders");
    }
    SDL_GPUShader* vertex = *madeVertex;
    SDL_GPUShader* fragment = *madeFragment;
    // The depth buffer the scene passes test against: 24 bits where the device has them, as OpenGL's default frame
    // buffer usually gives, else a 32-bit float.
    renderer->m_depthFormat =
        SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTURETYPE_2D,
                                     SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)
            ? SDL_GPU_TEXTUREFORMAT_D24_UNORM
            : SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

    // One vertex buffer of Overlay2dVertex: the position and texture coordinate as floats, the colour as 8-bit
    // channels.
    const SDL_GPUVertexBufferDescription buffer{0, sizeof(graphics::Overlay2dVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX,
                                                0};
    const std::array<SDL_GPUVertexAttribute, 3> attributes{{
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(graphics::Overlay2dVertex, x)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(graphics::Overlay2dVertex, u)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, offsetof(graphics::Overlay2dVertex, colour)},
    }};
    // The 2D pass's states (docs/research/graphics.md#2d-drawing): no depth, no culling, blended by source alpha over
    // what is below, the alpha channel by the same factors as librw's glBlendFunc blends it.
    SDL_GPUColorTargetDescription target{};
    target.format = kTargetFormat;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes.data();
    info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &target;
    info.target_info.num_color_targets = 1;
    // The render pass has the scene's depth buffer, which the 2D pass neither tests nor writes.
    info.target_info.depth_stencil_format = static_cast<SDL_GPUTextureFormat>(renderer->m_depthFormat);
    info.target_info.has_depth_stencil_target = true;
    renderer->m_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    // The same with no blending, which writes what the shader gives as the GL3 renderer's opaque screen quads do
    // (ONE / ZERO): the blur pulse's.
    target.blend_state.enable_blend = false;
    renderer->m_opaquePipeline =
        renderer->m_pipeline != nullptr ? SDL_CreateGPUGraphicsPipeline(device, &info) : nullptr;
    // The pipelines hold what they need of the shaders.
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
    if (renderer->m_pipeline == nullptr || renderer->m_opaquePipeline == nullptr) {
        return failure("its pipeline");
    }

    // The samplers: nearest or linear, clamped at the edges or repeated, with no mip levels.
    for (int i = 0; i < 4; ++i) {
        const bool linear = i >= 2;
        const bool repeat = (i % 2) == 1;
        SDL_GPUSamplerCreateInfo sampler{};
        sampler.min_filter = linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        sampler.mag_filter = sampler.min_filter;
        sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        const SDL_GPUSamplerAddressMode address =
            repeat ? SDL_GPU_SAMPLERADDRESSMODE_REPEAT : SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler.address_mode_u = address;
        sampler.address_mode_v = address;
        sampler.address_mode_w = address;
        renderer->m_samplers[i] = SDL_CreateGPUSampler(device, &sampler);
        if (renderer->m_samplers[i] == nullptr) {
            return failure("its samplers");
        }
    }

    // Flat colour samples one white texel, as librw's white texture in the GL3 renderer.
    SDL_GPUTextureCreateInfo white{};
    white.type = SDL_GPU_TEXTURETYPE_2D;
    white.format = kTargetFormat;
    white.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    white.width = 1;
    white.height = 1;
    white.layer_count_or_depth = 1;
    white.num_levels = 1;
    SDL_GPUTexture* made = SDL_CreateGPUTexture(device, &white);
    if (made == nullptr) {
        return failure("its white texture");
    }
    renderer->m_white = OwnedTexture(device, made, 1, 1, false);

    // The scene passes' drawing: without it the 2D passes still draw, and the reason is kept.
    if (auto scene = GpuScene::create(device, static_cast<std::uint32_t>(kTargetFormat), renderer->m_depthFormat);
        scene) {
        renderer->m_scene = std::move(*scene);
    } else {
        renderer->m_sceneProblem = scene.error().message;
    }
    return renderer;
#endif
}

GpuRenderer::~GpuRenderer() {
    SDL_WaitForGPUIdle(m_device);
    m_scene.reset();
    m_textures.clear();
    m_viewCopy = OwnedTexture();
    m_halfCopy = OwnedTexture();
    m_kept.clear();
    m_white = OwnedTexture();
    m_target = OwnedTexture();
    m_depth = OwnedTexture();
    for (SDL_GPUSampler* sampler : m_samplers) {
        if (sampler != nullptr) {
            SDL_ReleaseGPUSampler(m_device, sampler);
        }
    }
    if (m_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
    }
    if (m_opaquePipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_opaquePipeline);
    }
    if (m_vertices != nullptr) {
        SDL_ReleaseGPUBuffer(m_device, m_vertices);
    }
    if (m_indices != nullptr) {
        SDL_ReleaseGPUBuffer(m_device, m_indices);
    }
    if (m_upload != nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_upload);
    }
    for (const Download& download : m_downloads) {
        if (download.buffer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(m_device, download.buffer);
        }
    }
}

bool GpuRenderer::ensureTarget(graphics::Extent size) {
    if (m_target.texture != nullptr && m_depth.texture != nullptr && m_target.width == size.width &&
        m_target.height == size.height) {
        return true;
    }
    // A colour target the presenter blits from (which needs SAMPLER) and reads back.
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = kTargetFormat;
    info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(size.width);
    info.height = static_cast<Uint32>(size.height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    SDL_GPUTexture* made = SDL_CreateGPUTexture(m_device, &info);
    m_target = OwnedTexture(m_device, made, size.width, size.height, false);
    // Its depth buffer, the same size.
    info.format = static_cast<SDL_GPUTextureFormat>(m_depthFormat);
    info.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    SDL_GPUTexture* depth = made != nullptr ? SDL_CreateGPUTexture(m_device, &info) : nullptr;
    m_depth = OwnedTexture(m_device, depth, size.width, size.height, false);
    if (depth == nullptr) {
        m_target = OwnedTexture();
        return false;
    }
    return true;
}

bool GpuRenderer::ensureBuffer(SDL_GPUBuffer*& buffer, std::uint32_t& capacity, std::uint32_t bytes,
                               std::uint32_t usage) {
    if (buffer != nullptr && capacity >= bytes) {
        return true;
    }
    std::uint32_t size = std::max<std::uint32_t>(capacity, 4096);
    while (size < bytes) {
        size *= 2;
    }
    if (buffer != nullptr) {
        SDL_ReleaseGPUBuffer(m_device, buffer);
    }
    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = size;
    buffer = SDL_CreateGPUBuffer(m_device, &info);
    capacity = buffer != nullptr ? size : 0;
    return buffer != nullptr;
}

bool GpuRenderer::ensureUploadBuffer(std::uint32_t bytes) {
    if (m_upload != nullptr && m_uploadCapacity >= bytes) {
        return true;
    }
    std::uint32_t size = std::max<std::uint32_t>(m_uploadCapacity, 65536);
    while (size < bytes) {
        size *= 2;
    }
    if (m_upload != nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_upload);
    }
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = size;
    m_upload = SDL_CreateGPUTransferBuffer(m_device, &info);
    m_uploadCapacity = m_upload != nullptr ? size : 0;
    return m_upload != nullptr;
}

SDL_GPUSampler* GpuRenderer::samplerFor(bool linear, graphics::TextureAddress address) const {
    // The 2D side records only Clamp and Wrap; a mirrored or bordered texture would be clamped (stand-in, none is
    // drawn).
    const bool repeat = address == graphics::TextureAddress::Wrap;
    return m_samplers[(linear ? 2 : 0) + (repeat ? 1 : 0)];
}

std::vector<GpuScene::EffectTexture> GpuRenderer::effectTexturesOf(const graphics::FrameDescription& frame,
                                                                   std::vector<PendingUpload>& uploads) {
    std::vector<GpuScene::EffectTexture> textures(frame.textureCount() + 1);
    std::vector<bool> asked(textures.size(), false);
    for (const graphics::FramePass& pass : frame.passes()) {
        const auto* effects = std::get_if<graphics::EffectPass>(&pass);
        if (effects == nullptr) {
            continue;
        }
        for (const graphics::EffectDraw& draw : effects->draws) {
            const graphics::FrameTextureId id = draw.texture;
            if (id == graphics::kNoFrameTexture || id >= textures.size() || asked[id]) {
                continue;
            }
            asked[id] = true;
            const graphics::Texture* texture = frame.textureOf(id);
            if (const OwnedTexture* owned = texture != nullptr ? textureFor(texture, uploads) : nullptr;
                owned != nullptr) {
                textures[id] = GpuScene::EffectTexture{owned->texture, owned->alpha, owned->linear};
            }
        }
    }
    return textures;
}

const GpuRenderer::OwnedTexture* GpuRenderer::textureFor(const graphics::Texture* texture,
                                                         std::vector<PendingUpload>& uploads) {
    // What the texture is: a frame kept as pixels (a movie's, a test card's) or a sprite sheet read through librw.
    const auto* pixels = dynamic_cast<const PixelTexture*>(texture);
    const auto* sheet = pixels == nullptr ? dynamic_cast<const SheetTexture*>(texture) : nullptr;
    if (pixels == nullptr && sheet == nullptr) {
        return nullptr;
    }
    const std::uint64_t revision = pixels != nullptr ? pixels->revision() : 0;
    auto* entry = m_textures.find(texture->serial(), m_frame);
    if (entry != nullptr && entry->revision == revision && entry->value.texture != nullptr) {
        return &entry->value;
    }

    // New, or new pixels: read them, and make the GPU's texture again when the size changed.
    PendingUpload upload;
    bool linear = true;
    if (pixels != nullptr) {
        upload.pixels.assign(pixels->pixels().begin(), pixels->pixels().end());
        upload.width = pixels->width();
        upload.height = pixels->height();
        linear = blends(pixels->filter());
    } else {
        auto image = sheet->toImage();
        if (!image) {
            return nullptr;
        }
        upload.pixels = std::move(image->pixels);
        upload.width = image->width;
        upload.height = image->height;
        linear = sheet->rwTexture() == nullptr || blends(filterOf(*sheet->rwTexture()));
    }
    if (upload.width <= 0 || upload.height <= 0 ||
        upload.pixels.size() < static_cast<std::size_t>(upload.width) * static_cast<std::size_t>(upload.height) * 4) {
        return nullptr;
    }
    if (entry == nullptr || entry->value.width != upload.width || entry->value.height != upload.height ||
        entry->value.texture == nullptr) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = kTargetFormat;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(upload.width);
        info.height = static_cast<Uint32>(upload.height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        SDL_GPUTexture* made = SDL_CreateGPUTexture(m_device, &info);
        if (made == nullptr) {
            return nullptr;
        }
        entry = &m_textures.put(texture->serial(), revision, m_frame,
                                OwnedTexture(m_device, made, upload.width, upload.height, linear));
    } else {
        entry->revision = revision;
        entry->value.linear = linear;
    }
    // Whether a texel is not opaque, as librw reads an image when it makes a raster of it (Image::hasAlpha).
    entry->value.alpha = false;
    for (std::size_t at = 3; at < upload.pixels.size(); at += 4) {
        if (upload.pixels[at] != 255) {
            entry->value.alpha = true;
            break;
        }
    }
    upload.texture = entry->value.texture;
    upload.serial = texture->serial();
    uploads.push_back(std::move(upload));
    return &entry->value;
}

// Treats this frame's pending uploads as never made, so the next frame uploads the white texel and each texture's
// pixels again rather than drawing them from pixels the GPU never got.
void GpuRenderer::forgetUploads(const std::vector<PendingUpload>& uploads) {
    for (const PendingUpload& upload : uploads) {
        if (upload.texture == m_white.texture) {
            m_whiteUploaded = false;
        } else {
            m_textures.forget(upload.serial);
        }
    }
}

std::uint32_t GpuRenderer::targetFormat() { return static_cast<std::uint32_t>(kTargetFormat); }

SDL_GPUTexture* GpuRenderer::record(SDL_GPUCommandBuffer* commands, const graphics::FrameDescription& frame,
                                    graphics::Extent size, const graphics::AssetStore* store) {
    ++m_frame;
    m_textures.evict(m_frame, kTextureKeepFrames);
    if (size.width <= 0 || size.height <= 0 || !ensureTarget(size)) {
        return nullptr;
    }

    // 1. The frame's 2D draws as one batch, and the texture each one samples (uploaded this frame when new); the screen
    // passes' quads after them; then the scene passes' draws, with their meshes and textures.
    graphics::OverlayBatch batch = graphics::batchOverlays(frame);
    const std::vector<ScreenPlan> screenPlans = planScreenPasses(frame, size, batch);
    std::vector<PendingUpload> uploads;
    if (!m_whiteUploaded) {
        uploads.push_back(PendingUpload{m_white.texture, {255, 255, 255, 255}, 1, 1});
        m_whiteUploaded = true;
    }
    std::vector<const OwnedTexture*> drawTextures(batch.draws.size(), nullptr);
    for (std::size_t i = 0; i < batch.draws.size(); ++i) {
        const graphics::Texture* texture = frame.textureOf(batch.draws[i].texture);
        drawTextures[i] = texture == nullptr ? &m_white : textureFor(texture, uploads);
    }
    GpuScene* scene = store != nullptr ? m_scene.get() : nullptr;
    if (scene != nullptr) {
        scene->prepare(frame, *store, m_frame, m_white.texture, effectTexturesOf(frame, uploads));
    }

    // 2. Everything uploaded through one transfer buffer: the vertices, the indices, each texture's pixels, then the
    // scene's new meshes and textures.
    const auto vertexBytes = static_cast<std::uint32_t>(batch.vertices.size() * sizeof(graphics::Overlay2dVertex));
    const auto indexBytes = static_cast<std::uint32_t>(batch.indices.size() * sizeof(std::uint32_t));
    std::uint32_t total = alignUp(vertexBytes + indexBytes, kTextureAlignment);
    std::vector<std::uint32_t> uploadOffsets;
    for (const PendingUpload& upload : uploads) {
        uploadOffsets.push_back(total);
        total = alignUp(total + static_cast<std::uint32_t>(upload.pixels.size()), kTextureAlignment);
    }
    if (scene != nullptr) {
        total = scene->layUploads(total);
    }
    // Forgets what this frame meant to upload, when nothing of it reached the GPU, and the frames the motion blur
    // meant to keep, which it now never draws.
    const auto forget = [this, &uploads, scene, &screenPlans] {
        forgetUploads(uploads);
        if (scene != nullptr) {
            scene->forgetUploads();
        }
        for (const ScreenPlan& plan : screenPlans) {
            if (plan.copy != 0) {
                keptFrame(plan.copy).kept = false;
            }
        }
    };
    const bool drawsSomething = vertexBytes > 0 && indexBytes > 0;
    if (drawsSomething && (!ensureBuffer(m_vertices, m_vertexCapacity, vertexBytes, SDL_GPU_BUFFERUSAGE_VERTEX) ||
                           !ensureBuffer(m_indices, m_indexCapacity, indexBytes, SDL_GPU_BUFFERUSAGE_INDEX))) {
        // No buffer to draw from: forget the pixels this frame meant to upload, as a failed upload buffer does.
        forget();
        return nullptr;
    }
    if (total > 0) {
        // Cycled: a buffer the GPU still reads for the last frame is not written over.
        auto* bytes = ensureUploadBuffer(total)
                          ? static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(m_device, m_upload, true))
                          : nullptr;
        if (bytes == nullptr) {
            // Nothing reached the GPU: forget what this frame meant to upload, so the next frame uploads it again
            // rather than drawing it from pixels it never got, and draw nothing from the last frame's buffers.
            forget();
            return nullptr;
        }
        if (vertexBytes > 0) {
            std::memcpy(bytes, batch.vertices.data(), vertexBytes);
        }
        if (indexBytes > 0) {
            std::memcpy(bytes + vertexBytes, batch.indices.data(), indexBytes);
        }
        for (std::size_t i = 0; i < uploads.size(); ++i) {
            std::memcpy(bytes + uploadOffsets[i], uploads[i].pixels.data(), uploads[i].pixels.size());
        }
        if (scene != nullptr) {
            scene->writeUploads(bytes);
        }
        SDL_UnmapGPUTransferBuffer(m_device, m_upload);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (drawsSomething) {
            const SDL_GPUTransferBufferLocation vertexFrom{m_upload, 0};
            const SDL_GPUBufferRegion vertexTo{m_vertices, 0, vertexBytes};
            SDL_UploadToGPUBuffer(copy, &vertexFrom, &vertexTo, true);
            const SDL_GPUTransferBufferLocation indexFrom{m_upload, vertexBytes};
            const SDL_GPUBufferRegion indexTo{m_indices, 0, indexBytes};
            SDL_UploadToGPUBuffer(copy, &indexFrom, &indexTo, true);
        }
        for (std::size_t i = 0; i < uploads.size(); ++i) {
            const SDL_GPUTextureTransferInfo from{m_upload, uploadOffsets[i], static_cast<Uint32>(uploads[i].width),
                                                  static_cast<Uint32>(uploads[i].height)};
            SDL_GPUTextureRegion to{};
            to.texture = uploads[i].texture;
            to.w = static_cast<Uint32>(uploads[i].width);
            to.h = static_cast<Uint32>(uploads[i].height);
            to.d = 1;
            SDL_UploadToGPUTexture(copy, &from, &to, true);
        }
        if (scene != nullptr) {
            scene->recordUploads(copy, m_upload);
        }
        SDL_EndGPUCopyPass(copy);
    }

    // 3. One render pass: the window cleared as the frame starts and the depth buffer to the far plane, then every
    // pass in order, the 2D passes' draws and the scene passes' between them; a scene pass that clears the depth
    // starts a render pass of its own, and a screen pass ends the render pass for each copy it makes, the next draw
    // starting another that keeps the colour and the depth drawn so far.
    const graphics::Rgba clear = graphics::windowClearOf(frame.start());
    SDL_GPUColorTargetInfo target{};
    target.texture = m_target.texture;
    target.clear_color = SDL_FColor{static_cast<float>(clear.r) / 255.0F, static_cast<float>(clear.g) / 255.0F,
                                    static_cast<float>(clear.b) / 255.0F, static_cast<float>(clear.a) / 255.0F};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.cycle = true;
    SDL_GPUDepthStencilTargetInfo depth{};
    depth.texture = m_depth.texture;
    depth.clear_depth = 1.0F;
    depth.load_op = SDL_GPU_LOADOP_CLEAR;
    // Kept across a screen pass's copies, for what is drawn after it.
    depth.store_op = screenPlans.empty() ? SDL_GPU_STOREOP_DONT_CARE : SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depth.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, &depth);
    if (pass == nullptr) {
        return nullptr;
    }
    // Starts a render pass that keeps the colour and, unless `clearDepth`, the depth drawn so far.
    const auto resume = [&](bool clearDepth) {
        target.load_op = SDL_GPU_LOADOP_LOAD;
        target.cycle = false;
        depth.load_op = clearDepth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        depth.cycle = false;
        pass = SDL_BeginGPURenderPass(commands, &target, 1, &depth);
        return pass != nullptr;
    };
    // Binds the 2D pipeline (`opaque`: the one with no blending) and its buffers, since a scene pass may have bound
    // its own.
    const auto bind2d = [&](bool opaque) {
        SDL_BindGPUGraphicsPipeline(pass, opaque ? m_opaquePipeline : m_pipeline);
        const FrameUniform frameUniform{static_cast<float>(size.width), static_cast<float>(size.height), {0, 0}};
        SDL_PushGPUVertexUniformData(commands, 0, &frameUniform, sizeof(frameUniform));
        const SDL_GPUBufferBinding vertices{m_vertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
        const SDL_GPUBufferBinding indices{m_indices, 0};
        SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    };
    std::size_t nextPlan = 0;
    const std::span<const graphics::FramePass> passes = frame.passes();
    for (std::size_t p = 0; p < passes.size(); ++p) {
        // A screen pass: its copies and draws in order (the frame's last pass may leave the render pass ended).
        if (std::holds_alternative<graphics::ScreenPass>(passes[p])) {
            if (nextPlan < screenPlans.size() && screenPlans[nextPlan].pass == p) {
                const ScreenPlan& plan = screenPlans[nextPlan++];
                if (!recordScreenPass(commands, plan, pass, [&] { return resume(false); }, bind2d)) {
                    return nullptr;
                }
            }
            continue;
        }
        if (pass == nullptr && !resume(false)) {
            return nullptr;
        }
        // An effect pass: drawn by the scene drawing, in the depth the scene passes left.
        if (std::holds_alternative<graphics::EffectPass>(passes[p])) {
            if (scene != nullptr) {
                scene->draw(commands, pass, p);
            }
            continue;
        }
        if (const auto* scenePass = std::get_if<graphics::ScenePass>(&passes[p]); scenePass != nullptr) {
            if (scenePass->clearDepth) {
                // The depth alone cleared to the far plane before the pass (the Z-only clear after the level's
                // background): a render pass of its own that keeps the colour drawn so far.
                SDL_EndGPURenderPass(pass);
                if (!resume(true)) {
                    return nullptr;
                }
            }
            if (scene != nullptr) {
                scene->draw(commands, pass, p);
            }
            continue;
        }
        const std::uint32_t first = p < batch.passDraws.size() ? batch.passDraws[p] : 0;
        const std::uint32_t end = p + 1 < batch.passDraws.size() ? batch.passDraws[p + 1] : first;
        if (!drawsSomething || first == end) {
            continue;
        }
        bind2d(false);
        for (std::uint32_t i = first; i < end; ++i) {
            const graphics::OverlayDraw& draw = batch.draws[i];
            const OwnedTexture* texture = drawTextures[i];
            if (texture == nullptr) {
                continue; // a texture it cannot draw: left out, as a missing part of the picture
            }
            const SDL_GPUTextureSamplerBinding binding{texture->texture, samplerFor(texture->linear, draw.address)};
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            const AlphaUniform alpha{draw.alpha.low, draw.alpha.high, {0, 0}};
            SDL_PushGPUFragmentUniformData(commands, 0, &alpha, sizeof(alpha));
            SDL_DrawGPUIndexedPrimitives(pass, draw.indexCount, 1, draw.firstIndex, 0, 0);
        }
    }
    if (pass != nullptr) {
        SDL_EndGPURenderPass(pass);
    }
    return m_target.texture;
}

bool GpuRenderer::recordReadBack(SDL_GPUCommandBuffer* commands, ReadBack at) {
    if (m_target.texture == nullptr) {
        return false;
    }
    Download& download = m_downloads[static_cast<std::size_t>(at)];
    const auto bytes = static_cast<std::uint32_t>(m_target.width) * static_cast<std::uint32_t>(m_target.height) * 4;
    if (download.buffer == nullptr || download.capacity < bytes) {
        if (download.buffer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(m_device, download.buffer);
        }
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = bytes;
        download.buffer = SDL_CreateGPUTransferBuffer(m_device, &info);
        download.capacity = download.buffer != nullptr ? bytes : 0;
        if (download.buffer == nullptr) {
            return false;
        }
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureRegion from{};
    from.texture = m_target.texture;
    from.w = static_cast<Uint32>(m_target.width);
    from.h = static_cast<Uint32>(m_target.height);
    from.d = 1;
    const SDL_GPUTextureTransferInfo to{download.buffer, 0, static_cast<Uint32>(m_target.width),
                                        static_cast<Uint32>(m_target.height)};
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    download.size = graphics::Extent{m_target.width, m_target.height};
    return true;
}

std::vector<std::uint8_t> GpuRenderer::readBack(ReadBack at) {
    Download& download = m_downloads[static_cast<std::size_t>(at)];
    const graphics::Extent size = std::exchange(download.size, graphics::Extent{});
    if (download.buffer == nullptr || size.width <= 0 || size.height <= 0) {
        return {};
    }
    const std::size_t bytes = static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4;
    const auto* mapped = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(m_device, download.buffer, false));
    if (mapped == nullptr) {
        return {};
    }
    // SDL's textures count rows from the top on every backend, as images do.
    std::vector<std::uint8_t> rgba(mapped, mapped + bytes);
    SDL_UnmapGPUTransferBuffer(m_device, download.buffer);
    return rgba;
}

} // namespace coney::platform
