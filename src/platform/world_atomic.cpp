// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_atomic.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <utility>
#include <vector>

#include <rw.h>

#include "core/assert.h"
#include "graphics/ps2_world_mesh.h"
#include "graphics/rw_stream.h"

namespace coney::platform {

namespace {

// Bytes librw gives each atomic for the game's plugin: the two scales, the word, and the owner pointer the game keeps
// there (not streamed; Coney has no owners yet but keeps the room, as the original's 16 bytes do).
constexpr rw::int32 kAtomicPluginBytes = 16;

// The game's atomic plugin data inside a librw atomic, at the offset librw gave the plugin.
rw::int32 atomicPluginOffset = -1;

// Coney's own atomic plugin (never streamed): the pointer to an atomic's dual layer (dualLayerOf()). The id is Coney's,
// outside RenderWare's and the game's ranges.
constexpr rw::uint32 kDualLayerPluginId = 0x00C0DE01;
rw::int32 dualLayerOffset = -1;

// A new atomic has no dual layer.
void* constructDualLayer(void* object, rw::int32 offset, rw::int32 /*size*/) {
    rw::Atomic* none = nullptr;
    std::memcpy(static_cast<std::byte*>(object) + offset, static_cast<const void*>(&none), sizeof(rw::Atomic*));
    return object;
}

// A copy of an atomic does not share its original's dual layer, which the original's owner destroys.
void* copyDualLayer(void* destination, void* /*source*/, rw::int32 offset, rw::int32 size) {
    return constructDualLayer(destination, offset, size);
}

// Coney's reader of RenderWare's MatFX material extension (rendering.md#dual), keeping only the dual effect's texture;
// librw's own MatFX plugin is not attached, as it opens a driver for every backend (shaders without a GL context
// under the NULL engine) and draws through pipelines Coney does not use.
constexpr rw::uint32 kMatFxPluginId = 0x120; // rwID_MATERIALEFFECTSPLUGIN
rw::int32 dualTextureOffset = -1;

// The effect types a MatFX slot can hold (RenderWare's numbering).
enum : std::uint8_t { kFxBumpMap = 1, kFxEnvMap = 2, kFxDual = 4 };

// A material's dual texture slot, or its value.
rw::Texture*& dualSlot(void* material) {
    return *reinterpret_cast<rw::Texture**>(static_cast<std::byte*>(material) + dualTextureOffset);
}

// A new material has no dual texture.
void* constructDualTexture(void* object, rw::int32 /*offset*/, rw::int32 /*size*/) {
    dualSlot(object) = nullptr;
    return object;
}

// A destroyed material lets go of its dual texture.
void* destroyDualTexture(void* object, rw::int32 /*offset*/, rw::int32 /*size*/) {
    if (rw::Texture* texture = std::exchange(dualSlot(object), nullptr); texture != nullptr) {
        texture->destroy();
    }
    return object;
}

// A copied material shares its original's dual texture, holding its own reference.
void* copyDualTexture(void* destination, void* source, rw::int32 /*offset*/, rw::int32 /*size*/) {
    rw::Texture* texture = dualSlot(source);
    if (texture != nullptr) {
        ++texture->refCount;
    }
    dualSlot(destination) = texture;
    return destination;
}

// Reads one texture the extension says follows (a flag, then the texture chunk); null when it has none, or on error.
rw::Texture* readFxTexture(rw::Stream* stream, bool& ok) {
    if (stream->readI32() == 0) {
        return nullptr;
    }
    if (!rw::findChunk(stream, rw::ID_TEXTURE, nullptr, nullptr)) {
        ok = false;
        return nullptr;
    }
    return rw::Texture::streamRead(stream);
}

// The MatFX material extension: its effects word, then two slots, each a type and that effect's values and textures.
rw::Stream* readDualTexture(rw::Stream* stream, rw::int32 /*length*/, void* object, rw::int32 /*offset*/,
                            rw::int32 /*size*/) {
    (void)stream->readU32(); // which effects; the slots below say the same
    bool ok = true;
    for (int slot = 0; slot < 2 && ok; ++slot) {
        switch (stream->readU32()) {
        case kFxBumpMap: {
            (void)stream->readF32(); // coefficient
            for (int t = 0; t < 2 && ok; ++t) {
                if (rw::Texture* texture = readFxTexture(stream, ok); texture != nullptr) {
                    texture->destroy(); // Coney draws no bump maps
                }
            }
            break;
        }
        case kFxEnvMap: {
            (void)stream->readF32(); // coefficient
            (void)stream->readI32(); // frame-buffer alpha
            if (rw::Texture* texture = readFxTexture(stream, ok); texture != nullptr) {
                texture->destroy(); // the environment maps are not drawn yet
            }
            break;
        }
        case kFxDual: {
            (void)stream->readI32(); // source blend
            (void)stream->readI32(); // destination blend
            rw::Texture* texture = readFxTexture(stream, ok);
            if (rw::Texture* old = std::exchange(dualSlot(object), texture); old != nullptr) {
                old->destroy();
            }
            break;
        }
        default: // nothing, or a UV transform: no values in the stream
            break;
        }
    }
    return ok ? stream : nullptr;
}

// Coney never writes materials back out.
rw::Stream* writeDualTexture(rw::Stream* stream, rw::int32 /*length*/, void* /*object*/, rw::int32 /*offset*/,
                             rw::int32 /*size*/) {
    return stream;
}

// So nothing is written.
rw::int32 dualTextureStreamSize(void* /*object*/, rw::int32 /*offset*/, rw::int32 /*size*/) { return 0; }

// Stores `layer` as `atomic`'s dual layer.
void setDualLayer(rw::Atomic* atomic, rw::Atomic* layer) {
    std::memcpy(reinterpret_cast<std::byte*>(atomic) + dualLayerOffset, static_cast<const void*>(&layer),
                sizeof(rw::Atomic*));
}

// Sets the plugin's defaults on a new atomic: scales 1, word 0 (as the original's constructor at 0x00192618 does).
void* constructAtomicPlugin(void* object, rw::int32 offset, rw::int32 /*size*/) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &world::kDefaultAtomicPluginData,
                sizeof(world::AtomicPluginData));
    return object;
}

