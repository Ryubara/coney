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

#include "platform/draw_stream.h"

namespace coney::platform::draw {

namespace {

// The render states librw has, TEXTURERASTER (0) to GSALPHATESTREF.
constexpr std::size_t kStateCount = static_cast<std::size_t>(rw::GSALPHATESTREF) + 1;

// What is known of a raster Coney made: the digest of its pixels, from its uploads while rasters were tracked.
struct RasterFacts {
    std::uint64_t digest = 0;
    bool digested = false;
};

// The layer's state, global as librw's own is.
struct Layer {
    bool forwarding = true;
    bool trackRasters = false;
    std::array<rw::uint32, kStateCount> states{};
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
DrawStream* stream() { return layer().stream ? &*layer().stream : nullptr; }

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
    for (int i = 0; i < 8; ++i) {
        chained[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(facts.digest >> (8 * i));
        chained[static_cast<std::size_t>(12 + i)] = static_cast<std::uint8_t>(pixels >> (8 * i));
    }
    for (int i = 0; i < 4; ++i) {
        chained[static_cast<std::size_t>(8 + i)] =
            static_cast<std::uint8_t>(static_cast<std::uint32_t>(level) >> (8 * i));
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
    if (forwarding()) {
        camera->beginUpdate();
    }
}

void endCameraUpdate(rw::Camera* camera) {
    recordWords(DrawRecord::CameraEnd, {});
    if (forwarding()) {
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
