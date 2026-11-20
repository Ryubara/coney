// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only renderer data: the meshes and textures the frame description names by id
// (docs/engine/asset-formats.md); the original keeps RenderWare objects by pointer
#include "platform/scene_assets.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <rw.h>

#include "core/perf.h"
#include "platform/asset_convert.h"

namespace coney::platform {

namespace {

// Adds the time of its lifetime to a total, while the profiler measures (the clock is the profiler's, core/perf.h).
class ConversionTimer {
  public:
    explicit ConversionTimer(std::uint64_t& total)
        : m_total(total), m_on(perf::enabled()), m_start(m_on ? perf::now() : 0) {}
    // Adds the time since the start, when it was started.
    ~ConversionTimer() {
        if (m_on) {
            m_total += perf::now() - m_start;
        }
    }
    ConversionTimer(const ConversionTimer&) = delete;
    ConversionTimer& operator=(const ConversionTimer&) = delete;
    ConversionTimer(ConversionTimer&&) = delete;
    ConversionTimer& operator=(ConversionTimer&&) = delete;

  private:
    std::uint64_t& m_total;
    bool m_on;
    std::uint64_t m_start;
};

} // namespace

std::uint64_t SceneAssets::bytesOf(const graphics::MeshData& mesh) {
    std::uint64_t bytes =
        mesh.vertices.size() * sizeof(graphics::MeshVertex) + mesh.indices.size() * sizeof(std::uint16_t) +
        mesh.subMeshes.size() * sizeof(graphics::SubMesh) + mesh.materials.size() * sizeof(graphics::MeshMaterial);
    for (const graphics::MeshMaterial& material : mesh.materials) {
        bytes += material.texture.capacity();
    }
    return bytes;
}

std::uint64_t SceneAssets::bytesOf(const graphics::TextureData& texture) {
    std::uint64_t bytes = 0;
    for (const graphics::TextureLevel& level : texture.levels) {
        bytes += level.texels.size();
    }
    return bytes;
}

void SceneAssets::hold(std::uint64_t& kind, std::uint64_t bytes) {
    kind += bytes;
    m_totals.peakBytes = std::max(m_totals.peakBytes, m_totals.textureBytes + m_totals.meshBytes);
}

std::expected<graphics::TextureId, Error> SceneAssets::addTexture(rw::Texture& texture) {
    if (!m_convert) {
        return graphics::TextureId{};
    }
    if (const graphics::TextureId have = textureOf(&texture); have.valid()) {
        ++m_uses[have.value].uses;
        return have;
    }
    const ConversionTimer timer(m_totals.nanoseconds);
    auto converted = textureFromRw(texture);
    if (!converted) {
        return std::unexpected(std::move(converted.error()));
    }
    const std::uint64_t bytes = bytesOf(*converted);
    auto id = m_store.addTexture(std::move(*converted));
    if (!id) {
        return std::unexpected(std::move(id.error()));
    }
    m_textures.emplace(&texture, *id);
    m_uses.emplace(id->value, TextureEntry{.source = &texture, .uses = 1});
    ++m_totals.textures;
    hold(m_totals.textureBytes, bytes);
    return *id;
}

std::expected<graphics::TextureId, Error> SceneAssets::addTexture(rw::Texture& texture, graphics::TextureData data) {
    if (!m_convert) {
        return graphics::TextureId{};
    }
    if (const graphics::TextureId have = textureOf(&texture); have.valid()) {
        ++m_uses[have.value].uses;
        return have;
    }
    // Held as a converted one is, under the librw texture its meshes' materials name.
    const std::uint64_t bytes = bytesOf(data);
    auto id = m_store.addTexture(std::move(data));
    if (!id) {
        return std::unexpected(std::move(id.error()));
    }
    m_textures.emplace(&texture, *id);
    m_uses.emplace(id->value, TextureEntry{.source = &texture, .uses = 1});
    hold(m_totals.textureBytes, bytes);
    return *id;
}

void SceneAssets::removeTexture(graphics::TextureId id) { release(id); }

void SceneAssets::release(graphics::TextureId id) {
    const auto found = m_uses.find(id.value);
    if (found == m_uses.end()) {
        return;
    }
    // Still used by another addTexture() or a held mesh's material: it stays, under the same id.
    if (--found->second.uses > 0) {
        return;
    }
    if (const graphics::TextureData* data = m_store.texture(id); data != nullptr) {
        m_totals.textureBytes -= bytesOf(*data);
    }
    m_store.removeTexture(id);
    // The librw texture it came from is forgotten too, so one made later at the same address is a new texture.
    m_textures.erase(found->second.source);
    m_uses.erase(found);
}

graphics::TextureId SceneAssets::textureOf(const rw::Texture* texture) const {
    const auto found = m_textures.find(texture);
    return found != m_textures.end() ? found->second : graphics::TextureId{};
}

std::expected<graphics::MeshId, Error> SceneAssets::addAtomic(rw::Atomic& atomic) {
    if (atomic.geometry == nullptr) {
        return std::unexpected(Error{ErrorCode::Invalid, "the atomic has no geometry"});
    }
    if (!m_convert) {
        // Only the atomic is kept, for the GL3 renderer: under an id of its own, since the store holds no mesh.
        const graphics::MeshId bare{++m_lastBareMesh};
        m_meshes.emplace(bare.value, MeshEntry{.atomic = &atomic, .textures = {}});
        return bare;
    }
    const ConversionTimer timer(m_totals.nanoseconds);
    auto converted = meshFromRw(*atomic.geometry);
    if (!converted) {
        return std::unexpected(std::move(converted.error()));
    }
    const std::uint64_t bytes = bytesOf(*converted);
    auto id = m_store.addMesh(std::move(*converted));
    if (!id) {
        return std::unexpected(std::move(id.error()));
    }
    ++m_totals.meshes;
    hold(m_totals.meshBytes, bytes);
    // Each material's texture by id, in the geometry's material list order, as meshFromRw() lists the materials.
    MeshEntry entry{.atomic = &atomic, .textures = {}};
    const rw::Geometry& geometry = *atomic.geometry;
    for (rw::int32 m = 0; m < geometry.matList.numMaterials; ++m) {
        const graphics::TextureId texture = textureOf(geometry.matList.materials[m]->texture);
        // The mesh holds the texture while it is held, so the id stays valid after whoever added it lets go: a
        // material may have found it in another dictionary, such as a streamed part's (renderer.md#texture-lifetime).
        if (texture.valid()) {
            ++m_uses[texture.value].uses;
        }
        entry.textures.push_back(texture);
    }
    m_meshes.emplace(id->value, std::move(entry));
    return *id;
}

void SceneAssets::removeMesh(graphics::MeshId id) {
    if (const graphics::MeshData* data = m_store.mesh(id); data != nullptr) {
        m_totals.meshBytes -= bytesOf(*data);
    }
    m_store.removeMesh(id);
    const auto found = m_meshes.find(id.value);
    if (found == m_meshes.end()) {
        return;
    }
    // Moved out first: releasing may not touch the entry being erased.
    const std::vector<graphics::TextureId> textures = std::move(found->second.textures);
    m_meshes.erase(found);
    for (const graphics::TextureId texture : textures) {
        release(texture);
    }
}

rw::Atomic* SceneAssets::atomic(graphics::MeshId id) const {
    const auto found = m_meshes.find(id.value);
    return found != m_meshes.end() ? found->second.atomic : nullptr;
}

std::span<const graphics::TextureId> SceneAssets::materialTextures(graphics::MeshId id) const {
    const auto found = m_meshes.find(id.value);
    if (found == m_meshes.end()) {
        return {};
    }
    return found->second.textures;
}

} // namespace coney::platform