// Copies the plugin data with the atomic (librw's Atomic::clone).
void* copyAtomicPlugin(void* destination, void* source, rw::int32 offset, rw::int32 /*size*/) {
    std::memcpy(static_cast<std::byte*>(destination) + offset, static_cast<const std::byte*>(source) + offset,
                sizeof(world::AtomicPluginData));
    return destination;
}

// The plugin's stream reader: 12 bytes into the plugin data. Other lengths are skipped and leave the defaults.
rw::Stream* readAtomicPlugin(rw::Stream* stream, rw::int32 length, void* object, rw::int32 offset, rw::int32 /*size*/) {
    std::array<std::byte, world::kAtomicPluginStreamBytes> bytes{};
    if (length != static_cast<rw::int32>(bytes.size())) {
        stream->seek(length);
        return stream;
    }
    stream->read8(bytes.data(), static_cast<rw::uint32>(bytes.size()));
    if (auto data = world::readAtomicPluginData(bytes); data) {
        std::memcpy(static_cast<std::byte*>(object) + offset, &*data, sizeof(world::AtomicPluginData));
    }
    return stream;
}

// The plugin's stream writer, the reader's inverse (librw calls it when an atomic is written out).
rw::Stream* writeAtomicPlugin(rw::Stream* stream, rw::int32 /*length*/, void* object, rw::int32 offset,
                              rw::int32 /*size*/) {
    world::AtomicPluginData data;
    std::memcpy(&data, static_cast<const std::byte*>(object) + offset, sizeof(data));
    stream->writeF32(data.positionScale);
    stream->writeF32(data.secondScale);
    stream->writeU32(data.word);
    return stream;
}

// The plugin's stream size: always its 12 bytes.
rw::int32 atomicPluginStreamSize(void* /*object*/, rw::int32 /*offset*/, rw::int32 /*size*/) {
    return static_cast<rw::int32>(world::kAtomicPluginStreamBytes);
}

// Coney's stand-ins for the game's two PS2 atomic pipelines, and for RenderWare's default PS2 pipeline as the level
// world uses it. They only unpack: drawing happens after the atomic has been handed to the platform's default
// pipeline (WorldAtomic::unpack).
std::array<rw::ObjPipeline, 2> gamePipelines{};
rw::ObjPipeline defaultLayoutPipeline{};

// One vertex in librw's terms, from either layout: what a plain geometry stores.
struct PlainVertex {
    rw::V3d position{};
    std::array<rw::TexCoords, 2> texCoords{};
    rw::RGBA colour{};
    rw::V3d normal{};
};

