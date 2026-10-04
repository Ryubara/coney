// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_atomic.h"

#include <algorithm>
#include <array>
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

// Coney's stand-ins for the game's two PS2 atomic pipelines. They only unpack: drawing happens after the atomic has
// been handed to the platform's default pipeline (WorldAtomic::unpack).
std::array<rw::ObjPipeline, 2> gamePipelines{};

// The vertex scales of `atomic`'s geometry and the packed vertices of every mesh, decoded again from the native data
// librw kept. Returns false if anything does not decode (WorldAtomic::read has checked the same data, so only an
// atomic that did not come through it can fail).
bool decodeMeshes(rw::Atomic* atomic, std::vector<graphics::Ps2WorldMesh>& meshes) {
    rw::Geometry* geometry = atomic->geometry;
    if (geometry->instData == nullptr || geometry->instData->platform != rw::PLATFORM_PS2 ||
        geometry->meshHeader == nullptr) {
        return false;
    }
    const auto* header = static_cast<const rw::ps2::InstanceDataHeader*>(geometry->instData);
    if (header->numMeshes != geometry->meshHeader->numMeshes) {
        return false;
    }
    const bool strip = geometry->meshHeader->flags == rw::MeshHeader::TRISTRIP;
    const rw::Mesh* mesh = geometry->meshHeader->getMeshes();
    for (rw::uint32 i = 0; i < header->numMeshes; ++i) {
        const rw::ps2::InstanceData& instance = header->instanceMeshes[i];
        auto decoded = graphics::decodePs2WorldMesh(
            std::span<const std::byte>(reinterpret_cast<const std::byte*>(instance.data), instance.dataSize), strip);
        if (!decoded || decoded->vertices.size() != mesh[i].numIndices) {
            return false;
        }
        meshes.push_back(std::move(*decoded));
    }
    return true;
}

// Ordering key of a packed vertex, so that identical vertices of a geometry share one index (strips join through
// repeated vertices, and librw finds degenerate triangles by equal indices).
using VertexKey = std::array<std::int32_t, 16>;

// The key of `vertex`: every component, in order.
VertexKey keyOf(const graphics::Ps2PackedVertex& vertex) {
    VertexKey key{};
    for (std::size_t c = 0; c < 4; ++c) {
        key[c] = vertex.position[c];
        key[4 + c] = vertex.texCoords[c];
        key[8 + c] = vertex.colour[c];
        key[12 + c] = static_cast<std::uint8_t>(vertex.normal[c]); // the bits are all that matter here
    }
    return key;
}

// A prelighting colour channel for librw, where 255 is full brightness. The PS2's GS modulates a texel by a vertex
// colour with 0x80 as 1.0, so the packed channels are doubled and clamped (a Coney choice inferred from the GS: the
// disc's values average about 14 and rarely pass 128, and look right doubled; alpha, always 255 here, is kept).
rw::uint8 doubled(std::uint8_t channel) { return static_cast<rw::uint8>(std::min(255, channel * 2)); }

