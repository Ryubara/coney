// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sandbox_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <system_error>
#include <vector>

#include <rw.h>

namespace coney::platform {

namespace {

// librw's GL3 index buffers are 16-bit, so one geometry holds at most this many vertices.
constexpr std::size_t kMaxBatchVertices = 65535;
// The most anisotropic filtering asked for: plenty for a floor seen edge-on, and every desktop GPU offers it.
constexpr rw::int32 kMaxAnisotropy = 8;
// A texture raster of 32-bit RGBA with room for its mip levels. librw declares the type, format and flag bits in
// separate enums, so they are combined as plain integers.
constexpr rw::int32 kRasterFormat = static_cast<rw::int32>(rw::Raster::TEXTURE) |
                                    static_cast<rw::int32>(rw::Raster::C8888) |
                                    static_cast<rw::int32>(rw::Raster::MIPMAP);

// A colour channel from 0-1 to a byte, rounded.
std::uint8_t channel(float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

// One mip level, RGBA bytes row by row.
struct MipLevel {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// The next smaller mip level of `level`: each pixel the average of the 2 × 2 (or 2 × 1 at a 1-wide edge) above it.
MipLevel halve(const MipLevel& level) {
    MipLevel out{std::max(1, level.width / 2), std::max(1, level.height / 2), {}};
    out.pixels.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const int x0 = std::min(x * 2, level.width - 1);
            const int x1 = std::min(x * 2 + 1, level.width - 1);
            const int y0 = std::min(y * 2, level.height - 1);
            const int y1 = std::min(y * 2 + 1, level.height - 1);
            for (int c = 0; c < 4; ++c) {
                const auto at = [&level, c](int px, int py) {
                    return static_cast<int>(level.pixels[(static_cast<std::size_t>(py) * level.width + px) * 4 + c]);
                };
                const int sum = at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1);
                out.pixels[(static_cast<std::size_t>(y) * out.width + x) * 4 + c] =
                    static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

// Reads one PNG into a mipmapped, repeating, trilinear texture. The mip levels are made here: librw's GL3 device can
// generate them (AUTOMIPMAP), but then limits the texture to its first level, so its smaller levels are never used.
std::expected<rw::Texture*, Error> loadTexture(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return fail(ErrorCode::NotFound, std::format("the sandbox texture {} is missing", path.string()));
    }
    rw::Image* image = rw::readPNG(path.string().c_str());
    if (image == nullptr) {
        return fail(ErrorCode::Invalid,
                    std::format("the sandbox texture {} is not a PNG librw can read", path.string()));
    }
    // 1. The image as 32-bit RGBA: the first mip level.
    image->convertTo32();
    MipLevel level{image->width, image->height, {}};
    level.pixels.resize(static_cast<std::size_t>(level.width) * level.height * 4);
    for (int y = 0; y < level.height; ++y) {
        const std::uint8_t* row = image->pixels + static_cast<std::ptrdiff_t>(y) * image->stride;
        std::copy(row, row + static_cast<std::ptrdiff_t>(level.width) * 4,
                  level.pixels.begin() + static_cast<std::ptrdiff_t>(y) * level.width * 4);
    }
    image->destroy();

    // 2. A raster with room for every level, each written in turn.
    rw::Raster* raster = rw::Raster::create(level.width, level.height, 32, kRasterFormat);
    if (raster == nullptr) {
        return fail(ErrorCode::Invalid, std::format("the sandbox texture {} could not be uploaded", path.string()));
    }
    for (rw::int32 index = 0; index < raster->getNumLevels(); ++index) {
        if (index > 0) {
            level = halve(level);
        }
        rw::uint8* pixels = raster->lock(index, rw::Raster::LOCKWRITE | rw::Raster::LOCKNOFETCH);
        for (int y = 0; y < level.height; ++y) {
            std::copy_n(level.pixels.begin() + static_cast<std::ptrdiff_t>(y) * level.width * 4, level.width * 4,
                        pixels + static_cast<std::ptrdiff_t>(y) * raster->stride);
        }
        raster->unlock(index);
    }

    // 3. The texture: trilinear, repeating, anisotropic.
    rw::Texture* texture = rw::Texture::create(raster);
    texture->setFilter(rw::Texture::LINEARMIPLINEAR);
    texture->setAddressU(rw::Texture::WRAP);
    texture->setAddressV(rw::Texture::WRAP);
    texture->setMaxAnisotropy(std::min(kMaxAnisotropy, std::max<rw::int32>(1, rw::getMaxSupportedMaxAnisotropy())));
    return texture;
}

} // namespace

graphics::Rgba skyColour(const sandbox::Lighting& lighting) {
    return graphics::Rgba{channel(lighting.sky.r), channel(lighting.sky.g), channel(lighting.sky.b), 255};
}

std::expected<std::unique_ptr<SandboxRenderer>, Error> SandboxRenderer::create(const RenderEngine& engine,
                                                                               const sandbox::SandboxWorld& world) {
    std::unique_ptr<SandboxRenderer> renderer(new SandboxRenderer());
    const sandbox::SandboxLayout& layout = world.layout();
    renderer->m_sky = skyColour(layout.lighting);
    renderer->m_fogStart = layout.lighting.fogStart;
    // The textures first (only where they can be drawn), so the materials can hold them.
    renderer->m_textures.assign(layout.textures.size(), nullptr);
    if (engine.drawsPixels()) {
        for (std::size_t i = 0; i < layout.textures.size(); ++i) {
            auto texture = loadTexture(world.folder() / layout.textures[i].file);
            if (!texture) {
                return std::unexpected(std::move(texture.error()));
            }
            renderer->m_textures[i] = *texture;
        }
    }
    renderer->buildAtomics(world);
    return renderer;
}

SandboxRenderer::~SandboxRenderer() {
    for (rw::Atomic* atomic : m_atomics) {
        rw::Frame* frame = atomic->getFrame();
        atomic->destroy();
        if (frame != nullptr) {
            frame->destroy();
        }
    }
    for (rw::Texture* texture : m_textures) {
        if (texture != nullptr) {
            texture->destroy();
        }
    }
}

std::size_t SandboxRenderer::textureCount() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_textures, [](const rw::Texture* t) { return t != nullptr; }));
}