// A decoded mesh in librw's terms, with the attributes its batches carried.
struct PlainMesh {
    std::vector<PlainVertex> vertices;
    std::uint32_t texCoordSets = 0;
    bool colours = false;
    bool normals = false;
};

// A prelighting colour channel for librw, where 255 is full brightness. The PS2's GS modulates a texel by a vertex
// colour with 0x80 as 1.0, so the packed channels are doubled and clamped (a Coney choice inferred from the GS: the
// disc's values average about 14 and rarely pass 128, and look right doubled; alpha, always 255 here, is kept).
rw::uint8 doubled(std::uint8_t channel) { return static_cast<rw::uint8>(std::min(255, channel * 2)); }

// The PS2 native meshes of `atomic`'s geometry, checked against its mesh header; nothing when there are none.
const rw::ps2::InstanceDataHeader* nativeMeshes(rw::Atomic* atomic) {
    rw::Geometry* geometry = atomic->geometry;
    if ((geometry->flags & rw::Geometry::NATIVE) == 0 || geometry->instData == nullptr ||
        geometry->instData->platform != rw::PLATFORM_PS2 || geometry->meshHeader == nullptr) {
        return nullptr;
    }
    const auto* header = static_cast<const rw::ps2::InstanceDataHeader*>(geometry->instData);
    return header->numMeshes == geometry->meshHeader->numMeshes ? header : nullptr;
}

// The DMA chain of native mesh `instance`.
std::span<const std::byte> chainOf(const rw::ps2::InstanceData& instance) {
    return {reinterpret_cast<const std::byte*>(instance.data), instance.dataSize};
}

// Decodes every mesh of `atomic` in the game's packed layout: positions times the atomic's position scale (0x3F0
// +0x00), texture coordinates times its second scale (the world viewer shows textures right only with it, world.md),
// normals over 128 as librw's PS2 code reads them, colours doubled. False if anything does not decode
// (WorldAtomic::read has checked the same data, so only an atomic that did not come through it can fail).
bool decodeGameMeshes(rw::Atomic* atomic, std::vector<PlainMesh>& meshes) {
    const rw::ps2::InstanceDataHeader* header = nativeMeshes(atomic);
    if (header == nullptr) {
        return false;
    }
    const world::AtomicPluginData scales = atomicPluginData(atomic);
    const bool strip = atomic->geometry->meshHeader->flags == rw::MeshHeader::TRISTRIP;
    const rw::Mesh* mesh = atomic->geometry->meshHeader->getMeshes();
    for (rw::uint32 i = 0; i < header->numMeshes; ++i) {
        auto decoded = graphics::decodePs2WorldMesh(chainOf(header->instanceMeshes[i]), strip);
        if (!decoded || decoded->vertices.size() != mesh[i].numIndices) {
            return false;
        }
        PlainMesh& plain = meshes.emplace_back();
        plain.texCoordSets = decoded->attributes.texCoordSets;
        plain.colours = decoded->attributes.colours;
        plain.normals = decoded->attributes.normals;
        for (const graphics::Ps2PackedVertex& v : decoded->vertices) {
            PlainVertex& out = plain.vertices.emplace_back();
            out.position = rw::makeV3d(static_cast<float>(v.position[0]) * scales.positionScale,
                                       static_cast<float>(v.position[1]) * scales.positionScale,
                                       static_cast<float>(v.position[2]) * scales.positionScale);
            for (std::size_t set = 0; set < 2; ++set) {
                out.texCoords.at(set) =
                    rw::TexCoords{static_cast<float>(v.texCoords.at(set * 2)) * scales.secondScale,
                                  static_cast<float>(v.texCoords.at(set * 2 + 1)) * scales.secondScale};
            }
            out.colour = rw::makeRGBA(doubled(v.colour[0]), doubled(v.colour[1]), doubled(v.colour[2]), v.colour[3]);
            out.normal = rw::makeV3d(static_cast<float>(v.normal[0]) / 128.0F, static_cast<float>(v.normal[1]) / 128.0F,
                                     static_cast<float>(v.normal[2]) / 128.0F);
        }
    }
    return true;
}