// Replaces `atomic`'s PS2 native geometry with plain geometry: librw's uninstance step for the game's pipelines.
// Positions are the packed integers times the atomic's position scale (0x3F0 +0x00); texture coordinates are
// scaled by its second scale (the world viewer shows textures right only with it, world.md); normals are signed bytes
// over 128 as librw's PS2 code reads them; colours are doubled (doubled()).
void uninstanceGameAtomic(rw::ObjPipeline* /*pipeline*/, rw::Atomic* atomic) {
    rw::Geometry* geometry = atomic->geometry;
    std::vector<graphics::Ps2WorldMesh> meshes;
    if ((geometry->flags & rw::Geometry::NATIVE) == 0 || !decodeMeshes(atomic, meshes)) {
        return;
    }
    const world::AtomicPluginData scales = atomicPluginData(atomic);

    // Number the distinct vertices; librw's indices are 16 bits.
    std::map<VertexKey, rw::uint16> indexOf;
    std::vector<const graphics::Ps2PackedVertex*> unique;
    std::vector<std::vector<rw::uint16>> indices(meshes.size());
    rw::int32 triangleBound = 0;
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        for (const graphics::Ps2PackedVertex& vertex : meshes[m].vertices) {
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

    // Fill the vertices.
    const std::uint32_t uvSets = meshes.empty() ? 0 : meshes[0].attributes.texCoordSets;
    const bool hasColour = !meshes.empty() && meshes[0].attributes.colours;
    const bool hasNormal = !meshes.empty() && meshes[0].attributes.normals;
    rw::MorphTarget& target = geometry->morphTargets[0];
    for (std::size_t i = 0; i < unique.size(); ++i) {
        const graphics::Ps2PackedVertex& v = *unique[i];
        target.vertices[i] = rw::makeV3d(static_cast<float>(v.position[0]) * scales.positionScale,
                                         static_cast<float>(v.position[1]) * scales.positionScale,
                                         static_cast<float>(v.position[2]) * scales.positionScale);
        if (target.normals != nullptr) {
            target.normals[i] = hasNormal ? rw::makeV3d(static_cast<float>(v.normal[0]) / 128.0F,
                                                        static_cast<float>(v.normal[1]) / 128.0F,
                                                        static_cast<float>(v.normal[2]) / 128.0F)
                                          : rw::makeV3d(0.0F, 0.0F, 1.0F);
        }
        if (geometry->colors != nullptr) {
            geometry->colors[i] =
                hasColour ? rw::makeRGBA(doubled(v.colour[0]), doubled(v.colour[1]), doubled(v.colour[2]), v.colour[3])
                          : rw::makeRGBA(255, 255, 255, 255);
        }
        for (rw::int32 set = 0; set < geometry->numTexCoordSets && set < 2; ++set) {
            const auto first = static_cast<std::size_t>(set) * 2;
            geometry->texCoords[set][i] =
                static_cast<std::uint32_t>(set) < uvSets
                    ? rw::TexCoords{static_cast<float>(v.texCoords[first]) * scales.secondScale,
                                    static_cast<float>(v.texCoords[first + 1]) * scales.secondScale}
                    : rw::TexCoords{0.0F, 0.0F};
        }
    }

    // The meshes keep their materials and counts; give them their indices, then derive the triangles.
    rw::Mesh* mesh = geometry->meshHeader->getMeshes();
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        std::memcpy(mesh[m].indices, indices[m].data(), indices[m].size() * sizeof(rw::uint16));
    }
    geometry->generateTriangles();
    rw::ps2::destroyNativeData(geometry, 0, 0);
    geometry->flags &= ~static_cast<rw::uint32>(rw::Geometry::NATIVE);
    geometry->calculateBoundingSphere();
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
    rw::Atomic::registerPlugin(0, kGamePipelinePlugin, nullptr, nullptr, nullptr);
    rw::Atomic::setStreamRightsCallback(kGamePipelinePlugin, gamePipelineRights);
}

world::AtomicPluginData atomicPluginData(const rw::Atomic* atomic) {
    CONEY_ASSERT(atomicPluginOffset >= 0 && atomic != nullptr);
    // Copied out byte by byte: the plugin's room is raw memory inside librw's atomic, written the same way.
    world::AtomicPluginData data;
    std::memcpy(&data, reinterpret_cast<const std::byte*>(atomic) + atomicPluginOffset, sizeof(data));
    return data;
}

std::expected<WorldAtomic, Error> WorldAtomic::read(std::span<const std::byte> section, world::Vec3 origin) {
    CONEY_ASSERT(atomicPluginOffset >= 0);
    auto info = world::inspectAtomicSection(section);
    if (!info) {
        return std::unexpected(std::move(info.error()));
    }
    if (info->pipelinePlugin != kGamePipelinePlugin ||
        (info->pipeline != kGameAtomicPipelineA && info->pipeline != kGameAtomicPipelineB)) {
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
        auto decoded = graphics::decodePs2WorldMesh(mesh.nativeData, info->triangleStrips);
        if (!decoded) {
            return fail(decoded.error().code, std::format("mesh {}: {}", i, decoded.error().message));
        }
        if (decoded->vertices.size() != mesh.indexCount) {
            return fail(ErrorCode::Invalid, std::format("mesh {} decodes to {} vertices; its mesh record says {}", i,
                                                        decoded->vertices.size(), mesh.indexCount));
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
        m_atomic->destroy();
        if (frame != nullptr) {
            frame->destroy();
        }
        m_atomic = nullptr;
    }
}

void WorldAtomic::unpack() {
    if ((m_atomic->geometry->flags & rw::Geometry::NATIVE) == 0) {
        return;
    }
    m_atomic->uninstance(); // through the game pipeline's stand-in: uninstanceGameAtomic
    m_atomic->pipeline = nullptr;
}

} // namespace coney::platform