void SandboxRenderer::buildAtomics(const sandbox::SandboxWorld& world) {
    const sandbox::SandboxMesh& mesh = world.mesh();
    const sandbox::SandboxLayout& layout = world.layout();
    // The material slot of a primitive's texture: the texture's index, or one past the last for untextured.
    const auto textureSlot = [&layout](std::uint32_t primitive) {
        const std::optional<std::size_t> texture = layout.primitives[primitive].texture;
        return texture ? *texture : layout.textures.size();
    };

    std::size_t next = 0; // the first triangle not yet in an atomic
    std::vector<std::int32_t> local(mesh.vertices.size(), -1);
    while (next < mesh.triangles.size()) {
        // 1. The batch: triangles in order until one more would pass the vertex limit.
        std::vector<std::uint32_t> vertices; // mesh vertex of each batch vertex
        std::size_t end = next;
        for (; end < mesh.triangles.size(); ++end) {
            std::size_t added = 0;
            for (const std::uint32_t v : mesh.triangles[end].vertices) {
                added += local[v] < 0 ? 1 : 0;
            }
            if (vertices.size() + added > kMaxBatchVertices) {
                break;
            }
            for (const std::uint32_t v : mesh.triangles[end].vertices) {
                if (local[v] < 0) {
                    local[v] = static_cast<std::int32_t>(vertices.size());
                    vertices.push_back(v);
                }
            }
        }

        // 2. Its materials: one per texture slot the batch uses, in slot order.
        std::vector<std::int32_t> materialOf(layout.textures.size() + 1, -1);
        for (std::size_t t = next; t < end; ++t) {
            materialOf[textureSlot(mesh.triangles[t].primitive)] = 0;
        }
        rw::Geometry* geometry = rw::Geometry::create(
            static_cast<rw::int32>(vertices.size()), static_cast<rw::int32>(end - next),
            rw::Geometry::POSITIONS | rw::Geometry::TEXTURED | rw::Geometry::PRELIT | rw::Geometry::MODULATE);
        rw::int32 materials = 0;
        for (std::size_t slot = 0; slot < materialOf.size(); ++slot) {
            if (materialOf[slot] < 0) {
                continue;
            }
            materialOf[slot] = materials++;
            rw::Material* material = rw::Material::create();
            material->color = rw::makeRGBA(255, 255, 255, 255);
            if (slot < m_textures.size()) {
                material->setTexture(m_textures[slot]); // null when not loaded: drawn untextured
            }
            geometry->matList.appendMaterial(material);
            material->destroy(); // the list holds its own reference
        }

        // 3. Vertices in RenderWare's axes, (x, y, z) -> (x, z, -y), with their texture coordinates and light.
        rw::MorphTarget& target = geometry->morphTargets[0];
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const sandbox::MeshVertex& v = mesh.vertices[vertices[i]];
            target.vertices[i] = rw::V3d{v.position.x, v.position.z, -v.position.y};
            geometry->texCoords[0][i] = rw::TexCoords{v.u, v.v};
            geometry->colors[i] = rw::makeRGBA(channel(v.colour.r), channel(v.colour.g), channel(v.colour.b), 255);
        }
        // 4. The triangles, by the batch-local numbers step 1 gave their vertices; then those numbers are cleared for
        // the next batch.
        for (std::size_t t = next; t < end; ++t) {
            const sandbox::MeshTriangle& triangle = mesh.triangles[t];
            rw::Triangle& out = geometry->triangles[t - next];
            for (std::size_t k = 0; k < 3; ++k) {
                out.v[k] = static_cast<rw::uint16>(local[triangle.vertices.at(k)]);
            }
            out.matId = static_cast<rw::uint16>(materialOf[textureSlot(triangle.primitive)]);
        }
        for (const std::uint32_t v : vertices) {
            local[v] = -1;
        }
        geometry->buildMeshes();
        geometry->calculateBoundingSphere();

        // 5. The atomic, on a frame of its own at the origin.
        rw::Atomic* atomic = rw::Atomic::create();
        atomic->setGeometry(geometry, 0);
        geometry->destroy(); // the atomic holds its own reference
        atomic->setFrame(rw::Frame::create());
        m_atomics.push_back(atomic);
        next = end;
    }
}

void SandboxRenderer::render(RenderEngine& engine, const WorldView& view, const std::function<void()>& drawObjects) {
    engine.beginWindowFrame(m_sky);
    rw::Camera* camera = engine.camera();
    if (camera == nullptr) {
        engine.present(); // NULL backend: nothing to draw
        return;
    }
    placeWorldCamera(camera, view);
    camera->fogPlane = std::min(m_fogStart, view.drawDistance * 0.99F);
    camera->beginUpdate();

    // The states of a world pass: Z test and write, back faces culled, fog in the sky colour (red in the low byte).
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    rw::SetRenderState(rw::FOGENABLE, 1);
    rw::SetRenderState(rw::FOGCOLOR, static_cast<rw::uint32>(m_sky.r) | static_cast<rw::uint32>(m_sky.g) << 8U |
                                         static_cast<rw::uint32>(m_sky.b) << 16U | 0xFF000000U);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    for (rw::Atomic* atomic : m_atomics) {
        atomic->render();
    }
    if (drawObjects) {
        drawObjects();
    }
    rw::SetRenderState(rw::FOGENABLE, 0);
    engine.present();
}

} // namespace coney::platform