// Decodes every mesh of `atomic` in RenderWare's default PS2 layout: float positions and texture coordinates as they
// are, normals over 127 as RenderWare packs them, colours doubled as for the game's layout (the GS is the same).
bool decodeDefaultMeshes(rw::Atomic* atomic, std::vector<PlainMesh>& meshes) {
    const rw::ps2::InstanceDataHeader* header = nativeMeshes(atomic);
    if (header == nullptr) {
        return false;
    }
    const bool strip = atomic->geometry->meshHeader->flags == rw::MeshHeader::TRISTRIP;
    const rw::Mesh* mesh = atomic->geometry->meshHeader->getMeshes();
    for (rw::uint32 i = 0; i < header->numMeshes; ++i) {
        auto decoded = graphics::decodePs2DefaultMesh(chainOf(header->instanceMeshes[i]), strip);
        if (!decoded || decoded->vertices.size() != mesh[i].numIndices) {
            return false;
        }
        PlainMesh& plain = meshes.emplace_back();
        plain.texCoordSets = 1;
        plain.colours = true;
        plain.normals = true;
        for (const graphics::Ps2DefaultVertex& v : decoded->vertices) {
            PlainVertex& out = plain.vertices.emplace_back();
            out.position = rw::makeV3d(v.position[0], v.position[1], v.position[2]);
            out.texCoords[0] = rw::TexCoords{v.texCoords[0], v.texCoords[1]};
            out.colour = rw::makeRGBA(doubled(v.colour[0]), doubled(v.colour[1]), doubled(v.colour[2]), v.colour[3]);
            out.normal = rw::makeV3d(static_cast<float>(v.normal[0]) / 127.0F, static_cast<float>(v.normal[1]) / 127.0F,
                                     static_cast<float>(v.normal[2]) / 127.0F);
        }
    }
    return true;
}

// Ordering key of a plain vertex, so that identical vertices of a geometry share one index (strips join through
// repeated vertices, and librw finds degenerate triangles by equal indices): every component's bits, in order.
using VertexKey = std::array<std::uint32_t, 12>;
VertexKey keyOf(const PlainVertex& v) {
    const auto bits = [](float f) { return std::bit_cast<std::uint32_t>(f); };
    return {bits(v.position.x),
            bits(v.position.y),
            bits(v.position.z),
            bits(v.texCoords[0].u),
            bits(v.texCoords[0].v),
            bits(v.texCoords[1].u),
            bits(v.texCoords[1].v),
            bits(v.normal.x),
            bits(v.normal.y),
            bits(v.normal.z),
            static_cast<std::uint32_t>(v.colour.red) | static_cast<std::uint32_t>(v.colour.green) << 8U |
                static_cast<std::uint32_t>(v.colour.blue) << 16U | static_cast<std::uint32_t>(v.colour.alpha) << 24U,
            0};
}

// Replaces `atomic`'s PS2 native geometry with plain geometry made of `meshes`: librw's uninstance step for Coney's
// stand-in pipelines. The meshes keep their materials and counts; identical vertices are shared and the triangles
// derived from the strips.
void replaceWithPlainGeometry(rw::Atomic* atomic, const std::vector<PlainMesh>& meshes) {
    rw::Geometry* geometry = atomic->geometry;
    // Number the distinct vertices; librw's indices are 16 bits.
    std::map<VertexKey, rw::uint16> indexOf;
    std::vector<const PlainVertex*> unique;
    std::vector<std::vector<rw::uint16>> indices(meshes.size());
    rw::int32 triangleBound = 0;
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        for (const PlainVertex& vertex : meshes[m].vertices) {
            auto [it, inserted] = indexOf.try_emplace(keyOf(vertex), static_cast<rw::uint16>(unique.size()));
            if (inserted) {
                if (unique.size() > 0xFFFF) {
                    return;
                }
                unique.push_back(&vertex);
            }
            indices[m].push_back(it->second);
        }
        triangleBound += static_cast<rw::int32>(meshes[m].vertices.size());
    }

    // Allocate the plain arrays (the triangle count is an upper bound until generateTriangles counts them).
    geometry->flags |= rw::Geometry::POSITIONS;
    geometry->numVertices = static_cast<rw::int32>(unique.size());
    geometry->numTriangles = triangleBound;
    geometry->allocateData();
    geometry->allocateMeshes(static_cast<rw::int32>(meshes.size()), geometry->meshHeader->totalIndices, 0);

    // Fill the vertices, with what the batches did not carry set to neutral values.
    const std::uint32_t uvSets = meshes.empty() ? 0 : meshes[0].texCoordSets;
    const bool hasColour = !meshes.empty() && meshes[0].colours;
    const bool hasNormal = !meshes.empty() && meshes[0].normals;
    rw::MorphTarget& target = geometry->morphTargets[0];
    for (std::size_t i = 0; i < unique.size(); ++i) {
        const PlainVertex& v = *unique[i];
        target.vertices[i] = v.position;
        if (target.normals != nullptr) {
            target.normals[i] = hasNormal ? v.normal : rw::makeV3d(0.0F, 0.0F, 1.0F);
        }
        if (geometry->colors != nullptr) {
            geometry->colors[i] = hasColour ? v.colour : rw::makeRGBA(255, 255, 255, 255);
        }
        for (rw::int32 set = 0; set < geometry->numTexCoordSets && set < 2; ++set) {
            geometry->texCoords[set][i] = static_cast<std::uint32_t>(set) < uvSets
                                              ? v.texCoords.at(static_cast<std::size_t>(set))
                                              : rw::TexCoords{0.0F, 0.0F};
        }
    }

    // Give the meshes their indices, then derive the triangles.
    rw::Mesh* mesh = geometry->meshHeader->getMeshes();
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        std::memcpy(mesh[m].indices, indices[m].data(), indices[m].size() * sizeof(rw::uint16));
    }
    geometry->generateTriangles();
    rw::ps2::destroyNativeData(geometry, 0, 0);
    geometry->flags &= ~static_cast<rw::uint32>(rw::Geometry::NATIVE);
    geometry->calculateBoundingSphere();
}

