// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/draw_calls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <rw.h>

#include "platform/character_skin.h"
#include "platform/draw_stream.h"
#include "platform/texture_lod.h"

namespace coney::platform::draw {

namespace {

// The render states librw has, TEXTURERASTER (0) to GSALPHATESTREF.
constexpr std::size_t kStateCount = static_cast<std::size_t>(rw::GSALPHATESTREF) + 1;

// The states as librw's GL3 device starts them (resetRenderState in gl3device.cpp), which the layer reads back for a
// state nobody has set while it does not forward: so the GPU renderer, which reads them here, draws in the states GL3
// would (above all the alpha test, GEQUAL 10: docs/engine/gpu-renderer.md#scene). Every other state starts at 0.
std::array<rw::uint32, kStateCount> gl3StartStates() {
    std::array<rw::uint32, kStateCount> states{};
    const auto set = [&states](rw::int32 state, rw::uint32 value) { states[static_cast<std::size_t>(state)] = value; };
    set(rw::ALPHATESTFUNC, rw::ALPHAGREATEREQUAL);
    set(rw::ALPHATESTREF, 10);
    set(rw::GSALPHATESTREF, 128);
    set(rw::SRCBLEND, rw::BLENDSRCALPHA);
    set(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    set(rw::ZWRITEENABLE, 1);
    set(rw::ZTESTENABLE, 1);
    set(rw::CULLMODE, rw::CULLNONE);
    set(rw::FOGCOLOR, 0xFFFFFFFFU);
    set(rw::STENCILFAIL, rw::STENCILKEEP);
    set(rw::STENCILZFAIL, rw::STENCILKEEP);
    set(rw::STENCILPASS, rw::STENCILKEEP);
    set(rw::STENCILFUNCTION, rw::STENCILALWAYS);
    set(rw::STENCILFUNCTIONMASK, 0xFFFFFFFFU);
    set(rw::STENCILFUNCTIONWRITEMASK, 0xFFFFFFFFU);
    return states;
}

// What is known of a raster Coney made: the digest of its pixels, from its uploads while rasters were tracked.
struct RasterFacts {
    std::uint64_t digest = 0;
    bool digested = false;
};

// The layer's state, global as librw's own is.
struct Layer {
    bool forwarding = true;
    bool trackRasters = false;
    std::array<rw::uint32, kStateCount> states = gl3StartStates(); // the values; a recording holds only those set
    std::array<bool, kStateCount> stateSet{};
    void* textureRaster = nullptr; // TEXTURERASTER, which is a pointer
    std::optional<DrawStream> stream;
    std::unordered_map<const rw::Raster*, std::uint32_t> recordedRasters; // order of first use in the recording
    std::unordered_map<const rw::Raster*, RasterFacts> rasters;
    std::unordered_map<const rw::Raster*, std::vector<rw::uint8>> lockBuffers; // not forwarding: the locked level
    // The last Im3D transform's vertices, world matrix and flags, which the next draws take.
    std::vector<rw::gl3::Im3DVertex> im3dVertices;
    std::optional<rw::Matrix> im3dWorld;
    rw::uint32 im3dFlags = 0;
};

// The one layer, made on first use.
Layer& layer() {
    static Layer instance;
    return instance;
}

// The stream being recorded, or null.
DrawStream* stream() {
    std::optional<DrawStream>& recorded = layer().stream;
    return recorded.has_value() ? &recorded.value() : nullptr;
}

// Whether `state` is one of librw's render states.
bool knownState(rw::int32 state) { return state >= 0 && static_cast<std::size_t>(state) < kStateCount; }

// Writes `raster` as draws see it: 0 for none, else its place in the recording's order of first use, its size,
// depth, type and format, and its pixel digest where one is known.
void writeRaster(DrawStream& out, const rw::Raster* raster) {
    if (raster == nullptr) {
        out.word(0);
        return;
    }
    Layer& state = layer();
    const auto found =
        state.recordedRasters.emplace(raster, static_cast<std::uint32_t>(state.recordedRasters.size() + 1)).first;
    out.word(found->second);
    out.word(static_cast<std::uint32_t>(raster->width));
    out.word(static_cast<std::uint32_t>(raster->height));
    out.word(static_cast<std::uint32_t>(raster->depth));
    out.word(static_cast<std::uint32_t>(raster->type));
    out.word(static_cast<std::uint32_t>(raster->format));
    const auto facts = state.rasters.find(raster);
    const bool digested = facts != state.rasters.end() && facts->second.digested;
    out.word(digested ? 1 : 0);
    out.digest(digested ? facts->second.digest : 0);
}

// Writes every render state set so far, by number, the raster as writeRaster() does.
void writeStates(DrawStream& out) {
    const Layer& state = layer();
    for (std::size_t i = 0; i < kStateCount; ++i) {
        if (!state.stateSet[i]) {
            continue;
        }
        out.word(static_cast<std::uint32_t>(i));
        if (i == static_cast<std::size_t>(rw::TEXTURERASTER)) {
            writeRaster(out, static_cast<const rw::Raster*>(state.textureRaster));
        } else {
            out.word(state.states[i]);
        }
    }
    out.word(0xFFFFFFFFU); // the end of the states
}

// Writes `matrix`'s axes and position on the grid.
void writeMatrix(DrawStream& out, const rw::Matrix& matrix) {
    for (const rw::V3d* axis : {&matrix.right, &matrix.up, &matrix.at, &matrix.pos}) {
        out.gridValue(axis->x);
        out.gridValue(axis->y);
        out.gridValue(axis->z);
    }
}

// Writes 16-bit `indices`, counted.
void writeIndices(DrawStream& out, const void* indices, rw::int32 count) {
    out.word(static_cast<std::uint32_t>(std::max(count, 0)));
    const auto* index = static_cast<const std::uint16_t*>(indices);
    for (rw::int32 i = 0; index != nullptr && i < count; ++i) {
        out.word(index[i]);
    }
}

// Records an Im2D draw of `vertices`, with `indices` when given.
void recordIm2D(int type, const void* vertices, rw::int32 numVertices, const void* indices, rw::int32 numIndices) {
    DrawStream* out = stream();
    if (out == nullptr) {
        return;
    }
    out->begin(DrawRecord::Im2DDraw);
    out->word(static_cast<std::uint32_t>(type));
    writeStates(*out);
    const auto* vertex = static_cast<const rw::gl3::Im2DVertex*>(vertices);
    const rw::int32 count = vertex != nullptr ? std::max(numVertices, 0) : 0;
    out->word(static_cast<std::uint32_t>(count));
    out->countVertices(static_cast<std::uint32_t>(count));
    for (rw::int32 i = 0; i < count; ++i) {
        const rw::gl3::Im2DVertex& v = vertex[i];
        out->gridValue(v.x);
        out->gridValue(v.y);
        out->gridValue(v.z);
        out->gridValue(v.w);
        out->colour(v.r, v.g, v.b, v.a);
        out->gridValue(v.u);
        out->gridValue(v.v);
    }
    writeIndices(*out, indices, indices != nullptr ? numIndices : 0);
}

// Records an Im3D draw of the last transform's vertices, with `indices` when given.
void recordIm3D(int type, const void* indices, rw::int32 numIndices) {
    DrawStream* out = stream();
    if (out == nullptr) {
        return;
    }
    const Layer& state = layer();
    out->begin(DrawRecord::Im3DDraw);
    out->word(static_cast<std::uint32_t>(type));
    writeStates(*out);
    out->word(state.im3dFlags);
    out->word(state.im3dWorld ? 1 : 0);
    if (state.im3dWorld) {
        writeMatrix(*out, *state.im3dWorld);
    }
    out->word(static_cast<std::uint32_t>(state.im3dVertices.size()));
    out->countVertices(static_cast<std::uint32_t>(state.im3dVertices.size()));
    for (const rw::gl3::Im3DVertex& v : state.im3dVertices) {
        out->gridValue(v.position.x);
        out->gridValue(v.position.y);
        out->gridValue(v.position.z);
        out->gridValue(v.normal.x);
        out->gridValue(v.normal.y);
        out->gridValue(v.normal.z);
        out->colour(v.r, v.g, v.b, v.a);
        out->gridValue(v.u);
        out->gridValue(v.v);
    }
    writeIndices(*out, indices, indices != nullptr ? numIndices : 0);
}

// Writes a colour of librw's floats (a light's, which may pass 1) on the grid, alpha included.
void writeColourValues(DrawStream& out, const rw::RGBAf& colour) {
    out.gridValue(colour.red);
    out.gridValue(colour.green);
    out.gridValue(colour.blue);
    out.gridValue(colour.alpha);
}

// Writes the lights of `list` (a world's global or local lights) in their order: type, flags, colour, the frame's
// position and direction, radius and cone; a count first.
void writeLights(DrawStream& out, rw::LinkList& list) {
    std::uint32_t count = 0;
    FORLIST(link, list) { ++count; }
    out.word(count);
    FORLIST(link, list) {
        rw::Light* light = rw::Light::fromWorld(link);
        out.word(static_cast<std::uint32_t>(light->getType()));
        out.word(light->getFlags());
        writeColourValues(out, light->color);
        rw::Frame* frame = light->getFrame();
        out.word(frame != nullptr ? 1 : 0);
        if (frame != nullptr) {
            const rw::Matrix* ltm = frame->getLTM();
            for (const rw::V3d* axis : {&ltm->pos, &ltm->at}) {
                out.gridValue(axis->x);
                out.gridValue(axis->y);
                out.gridValue(axis->z);
            }
        }
        out.gridValue(light->radius);
        out.gridValue(light->minusCosAngle);
    }
}

// Writes `geometry` as the atomic's pipeline reads it: flags and counts, the first morph target's positions and
// normals, the prelighting, the texture coordinates and the triangles, each array marked present or not.
void writeGeometry(DrawStream& out, const rw::Geometry& geometry) {
    out.word(geometry.flags);
    const auto vertices = static_cast<std::uint32_t>(std::max(geometry.numVertices, 0));
    const auto triangles = static_cast<std::uint32_t>(std::max(geometry.numTriangles, 0));
    out.word(vertices);
    out.word(triangles);
    out.word(static_cast<std::uint32_t>(geometry.numMorphTargets));
    out.word(static_cast<std::uint32_t>(geometry.numTexCoordSets));
    out.countVertices(vertices);
    const rw::MorphTarget* target = geometry.numMorphTargets > 0 ? &geometry.morphTargets[0] : nullptr;
    // A vector array on the grid, or a word saying there is none.
    const auto writeVectors = [&out, vertices](const rw::V3d* array) {
        out.word(array != nullptr ? 1 : 0);
        for (std::uint32_t i = 0; array != nullptr && i < vertices; ++i) {
            out.gridValue(array[i].x);
            out.gridValue(array[i].y);
            out.gridValue(array[i].z);
        }
    };
    writeVectors(target != nullptr ? target->vertices : nullptr);
    writeVectors(target != nullptr ? target->normals : nullptr);
    out.word(geometry.colors != nullptr ? 1 : 0);
    for (std::uint32_t i = 0; geometry.colors != nullptr && i < vertices; ++i) {
        const rw::RGBA& c = geometry.colors[i];
        out.colour(c.red, c.green, c.blue, c.alpha);
    }
    const rw::int32 sets = std::clamp<rw::int32>(geometry.numTexCoordSets, 0, 8);
    for (rw::int32 set = 0; set < sets; ++set) {
        const rw::TexCoords* uv = geometry.texCoords[set];
        out.word(uv != nullptr ? 1 : 0);
        for (std::uint32_t i = 0; uv != nullptr && i < vertices; ++i) {
            out.gridValue(uv[i].u);
            out.gridValue(uv[i].v);
        }
    }
    out.word(geometry.triangles != nullptr ? 1 : 0);
    for (std::uint32_t i = 0; geometry.triangles != nullptr && i < triangles; ++i) {
        const rw::Triangle& t = geometry.triangles[i];
        out.word(static_cast<std::uint32_t>(t.v[0]) | static_cast<std::uint32_t>(t.v[1]) << 16U);
        out.word(static_cast<std::uint32_t>(t.v[2]) | static_cast<std::uint32_t>(t.matId) << 16U);
    }
}

// Writes what a character on the skinned pipeline is posed by, which its geometry (the bind positions) does not hold:
// each vertex's weights and bones, then the palette's matrices the bones reach. Nothing for any other atomic, so the
// records of the world's, the objects' and the CPU-skinned characters' draws are as they were.
void writeSkin(DrawStream& out, const rw::Atomic& atomic) {
    const BonePalette* palette = characterSkinPalette(&atomic);
    const SkinVertexData* vertices = characterSkinVertices(&atomic);
    if (palette == nullptr || vertices == nullptr) {
        return;
    }
    out.word(static_cast<std::uint32_t>(vertices->weights.size()));
    for (std::size_t i = 0; i < vertices->weights.size(); ++i) {
        for (std::size_t slot = 0; slot < 4; ++slot) {
            out.gridValue(vertices->weights[i][slot]);
            out.word(vertices->indices[i][slot]);
        }
    }
    const std::size_t bones = std::min(vertices->bones, kMaxSkinBones);
    out.word(static_cast<std::uint32_t>(bones));
    for (std::size_t i = 0; i < bones * kBoneFloats; ++i) {
        out.gridValue((*palette)[i]);
    }
}

// Records a render of `atomic`: the states, the distance levels switch, its frame, its geometry, its materials and
// the lights librw lights it with (renderAtomic() says which).
void recordAtomic(rw::Atomic* atomic) {
    DrawStream* out = stream();
    if (out == nullptr) {
        return;
    }
    out->begin(DrawRecord::AtomicDraw);
    writeStates(*out);
    out->word(distanceMipmaps() ? 1 : 0);
    rw::Frame* frame = atomic->getFrame();
    out->word(frame != nullptr ? 1 : 0);
    if (frame != nullptr) {
        writeMatrix(*out, *frame->getLTM());
    }
    const rw::Geometry* geometry = atomic->geometry;
    out->word(geometry != nullptr ? 1 : 0);
    if (geometry == nullptr) {
        return;
    }
    writeGeometry(*out, *geometry);
    out->word(static_cast<std::uint32_t>(std::max(geometry->matList.numMaterials, 0)));
    for (rw::int32 m = 0; m < geometry->matList.numMaterials; ++m) {
        const rw::Material* material = geometry->matList.materials[m];
        out->colour(material->color.red, material->color.green, material->color.blue, material->color.alpha);
        out->gridValue(material->surfaceProps.ambient);
        out->gridValue(material->surfaceProps.specular);
        out->gridValue(material->surfaceProps.diffuse);
        rw::Texture* texture = material->texture;
        out->word(texture != nullptr ? 1 : 0);
        if (texture != nullptr) {
            out->word(static_cast<std::uint32_t>(texture->filterAddressing));
            writeRaster(*out, texture->raster);
        }
    }
    // librw lights an atomic from the current world, and that world's local lights only when the atomic is in it.
    rw::World* world = rw::engine->currentWorld;
    out->word(world != nullptr ? 1 : 0);
    if (world != nullptr) {
        writeLights(*out, world->globalLights);
        out->word(atomic->world == world ? 1 : 0);
        if (atomic->world == world) {
            writeLights(*out, world->localLights);
        }
    }
    writeSkin(*out, *atomic);
}

// Starts a record of `kind` with `values` after it, when a recording runs.
void recordWords(DrawRecord kind, std::initializer_list<std::uint32_t> values) {
    if (DrawStream* out = stream(); out != nullptr) {
        out->begin(kind);
        for (const std::uint32_t value : values) {
            out->word(value);
        }
    }
}

// Notes an upload of `bytes` to `raster`'s `level`: the raster's digest takes it in, and a recording gets a record.
void noteUpload(const rw::Raster* raster, rw::int32 level, std::span<const std::uint8_t> bytes, rw::int32 width,
                rw::int32 height) {
    Layer& state = layer();
    if (!state.trackRasters || raster == nullptr) {
        return;
    }
    const std::uint64_t pixels = pixelDigest(bytes);
    RasterFacts& facts = state.rasters[raster];
    // The raster's digest chains its uploads in order: the old digest, the level, the level's pixels.
    std::array<std::uint8_t, 20> chained{};
    for (std::size_t i = 0; i < 8; ++i) {
        chained[i] = static_cast<std::uint8_t>(facts.digest >> (8 * i));
        chained[12 + i] = static_cast<std::uint8_t>(pixels >> (8 * i));
    }
    for (std::size_t i = 0; i < 4; ++i) {
        chained[8 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(level) >> (8 * i));
    }
    facts.digest = pixelDigest(chained);
    facts.digested = true;
    if (DrawStream* out = stream(); out != nullptr) {
        out->begin(DrawRecord::RasterUpload);
        out->word(static_cast<std::uint32_t>(level));
        out->word(static_cast<std::uint32_t>(width));
        out->word(static_cast<std::uint32_t>(height));
        out->digest(pixels);
    }
}

// Notes a raster just made, of no known content yet; a recording gets a record of its shape.
void noteCreated(const rw::Raster* raster) {
    if (raster == nullptr) {
        return;
    }
    layer().rasters.erase(raster); // a raster at a freed one's address starts with no digest
    layer().recordedRasters.erase(raster);
    recordWords(DrawRecord::RasterCreate,
                {static_cast<std::uint32_t>(raster->width), static_cast<std::uint32_t>(raster->height),
                 static_cast<std::uint32_t>(raster->depth), static_cast<std::uint32_t>(raster->type),
                 static_cast<std::uint32_t>(raster->format)});
}

// A PS2 raster's texels then its palette: the bytes read from the disc and nothing else. A new-style raster's data
// block is not only that: it starts with a pointer to its own texels (a heap address) and has alignment gaps librw
// never fills, both of which differ from run to run, so the hash of two runs of one frame differed. Its texel and
// palette blocks are taken from where librw reads them into it (ps2raster.cpp, readNativeTexture: the texel block at
// that pointer, the palette block from 0x50 bytes before `palette`, each with its GIF tags first).
std::optional<std::vector<std::uint8_t>> ps2Texels(const rw::Raster& raster) {
    const rw::ps2::Ps2Raster* native = GETPS2RASTEREXT(&raster);
    if (native->data == nullptr) {
        return std::nullopt;
    }
    const rw::uint8* begin = native->data;
    const rw::uint8* end = native->data + native->dataSize;
    // Whether `size` bytes from `at` lie inside the data block.
    const auto inside = [begin, end](const rw::uint8* at, std::size_t size) {
        return at >= begin && at <= end && size <= static_cast<std::size_t>(end - at);
    };
    if ((native->flags & rw::ps2::Ps2Raster::NEWSTYLE) == 0) {
        // An old-style raster's block is its texels and palette, whole.
        return std::vector<std::uint8_t>(begin, end);
    }
    const auto* pointers = reinterpret_cast<const rw::ps2::Ps2Raster::PixelPtr*>(begin);
    if (!inside(pointers->pixels, native->pixelSize)) {
        // An old file's block was read whole over the pointer: all of it but the pointer's 16 bytes is from the disc.
        constexpr std::ptrdiff_t kPointerBytes = sizeof(rw::ps2::Ps2Raster::PixelPtr);
        return std::vector<std::uint8_t>(begin + std::min(kPointerBytes, end - begin), end);
    }
    std::vector<std::uint8_t> bytes(pointers->pixels, pointers->pixels + native->pixelSize);
    // The palette block starts 0x50 bytes before `palette`; checked against the block before the subtraction is made.
    constexpr std::ptrdiff_t kPaletteTags = 0x50;
    if (native->paletteSize != 0 && raster.palette != nullptr && raster.palette >= begin &&
        raster.palette - begin >= kPaletteTags && inside(raster.palette - kPaletteTags, native->paletteSize)) {
        const rw::uint8* palette = raster.palette - kPaletteTags;
        bytes.insert(bytes.end(), palette, palette + native->paletteSize);
    }
    return bytes;
}

} // namespace

void reset(bool forwardCalls) {
    layer() = Layer{};
    layer().forwarding = forwardCalls;
}

void setForwarding(bool on) { layer().forwarding = on; }

bool forwarding() { return layer().forwarding; }

void setTrackRasters(bool on) { layer().trackRasters = on; }

void startRecording() {
    layer().stream.emplace();
    layer().recordedRasters.clear();
}

bool recording() { return layer().stream.has_value(); }

std::optional<DrawStream> stopRecording() {
    std::optional<DrawStream> result = std::exchange(layer().stream, std::nullopt);
    layer().recordedRasters.clear();
    return result;
}

void setRenderState(rw::int32 state, rw::uint32 value) {
    Layer& current = layer();
    if (knownState(state)) {
        const auto index = static_cast<std::size_t>(state);
        current.states[index] = value;
        current.stateSet[index] = true;
        // librw's TEXTUREADDRESS sets both directions.
        if (state == rw::TEXTUREADDRESS) {
            for (const rw::int32 axis : {rw::TEXTUREADDRESSU, rw::TEXTUREADDRESSV}) {
                current.states[static_cast<std::size_t>(axis)] = value;
                current.stateSet[static_cast<std::size_t>(axis)] = true;
            }
        }
    }
    if (current.forwarding) {
        rw::SetRenderState(state, value);
    }
}

void setRenderStatePtr(rw::int32 state, void* value) {
    Layer& current = layer();
    if (state == rw::TEXTURERASTER) {
        current.textureRaster = value;
        current.stateSet[static_cast<std::size_t>(rw::TEXTURERASTER)] = true;
    }
    if (current.forwarding) {
        rw::SetRenderStatePtr(state, value);
    }
}

rw::uint32 getRenderState(rw::int32 state) {
    const Layer& current = layer();
    if (current.forwarding) {
        return rw::GetRenderState(state);
    }
    return knownState(state) ? current.states[static_cast<std::size_t>(state)] : 0;
}

float im2dNearZ() { return rw::im2d::GetNearZ(); }

void im2dRenderPrimitive(int type, void* vertices, rw::int32 numVertices) {
    recordIm2D(type, vertices, numVertices, nullptr, 0);
    if (forwarding()) {
        rw::im2d::RenderPrimitive(static_cast<rw::PrimitiveType>(type), vertices, numVertices);
    }
}

void im2dRenderIndexedPrimitive(int type, void* vertices, rw::int32 numVertices, void* indices, rw::int32 numIndices) {
    recordIm2D(type, vertices, numVertices, indices, numIndices);
    if (forwarding()) {
        rw::im2d::RenderIndexedPrimitive(static_cast<rw::PrimitiveType>(type), vertices, numVertices, indices,
                                         numIndices);
    }
}

void im3dTransform(void* vertices, rw::int32 numVertices, rw::Matrix* world, rw::uint32 flags) {
    Layer& current = layer();
    // Kept only while a recording would use them.
    if (current.stream) {
        const auto* first = static_cast<const rw::gl3::Im3DVertex*>(vertices);
        const rw::int32 count = first != nullptr ? std::max(numVertices, 0) : 0;
        current.im3dVertices.assign(first, first + count);
        current.im3dWorld = world != nullptr ? std::optional<rw::Matrix>(*world) : std::nullopt;
        current.im3dFlags = flags;
    }
    if (current.forwarding) {
        rw::im3d::Transform(vertices, numVertices, world, flags);
    }
}

void im3dRenderPrimitive(int type) {
    recordIm3D(type, nullptr, 0);
    if (forwarding()) {
        rw::im3d::RenderPrimitive(static_cast<rw::PrimitiveType>(type));
    }
}

void im3dRenderIndexedPrimitive(int type, void* indices, rw::int32 numIndices) {
    recordIm3D(type, indices, numIndices);
    if (forwarding()) {
        rw::im3d::RenderIndexedPrimitive(static_cast<rw::PrimitiveType>(type), indices, numIndices);
    }
}

void im3dEnd() {
    Layer& current = layer();
    current.im3dVertices.clear();
    current.im3dWorld.reset();
    current.im3dFlags = 0;
    if (current.forwarding) {
        rw::im3d::End();
    }
}

void renderAtomic(rw::Atomic* atomic) {
    if (atomic == nullptr) {
        return;
    }
    recordAtomic(atomic);
    if (forwarding()) {
        atomic->render();
    }
}

void clearCamera(rw::Camera* camera, rw::RGBA* colour, rw::uint32 mode) {
    if (DrawStream* out = stream(); out != nullptr) {
        out->begin(DrawRecord::CameraClear);
        out->word(mode);
        if (colour != nullptr) {
            out->colour(colour->red, colour->green, colour->blue, colour->alpha);
        } else {
            out->colour(0, 0, 0, 0);
        }
    }
    if (forwarding()) {
        camera->clear(colour, mode);
    }
}

// A null camera is tolerated, in begin and end alike (no caller passes one today): the record says "no camera" and
// nothing reaches librw, so neither call can crash on it. The same guard stands in endCameraUpdate.
void beginCameraUpdate(rw::Camera* camera) {
    if (DrawStream* out = stream(); out != nullptr) {
        out->begin(DrawRecord::CameraBegin);
        rw::Frame* frame = camera != nullptr ? camera->getFrame() : nullptr;
        out->word(frame != nullptr ? 1 : 0);
        if (frame != nullptr) {
            writeMatrix(*out, *frame->getLTM());
        }
        if (camera != nullptr) {
            out->gridValue(camera->viewWindow.x);
            out->gridValue(camera->viewWindow.y);
            out->gridValue(camera->viewOffset.x);
            out->gridValue(camera->viewOffset.y);
            out->gridValue(camera->nearPlane);
            out->gridValue(camera->farPlane);
            out->gridValue(camera->fogPlane);
            out->word(static_cast<std::uint32_t>(camera->projection));
            const rw::Raster* target = camera->frameBuffer;
            out->word(target != nullptr ? static_cast<std::uint32_t>(target->width) : 0);
            out->word(target != nullptr ? static_cast<std::uint32_t>(target->height) : 0);
        }
    }
    if (forwarding() && camera != nullptr) {
        camera->beginUpdate();
    }
}

void endCameraUpdate(rw::Camera* camera) {
    recordWords(DrawRecord::CameraEnd, {});
    if (forwarding() && camera != nullptr) {
        camera->endUpdate();
    }
}

void showCameraRaster(rw::Camera* camera, rw::uint32 flags) {
    recordWords(DrawRecord::CameraShow, {flags});
    if (forwarding()) {
        camera->showRaster(flags);
    }
}

rw::Raster* createRaster(rw::int32 width, rw::int32 height, rw::int32 depth, rw::int32 format) {
    rw::Raster* raster =
        rw::Raster::create(width, height, depth, forwarding() ? format : format | rw::Raster::DONTALLOCATE);
    noteCreated(raster);
    return raster;
}

rw::uint8* lockRaster(rw::Raster* raster, rw::int32 level, rw::int32 lockMode) {
    if (forwarding()) {
        return raster->lock(level, lockMode);
    }
    // The level's size, as librw's own lock works it out; at least a byte a texel.
    const rw::int32 width = std::max(raster->width >> level, 1);
    const rw::int32 height = std::max(raster->height >> level, 1);
    const auto bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                       static_cast<std::size_t>(std::max(raster->depth, 8) / 8);
    std::vector<rw::uint8>& buffer = layer().lockBuffers[raster];
    buffer.assign(bytes, 0);
    return buffer.data();
}

void unlockRaster(rw::Raster* raster, rw::int32 level) {
    if (forwarding()) {
        // librw's lock leaves the raster at the level's size with its texels in `pixels`.
        if (raster->pixels != nullptr) {
            const auto bytes = static_cast<std::size_t>(raster->stride) * static_cast<std::size_t>(raster->height);
            noteUpload(raster, level, std::span<const std::uint8_t>(raster->pixels, bytes), raster->width,
                       raster->height);
        }
        raster->unlock(level);
        return;
    }
    Layer& current = layer();
    const auto buffer = current.lockBuffers.find(raster);
    if (buffer == current.lockBuffers.end()) {
        return;
    }
    noteUpload(raster, level, buffer->second, std::max(raster->width >> level, 1),
               std::max(raster->height >> level, 1));
    current.lockBuffers.erase(buffer);
}

void setRasterFromImage(rw::Raster* raster, rw::Image* image) {
    if (image != nullptr && image->pixels != nullptr) {
        const auto bytes = static_cast<std::size_t>(image->stride) * static_cast<std::size_t>(image->height);
        noteUpload(raster, 0, std::span<const std::uint8_t>(image->pixels, bytes), image->width, image->height);
    }
    if (forwarding()) {
        raster->setFromImage(image);
    }
}

rw::Raster* convertRasterForDrawing(rw::Raster* source) {
    // The source's texels, read before the conversion destroys it.
    std::optional<std::vector<std::uint8_t>> texels;
    if (layer().trackRasters && source != nullptr && source->platform == rw::PLATFORM_PS2) {
        texels = ps2Texels(*source);
    }
    const rw::int32 width = source != nullptr ? source->width : 0;
    const rw::int32 height = source != nullptr ? source->height : 0;
    rw::Raster* converted = forwarding() ? rw::Raster::convertTexToCurrentPlatform(source) : source;
    if (converted != nullptr && converted != source) {
        noteCreated(converted);
    }
    if (converted != nullptr && texels) {
        noteUpload(converted, 0, *texels, width, height);
    }
    return converted;
}

void copyFrameToRaster(rw::Raster* raster, int x, int y, int width, int height, int frameHeight) {
    if (DrawStream* out = stream(); out != nullptr) {
        out->begin(DrawRecord::FrameCopy);
        writeRaster(*out, raster);
        for (const int value : {x, y, width, height, frameHeight}) {
            out->word(static_cast<std::uint32_t>(value));
        }
    }
    if (!forwarding()) {
        return;
    }
    // GL counts the frame buffer's rows from the bottom.
    auto* native = PLUGINOFFSET(rw::gl3::Gl3Raster, raster, rw::gl3::nativeRasterOffset);
    rw::gl3::bindTexture(native->texid);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, frameHeight - (y + height), width, height);
}

bool readFrame(int x, int y, int width, int height, std::uint8_t* rgba) {
    recordWords(DrawRecord::FrameRead, {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                        static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)});
    if (!forwarding()) {
        return false;
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return glGetError() == GL_NO_ERROR;
}

void setWireframe(bool on) {
    recordWords(DrawRecord::Wireframe, {on ? 1U : 0U});
    if (!forwarding()) {
        return;
    }
    if (on) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glEnable(GL_POLYGON_OFFSET_LINE);
        glPolygonOffset(-1.0F, -1.0F);
    } else {
        glDisable(GL_POLYGON_OFFSET_LINE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

void bindWindowTarget() {
    recordWords(DrawRecord::WindowTarget, {});
    if (forwarding()) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void setRasterLevels(rw::Raster* raster, rw::int32 levels) {
    if (DrawStream* out = stream(); out != nullptr) {
        out->begin(DrawRecord::RasterLevels);
        writeRaster(*out, raster);
        out->word(static_cast<std::uint32_t>(levels));
    }
    if (!forwarding()) {
        return;
    }
    auto* native = PLUGINOFFSET(rw::gl3::Gl3Raster, raster, rw::gl3::nativeRasterOffset);
    const rw::uint32 bound = rw::gl3::bindTexture(native->texid);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    rw::gl3::bindTexture(bound); // librw's state cache still holds the texture bound before
}

} // namespace coney::platform::draw
