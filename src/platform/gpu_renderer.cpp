// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/gpu_renderer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <utility>

#include <SDL3/SDL.h>
#include <rw.h>

#include "platform/pixel_texture.h"
#include "platform/sprite_sheets.h"

#if defined(CONEY_GPU_SHADERS)
#include "platform/shaders/embedded_shaders.h"
#endif

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

#if defined(CONEY_GPU_SHADERS)
// The shader `name` made for `device` in the first format it takes; null when SDL refuses it.
SDL_GPUShader* makeShader(SDL_GPUDevice* device, std::string_view name) {
    const shaders::Shader* shader = shaders::findShader(name);
    if (shader == nullptr) {
        SDL_SetError("no shader %.*s in the build", static_cast<int>(name.size()), name.data());
        return nullptr;
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
        return SDL_CreateGPUShader(device, &info);
    }
    SDL_SetError("the device takes none of SPIR-V, DXIL and MSL");
    return nullptr;
}
#endif

// The vertex shader's uniform: the target's size in pixels.
struct FrameUniform {
    float width;
    float height;
    float unused[2];
};

// The fragment shader's uniform: the alpha a draw keeps.
struct AlphaUniform {
    float low;
    float high;
    float unused[2];
};

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
    SDL_GPUShader* vertex = makeShader(device, "overlay2d.vert");
    SDL_GPUShader* fragment = vertex != nullptr ? makeShader(device, "overlay2d.frag") : nullptr;
    if (vertex == nullptr || fragment == nullptr) {
        if (vertex != nullptr) {
            SDL_ReleaseGPUShader(device, vertex);
        }
        return failure("its shaders");
    }

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
    renderer->m_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    // The pipeline holds what it needs of the shaders.
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
    if (renderer->m_pipeline == nullptr) {
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
    return renderer;
#endif
}

GpuRenderer::~GpuRenderer() {
    SDL_WaitForGPUIdle(m_device);
    m_textures.clear();
    m_white = OwnedTexture();
    m_target = OwnedTexture();
    for (SDL_GPUSampler* sampler : m_samplers) {
        if (sampler != nullptr) {
            SDL_ReleaseGPUSampler(m_device, sampler);
        }
    }
    if (m_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
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
    if (m_download != nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_download);
    }
}

bool GpuRenderer::ensureTarget(graphics::Extent size) {
    if (m_target.texture != nullptr && m_target.width == size.width && m_target.height == size.height) {
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
    return made != nullptr;
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
                                    graphics::Extent size) {
    ++m_frame;
    m_textures.evict(m_frame, kTextureKeepFrames);
    if (size.width <= 0 || size.height <= 0 || !ensureTarget(size)) {
        return nullptr;
    }

    // 1. The frame's draws as one batch, and the texture each one samples (uploaded this frame when new).
    const graphics::OverlayBatch batch = graphics::batchOverlays(frame);
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

    // 2. Everything uploaded through one transfer buffer: the vertices, the indices, then each texture's pixels.
    const auto vertexBytes = static_cast<std::uint32_t>(batch.vertices.size() * sizeof(graphics::Overlay2dVertex));
    const auto indexBytes = static_cast<std::uint32_t>(batch.indices.size() * sizeof(std::uint32_t));
    std::uint32_t total = alignUp(vertexBytes + indexBytes, kTextureAlignment);
    std::vector<std::uint32_t> uploadOffsets;
    for (const PendingUpload& upload : uploads) {
        uploadOffsets.push_back(total);
        total = alignUp(total + static_cast<std::uint32_t>(upload.pixels.size()), kTextureAlignment);
    }
    const bool drawsSomething = vertexBytes > 0 && indexBytes > 0;
    if (drawsSomething && (!ensureBuffer(m_vertices, m_vertexCapacity, vertexBytes, SDL_GPU_BUFFERUSAGE_VERTEX) ||
                           !ensureBuffer(m_indices, m_indexCapacity, indexBytes, SDL_GPU_BUFFERUSAGE_INDEX))) {
        // No buffer to draw from: forget the pixels this frame meant to upload, as a failed upload buffer does.
        forgetUploads(uploads);
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
            forgetUploads(uploads);
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
        SDL_EndGPUCopyPass(copy);
    }

    // 3. One render pass: the window cleared as the frame starts, then every draw in order.
    const graphics::Rgba clear = graphics::windowClearOf(frame.start());
    SDL_GPUColorTargetInfo target{};
    target.texture = m_target.texture;
    target.clear_color = SDL_FColor{static_cast<float>(clear.r) / 255.0F, static_cast<float>(clear.g) / 255.0F,
                                    static_cast<float>(clear.b) / 255.0F, static_cast<float>(clear.a) / 255.0F};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    target.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    if (pass == nullptr) {
        return nullptr;
    }
    if (drawsSomething) {
        SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
        const FrameUniform frameUniform{static_cast<float>(size.width), static_cast<float>(size.height), {0, 0}};
        SDL_PushGPUVertexUniformData(commands, 0, &frameUniform, sizeof(frameUniform));
        const SDL_GPUBufferBinding vertices{m_vertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertices, 1);
        const SDL_GPUBufferBinding indices{m_indices, 0};
        SDL_BindGPUIndexBuffer(pass, &indices, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        for (std::size_t i = 0; i < batch.draws.size(); ++i) {
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
    SDL_EndGPURenderPass(pass);
    return m_target.texture;
}

bool GpuRenderer::recordReadBack(SDL_GPUCommandBuffer* commands) {
    if (m_target.texture == nullptr) {
        return false;
    }
    const auto bytes = static_cast<std::uint32_t>(m_target.width) * static_cast<std::uint32_t>(m_target.height) * 4;
    if (m_download == nullptr || m_downloadCapacity < bytes) {
        if (m_download != nullptr) {
            SDL_ReleaseGPUTransferBuffer(m_device, m_download);
        }
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        info.size = bytes;
        m_download = SDL_CreateGPUTransferBuffer(m_device, &info);
        m_downloadCapacity = m_download != nullptr ? bytes : 0;
        if (m_download == nullptr) {
            return false;
        }
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUTextureRegion from{};
    from.texture = m_target.texture;
    from.w = static_cast<Uint32>(m_target.width);
    from.h = static_cast<Uint32>(m_target.height);
    from.d = 1;
    const SDL_GPUTextureTransferInfo to{m_download, 0, static_cast<Uint32>(m_target.width),
                                        static_cast<Uint32>(m_target.height)};
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    m_downloadSize = graphics::Extent{m_target.width, m_target.height};
    return true;
}

std::vector<std::uint8_t> GpuRenderer::readBack() {
    const graphics::Extent size = std::exchange(m_downloadSize, graphics::Extent{});
    if (m_download == nullptr || size.width <= 0 || size.height <= 0) {
        return {};
    }
    const std::size_t bytes = static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height) * 4;
    const auto* mapped = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(m_device, m_download, false));
    if (mapped == nullptr) {
        return {};
    }
    // SDL's textures count rows from the top on every backend, as images do.
    std::vector<std::uint8_t> rgba(mapped, mapped + bytes);
    SDL_UnmapGPUTransferBuffer(m_device, m_download);
    return rgba;
}

} // namespace coney::platform