// The dual texture of `material` (its MatFX dual effect's), or null.
rw::Texture* dualTextureOf(rw::Material* material) {
    return material != nullptr && dualTextureOffset >= 0 ? dualSlot(material) : nullptr;
}

// Makes the dual layer of `base` (plain geometry with two texture-coordinate sets): a geometry with the same vertices,
// the second set as its only one, and one mesh per dual material, textured with the dual texture and coloured as the
// base material; on `base`'s frame. Null when no mesh has a dual texture.
rw::Atomic* makeDualLayer(rw::Atomic* base) {
    rw::Geometry* source = base->geometry;
    if (source == nullptr || source->meshHeader == nullptr || source->numTexCoordSets < 2 ||
        (source->flags & rw::Geometry::NATIVE) != 0) {
        return nullptr;
    }
    const rw::Mesh* meshes = source->meshHeader->getMeshes();
    std::vector<const rw::Mesh*> duals;
    rw::uint32 indexTotal = 0;
    for (rw::int32 m = 0; m < static_cast<rw::int32>(source->meshHeader->numMeshes); ++m) {
        if (dualTextureOf(meshes[m].material) != nullptr && meshes[m].numIndices > 0) {
            duals.push_back(&meshes[m]);
            indexTotal += meshes[m].numIndices;
        }
    }
    if (duals.empty()) {
        return nullptr;
    }

    // The vertices, with the second texture-coordinate set as the first.
    const rw::uint32 kept = source->flags & (rw::Geometry::POSITIONS | rw::Geometry::PRELIT | rw::Geometry::NORMALS |
                                             rw::Geometry::LIGHT | rw::Geometry::MODULATE | rw::Geometry::TRISTRIP);
    rw::Geometry* geometry =
        rw::Geometry::create(source->numVertices, static_cast<rw::int32>(indexTotal), kept | rw::Geometry::TEXTURED);
    const rw::MorphTarget& from = source->morphTargets[0];
    rw::MorphTarget& to = geometry->morphTargets[0];
    const auto count = static_cast<std::size_t>(source->numVertices);
    std::memcpy(to.vertices, from.vertices, count * sizeof(rw::V3d));
    if (to.normals != nullptr && from.normals != nullptr) {
        std::memcpy(to.normals, from.normals, count * sizeof(rw::V3d));
    }
    if (geometry->colors != nullptr && source->colors != nullptr) {
        std::memcpy(geometry->colors, source->colors, count * sizeof(rw::RGBA));
    }
    std::memcpy(geometry->texCoords[0], source->texCoords[1], count * sizeof(rw::TexCoords));

    // One mesh per dual material, its indices as the base mesh's.
    geometry->allocateMeshes(static_cast<rw::int32>(duals.size()), indexTotal, 0);
    geometry->meshHeader->flags = source->meshHeader->flags;
    rw::Mesh* out = geometry->meshHeader->getMeshes();
    for (std::size_t m = 0; m < duals.size(); ++m) {
        rw::Material* material = rw::Material::create();
        material->color = duals[m]->material->color;
        material->surfaceProps = duals[m]->material->surfaceProps;
        material->setTexture(dualTextureOf(duals[m]->material));
        geometry->matList.appendMaterial(material);
        material->destroy(); // the list holds its own reference
        out[m].material = material;
        out[m].numIndices = duals[m]->numIndices;
    }
    geometry->meshHeader->setupIndices(); // each mesh's indices after the one before, by the counts just set
    for (std::size_t m = 0; m < duals.size(); ++m) {
        std::memcpy(out[m].indices, duals[m]->indices, duals[m]->numIndices * sizeof(rw::uint16));
    }
    geometry->generateTriangles();
    geometry->calculateBoundingSphere();

    rw::Atomic* layer = rw::Atomic::create();
    layer->setGeometry(geometry, 0);
    geometry->destroy(); // the atomic holds its own reference
    layer->setFrame(base->getFrame());
    return layer;
}

// The uninstance step of the game pipelines' stand-in: the packed layout.
void uninstanceGameAtomic(rw::ObjPipeline* /*pipeline*/, rw::Atomic* atomic) {
    std::vector<PlainMesh> meshes;
    if (decodeGameMeshes(atomic, meshes)) {
        replaceWithPlainGeometry(atomic, meshes);
    }
}

// The uninstance step of the default pipeline's stand-in: RenderWare's own layout.
void uninstanceDefaultAtomic(rw::ObjPipeline* /*pipeline*/, rw::Atomic* atomic) {
    std::vector<PlainMesh> meshes;
    if (decodeDefaultMeshes(atomic, meshes)) {
        replaceWithPlainGeometry(atomic, meshes);
    }
}

// The rights callback of the game's pipeline plugin: an atomic whose right to render names one of the two world
// pipelines gets Coney's stand-in for it.
void gamePipelineRights(void* object, rw::int32 /*offset*/, rw::int32 /*size*/, rw::uint32 data) {
    auto* atomic = static_cast<rw::Atomic*>(object);
    if (data == kGameAtomicPipelineA) {
        atomic->pipeline = &gamePipelines[0];
    } else if (data == kGameAtomicPipelineB) {
        atomic->pipeline = &gamePipelines[1];
    }
}

// Copies `bytes` into a buffer librw may read from (its memory stream takes a non-const pointer).
std::vector<rw::uint8> copyForLibrw(std::span<const std::byte> bytes) {
    std::vector<rw::uint8> copy(bytes.size());
    std::memcpy(copy.data(), bytes.data(), bytes.size());
    return copy;
}

// Reads a geometry section (header included) with librw.
rw::Geometry* readGeometry(std::span<const std::byte> section) {
    std::vector<rw::uint8> copy = copyForLibrw(section);
    rw::StreamMemory memory;
    memory.open(copy.data(), static_cast<rw::uint32>(copy.size()), static_cast<rw::uint32>(copy.size()));
    rw::Geometry* geometry = nullptr;
    if (rw::findChunk(&memory, rw::ID_GEOMETRY, nullptr, nullptr)) {
        geometry = rw::Geometry::streamRead(&memory);
    }
    memory.close();
    return geometry;
}

} // namespace

void attachWorldPlugins() {
    if (rw::Atomic::getPluginOffset(world::kAtomicPluginId) >= 0) {
        return; // this librw engine has them already
    }
    rw::registerMeshPlugin();
    rw::registerNativeDataPlugin();
    rw::registerAtomicRightsPlugin();
    rw::registerMaterialRightsPlugin();
    // MatFX, for the dual layers' textures; Coney draws them itself.
    dualTextureOffset = rw::Material::registerPlugin(sizeof(rw::Texture*), kMatFxPluginId, constructDualTexture,
                                                     destroyDualTexture, copyDualTexture);
    rw::Material::registerPluginStream(kMatFxPluginId, readDualTexture, writeDualTexture, dualTextureStreamSize);
    dualLayerOffset =
        rw::Atomic::registerPlugin(sizeof(rw::Atomic*), kDualLayerPluginId, constructDualLayer, nullptr, copyDualLayer);

    atomicPluginOffset = rw::Atomic::registerPlugin(kAtomicPluginBytes, world::kAtomicPluginId, constructAtomicPlugin,
                                                    nullptr, copyAtomicPlugin);
    rw::Atomic::registerPluginStream(world::kAtomicPluginId, readAtomicPlugin, writeAtomicPlugin,
                                     atomicPluginStreamSize);

    // The game's pipelines: plugin id 3 owns them; it has no stream data of its own, only the rights callback.
    for (std::size_t i = 0; i < gamePipelines.size(); ++i) {
        rw::ObjPipeline& pipeline = gamePipelines[i];
        pipeline.init(rw::PLATFORM_PS2);
        pipeline.pluginID = kGamePipelinePlugin;
        pipeline.pluginData = i == 0 ? kGameAtomicPipelineA : kGameAtomicPipelineB;
        pipeline.impl.uninstance = uninstanceGameAtomic;
    }
    defaultLayoutPipeline.init(rw::PLATFORM_PS2);
    defaultLayoutPipeline.impl.uninstance = uninstanceDefaultAtomic;
    rw::Atomic::registerPlugin(0, kGamePipelinePlugin, nullptr, nullptr, nullptr);
    rw::Atomic::setStreamRightsCallback(kGamePipelinePlugin, gamePipelineRights);
}

rw::Atomic* dualLayerOf(const rw::Atomic* atomic) {
    if (dualLayerOffset < 0 || atomic == nullptr) {
        return nullptr;
    }
    rw::Atomic* layer = nullptr;
    std::memcpy(static_cast<void*>(&layer), reinterpret_cast<const std::byte*>(atomic) + dualLayerOffset,
                sizeof(rw::Atomic*));
    return layer;
}

world::AtomicPluginData atomicPluginData(const rw::Atomic* atomic) {
    CONEY_ASSERT(atomicPluginOffset >= 0 && atomic != nullptr);
    // Copied out byte by byte: the plugin's room is raw memory inside librw's atomic, written the same way.
    world::AtomicPluginData data;
    std::memcpy(&data, reinterpret_cast<const std::byte*>(atomic) + atomicPluginOffset, sizeof(data));
    return data;
}

std::expected<WorldAtomic, Error> WorldAtomic::read(std::span<const std::byte> section, world::Vec3 origin,
                                                    VertexLayout layout) {
    CONEY_ASSERT(atomicPluginOffset >= 0);
    auto info = world::inspectAtomicSection(section);
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    const bool packed = layout == VertexLayout::GamePacked;
    if (!packed && info->pipeline) {
        return fail(ErrorCode::Invalid,
                    std::format("an atomic in RenderWare's default layout names pipeline {:#x}/{:#x}",
                                info->pipelinePlugin, info->pipeline.value_or(0)));
    }
    if (packed && (info->pipelinePlugin != kGamePipelinePlugin ||
                   (info->pipeline != kGameAtomicPipelineA && info->pipeline != kGameAtomicPipelineB))) {
        return fail(ErrorCode::Invalid, std::format("the atomic's pipeline {:#x}/{:#x} is not one of the game's world "
                                                    "pipelines",
                                                    info->pipelinePlugin, info->pipeline.value_or(0)));
    }
    // Decode every mesh once here, so that the uninstance step later cannot meet data it does not take. librw's
    // indices are 16 bits, so a geometry is limited to 65,536 vertex references (distinct vertices are fewer).
    std::size_t references = 0;
    for (const world::MeshInfo& mesh : info->meshes) {
        references += mesh.indexCount;
    }
    if (references > 0x10000) {
        return fail(ErrorCode::Invalid, std::format("the geometry's meshes hold {} vertices, more than librw's 16-bit "
                                                    "indices reach",
                                                    references));
    }
    for (std::size_t i = 0; i < info->meshes.size(); ++i) {
        const world::MeshInfo& mesh = info->meshes[i];
        // The vertex count of a decoded mesh, or its failure.
        auto count = [&]() -> std::expected<std::size_t, Error> {
            if (packed) {
                auto decoded = graphics::decodePs2WorldMesh(mesh.nativeData, info->triangleStrips);
                return decoded ? std::expected<std::size_t, Error>(decoded->vertices.size())
                               : std::unexpected(std::move(decoded.error()));
            }
            auto decoded = graphics::decodePs2DefaultMesh(mesh.nativeData, info->triangleStrips);
            return decoded ? std::expected<std::size_t, Error>(decoded->vertices.size())
                           : std::unexpected(std::move(decoded.error()));
        }();
        if (!count) {
            return fail(count.error().code, std::format("mesh {}: {}", i, count.error().message));
        }
        if (*count != mesh.indexCount) {
            return fail(ErrorCode::Invalid, std::format("mesh {} decodes to {} vertices; its mesh record says {}", i,
                                                        *count, mesh.indexCount));
        }
    }

    // The geometry, read on its own: outside a clump it sits inside the atomic, which librw's atomic reader (written
    // for clumps, where the geometry list comes first) does not expect.
    rw::Geometry* geometry = readGeometry(info->geometry);
    if (geometry == nullptr) {
        return fail(ErrorCode::Invalid, "librw could not read the atomic's geometry");
    }

    // Then the atomic as librw reads one in a clump: a struct naming frame 0 and geometry 0, then the extension.
    std::vector<std::byte> clumpForm;
    const auto appendU32 = [&clumpForm](std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            clumpForm.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        }
    };
    appendU32(graphics::kRwStruct);
    appendU32(16);
    appendU32(graphics::kRwLibraryStamp);
    appendU32(0); // frame index
    appendU32(0); // geometry index
    appendU32(info->atomicFlags);
    appendU32(0);
    clumpForm.insert(clumpForm.end(), info->extension.begin(), info->extension.end());
    std::vector<rw::uint8> copy = copyForLibrw(clumpForm);
    rw::StreamMemory memory;
    memory.open(copy.data(), static_cast<rw::uint32>(copy.size()), static_cast<rw::uint32>(copy.size()));
    rw::Frame* frame = rw::Frame::create();
    rw::FrameList_ frames{1, &frame};
    rw::Atomic* atomic = rw::Atomic::streamReadClump(&memory, &frames, &geometry);
    memory.close();
    geometry->destroy(); // the atomic holds its own reference
    if (atomic == nullptr) {
        frame->destroy();
        return fail(ErrorCode::Invalid, "librw could not read the atomic");
    }
    if (!packed) {
        atomic->pipeline = &defaultLayoutPipeline; // no right to render: RenderWare's default pipeline
    }
    if (atomic->pipeline == nullptr) {
        // The rights callback did not run: the world plugins were attached to another engine.
        atomic->destroy();
        frame->destroy();
        return fail(ErrorCode::Invalid, "librw did not give the atomic the game's pipeline");
    }

    // The game translates the atomic's frame to its sector's origin (world.md, part file).
    const rw::V3d translation{origin.x, origin.y, origin.z};
    frame->translate(&translation, rw::COMBINEREPLACE);

    // The spans point into the caller's buffer; keep only the values.
    info->geometry = {};
    info->extension = {};
    for (world::MeshInfo& mesh : info->meshes) {
        mesh.nativeData = {};
    }
    return WorldAtomic(atomic, std::move(*info));
}

WorldAtomic::WorldAtomic(WorldAtomic&& other) noexcept
    : m_atomic(std::exchange(other.m_atomic, nullptr)), m_info(std::move(other.m_info)) {}

WorldAtomic& WorldAtomic::operator=(WorldAtomic&& other) noexcept {
    if (this != &other) {
        destroy();
        m_atomic = std::exchange(other.m_atomic, nullptr);
        m_info = std::move(other.m_info);
    }
    return *this;
}

WorldAtomic::~WorldAtomic() { destroy(); }

void WorldAtomic::destroy() noexcept {
    if (m_atomic != nullptr) {
        rw::Frame* frame = m_atomic->getFrame();
        if (rw::Atomic* layer = dualLayerOf(m_atomic); layer != nullptr) {
            setDualLayer(m_atomic, nullptr);
            layer->setFrame(nullptr);
            layer->destroy();
        }
        m_atomic->destroy();
        if (frame != nullptr) {
            frame->destroy();
        }
        m_atomic = nullptr;
    }
}

void WorldAtomic::setTransform(const world::FrameMatrix& transform) {
    rw::Matrix matrix;
    matrix.setIdentity();
    matrix.right = rw::V3d{transform.right.x, transform.right.y, transform.right.z};
    matrix.up = rw::V3d{transform.up.x, transform.up.y, transform.up.z};
    matrix.at = rw::V3d{transform.at.x, transform.at.y, transform.at.z};
    matrix.pos = rw::V3d{transform.position.x, transform.position.y, transform.position.z};
    matrix.update(); // no longer the identity setIdentity() marked it as
    m_atomic->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
}

void WorldAtomic::unpack() {
    if ((m_atomic->geometry->flags & rw::Geometry::NATIVE) == 0) {
        return;
    }
    m_atomic->uninstance(); // through Coney's stand-in pipeline: uninstanceGameAtomic or uninstanceDefaultAtomic
    m_atomic->pipeline = nullptr;
    if (dualLayerOf(m_atomic) == nullptr) {
        setDualLayer(m_atomic, makeDualLayer(m_atomic));
    }
}

} // namespace coney::platform
