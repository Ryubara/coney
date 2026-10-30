// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic scenes drawn headless through the draw layer (docs/engine/renderer.md#comparing): Coney-made quads,
// triangles, a textured mesh, the throw arc's ribbon and the named OpenGL operations, recorded on the NULL renderer
// with no OpenGL context, and their streams' hashes. No game data: every vertex, texel and colour is made here.
//
// The expected hashes are Coney's own: they were recorded from these scenes when the layer was written, and a change
// to one means the drawing of these scenes changed. Such a change fails CI until the pull request explains it and
// updates the hash (docs/engine/renderer.md#comparing). The original has no counterpart.
#include "platform/draw_calls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "animation/anim_math.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "graphics/render_device.h"
#include "graphics/throw_arc.h"
#include "platform/draw_stream.h"
#include "platform/render_engine.h"
#include "platform/throw_arc_renderer.h"
#include "support/fixtures.h"
#include "support/optional_value.h"

namespace draw = coney::platform::draw;
using coney::graphics::LogicalQuad;
using coney::graphics::LogicalVertex;
using coney::graphics::Rgba;
using coney::graphics::TriangleStates;
using coney::graphics::UvRect;
using coney::platform::DrawStream;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;

namespace {

// The headless renderer every scene draws on, at the original's frame size.
std::unique_ptr<RenderEngine> startHeadless() {
    coney::platform::WindowDesc desc;
    desc.width = 640;
    desc.height = 448;
    auto engine = RenderEngine::start(RenderBackend::Null, desc);
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// The 2D scene: a frame cleared to a colour (its fill quad), flat quads, alpha-tested triangles, a quad stretched
// over the whole view and a window rectangle, as the front end and the HUD draw them.
DrawStream drawTwoDimensionalScene(RenderEngine& engine) {
    engine.requestDrawStream(0);
    engine.beginFrame(Rgba{32, 48, 64, 255});
    const std::array<LogicalQuad, 3> quads{{
        {10.0F, 20.0F, 100.0F, 40.0F, UvRect{}, Rgba{255, 0, 0, 255}},
        {320.0F, 224.0F, 64.5F, 32.25F, UvRect{0.25F, 0.0F, 0.75F, 1.0F}, Rgba{0, 255, 0, 128}},
        {600.0F, 400.0F, 40.0F, 48.0F, UvRect{}, Rgba{0, 0, 255, 64}},
    }};
    engine.drawQuads(nullptr, quads);
    const std::array<LogicalVertex, 6> triangles{{
        {100.0F, 100.0F, 0.0F, 0.0F, Rgba{255, 255, 255, 255}},
        {200.0F, 100.0F, 1.0F, 0.0F, Rgba{255, 255, 0, 200}},
        {150.0F, 180.0F, 0.5F, 1.0F, Rgba{255, 0, 255, 100}},
        {300.0F, 300.0F, 0.0F, 0.0F, Rgba{10, 20, 30, 40}},
        {340.0F, 300.0F, 1.0F, 0.0F, Rgba{50, 60, 70, 80}},
        {320.0F, 340.0F, 0.5F, 1.0F, Rgba{90, 100, 110, 120}},
    }};
    engine.drawTriangles(nullptr, triangles, TriangleStates{false, 0.5F});
    const LogicalQuad wash{0.0F, 0.0F, 640.0F, 448.0F, UvRect{}, Rgba{0, 0, 0, 96}};
    engine.drawViewQuads(nullptr, std::span<const LogicalQuad>(&wash, 1));
    const LogicalQuad rect{4.0F, 4.0F, 12.0F, 12.0F, UvRect{}, Rgba{200, 200, 200, 255}};
    engine.drawWindowRects(std::span<const LogicalQuad>(&rect, 1));
    engine.present();
    const std::optional<DrawStream>& recorded = engine.drawStream();
    REQUIRE(recorded.has_value());
    return coney::test::got(recorded);
}

// The bytes of a 4 x 4 RGBA texture.
constexpr std::size_t kCheckerBytes = 64;

// A 4 x 4 RGBA checkerboard of two colours, Coney's own texture.
std::array<std::uint8_t, kCheckerBytes> checkerboard(std::uint8_t dark, std::uint8_t light) {
    std::array<std::uint8_t, kCheckerBytes> texels{};
    for (std::size_t y = 0; y < 4; ++y) {
        for (std::size_t x = 0; x < 4; ++x) {
            const std::uint8_t value = ((x + y) % 2 == 0) ? dark : light;
            const std::size_t at = (y * 4 + x) * 4;
            texels[at] = value;
            texels[at + 1] = value;
            texels[at + 2] = value;
            texels[at + 3] = 255;
        }
    }
    return texels;
}

// What the textured mesh scene may vary, for the checks that a change of each moves the hash.
struct MeshScene {
    std::uint8_t lightTexel = 230;
    float nudge = 0.0F;        // added to the first vertex's x
    bool repeatStates = false; // every state set twice, the second time to the same value
};

// The textured mesh scene: a camera placed and cleared, a 2 x 2 grid of quads in Im3D with vertex colours and normals
// under a world matrix, textured with a checkerboard uploaded through a lock, then the camera ended and shown.
DrawStream drawMeshScene(const MeshScene& scene) {
    draw::setTrackRasters(true); // the checkerboard known by its texels
    draw::startRecording();
    rw::Raster* raster = draw::createRaster(4, 4, 32, static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888);
    REQUIRE(raster != nullptr);
    rw::uint8* pixels = draw::lockRaster(raster, 0, rw::Raster::LOCKWRITE | rw::Raster::LOCKNOFETCH);
    REQUIRE(pixels != nullptr);
    const auto texels = checkerboard(20, scene.lightTexel);
    std::copy(texels.begin(), texels.end(), pixels);
    draw::unlockRaster(raster, 0);

    rw::Camera* camera = rw::Camera::create();
    rw::Frame* frame = rw::Frame::create();
    camera->setFrame(frame);
    const rw::V3d eye{0.0F, 2.0F, -6.0F};
    frame->translate(&eye);
    camera->setNearPlane(0.1F);
    camera->setFarPlane(100.0F);
    rw::RGBA sky = rw::makeRGBA(90, 120, 200, 255);
    draw::clearCamera(camera, &sky, rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
    draw::beginCameraUpdate(camera);

    // The states the world pass draws with, set once, or twice where the scene asks (the hash must not see it).
    const int times = scene.repeatStates ? 2 : 1;
    for (int i = 0; i < times; ++i) {
        draw::setRenderState(rw::ZTESTENABLE, 1);
        draw::setRenderState(rw::ZWRITEENABLE, 1);
        draw::setRenderState(rw::CULLMODE, rw::CULLBACK);
        draw::setRenderState(rw::FOGENABLE, 1);
        draw::setRenderState(rw::FOGCOLOR, 0xFFC87850U);
        draw::setRenderState(rw::VERTEXALPHA, 1);
        draw::setRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        draw::setRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        draw::setRenderState(rw::TEXTUREFILTER, rw::Texture::LINEAR);
        draw::setRenderState(rw::TEXTUREADDRESS, rw::Texture::WRAP);
        draw::setRenderStatePtr(rw::TEXTURERASTER, raster);
    }

    // Nine vertices on a 2 x 2 grid, one metre apart, colours shading across it and every normal up.
    std::vector<rw::gl3::Im3DVertex> vertices;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            rw::gl3::Im3DVertex vertex{};
            vertex.setX(static_cast<float>(column) - 1.0F);
            vertex.setY(0.0F);
            vertex.setZ(static_cast<float>(row) - 1.0F);
            vertex.setNormalY(1.0F);
            vertex.setColor(static_cast<rw::uint8>(80 * column), static_cast<rw::uint8>(80 * row), 200, 255);
            vertex.setU(static_cast<float>(column) * 0.5F);
            vertex.setV(static_cast<float>(row) * 0.5F);
            vertices.push_back(vertex);
        }
    }
    vertices.front().setX(vertices.front().getX() + scene.nudge);
    std::vector<std::uint16_t> indices;
    for (std::uint16_t row = 0; row < 2; ++row) {
        for (std::uint16_t column = 0; column < 2; ++column) {
            const auto corner = static_cast<std::uint16_t>(row * 3 + column);
            for (const std::uint16_t index :
                 {corner, static_cast<std::uint16_t>(corner + 3), static_cast<std::uint16_t>(corner + 1),
                  static_cast<std::uint16_t>(corner + 1), static_cast<std::uint16_t>(corner + 3),
                  static_cast<std::uint16_t>(corner + 4)}) {
                indices.push_back(index);
            }
        }
    }
    rw::Matrix world;
    world.setIdentity();
    const rw::V3d at{0.5F, 0.0F, 3.0F};
    world.translate(&at, rw::COMBINEREPLACE);
    draw::im3dTransform(vertices.data(), static_cast<rw::int32>(vertices.size()), &world,
                        rw::im3d::VERTEXXYZ | rw::im3d::VERTEXRGBA | rw::im3d::VERTEXUV);
    draw::im3dRenderIndexedPrimitive(rw::PRIMTYPETRILIST, indices.data(), static_cast<rw::int32>(indices.size()));
    draw::im3dEnd();
    draw::endCameraUpdate(camera);
    draw::showCameraRaster(camera, 0);
    std::optional<DrawStream> stream = draw::stopRecording();

    camera->setFrame(nullptr);
    frame->destroy();
    camera->destroy();
    raster->destroy();
    REQUIRE(stream.has_value());
    return coney::test::got(stream);
}

// A WAD of one file that is not the arc's texture: the arc then draws untextured, as ThrowArcRenderer says.
struct EmptyWad {
    coney::test::TempDir folder;
    std::optional<coney::io::Wad> wad;
    EmptyWad() {
        coney::test::Bytes file;
        file.text("not a texture");
        file.padTo(2048);
        coney::test::Bytes dir;
        dir.header(1, 0, 0, 0);
        dir.u32(0).u32(static_cast<std::uint32_t>(file.size())).u32(coney::nameHash("./ee_files/none.txt"));
        folder.write("WARRIORS.DIR", dir.span());
        folder.write("WARRIORS.WAD", file.span());
        auto disc = coney::io::Disc::open(folder.path());
        REQUIRE(disc.has_value());
        auto opened = coney::io::Wad::open(std::move(*disc));
        REQUIRE(opened.has_value());
        wad.emplace(std::move(*opened));
    }
};

// The throw arc's ribbon over a Coney-made arc of points, drawn by Coney's own ThrowArcRenderer.
DrawStream drawThrowArcScene(const coney::io::Wad& wad) {
    std::vector<coney::anim::Vec3> points;
    for (int i = 0; i < 10; ++i) {
        const auto t = static_cast<float>(i) * 0.25F;
        points.push_back(coney::anim::Vec3{t, 1.5F * t, 1.0F + t - (0.3F * t * t)});
    }
    const std::vector<coney::graphics::ThrowArcVertex> strip =
        coney::graphics::throwArcStrip(points, coney::anim::Vec3{1.0F, 0.0F, 0.0F}, false, false);
    coney::platform::ThrowArcRenderer renderer(wad, {});
    draw::startRecording();
    renderer.draw(strip);
    std::optional<DrawStream> stream = draw::stopRecording();
    REQUIRE(stream.has_value());
    return coney::test::got(stream);
}

} // namespace

TEST_CASE("the 2D scene's draw stream has its expected hash", "[platform][draw-stream]") {
    // Expected: recorded from this scene when the layer was written (docs/engine/renderer.md#comparing).
    auto engine = startHeadless();
    const DrawStream stream = drawTwoDimensionalScene(*engine);
    // The fill, the three quads in one draw, the triangles, the wash and the window rectangle.
    CHECK(stream.counts().im2dDraws == 5);
    CHECK(stream.counts().vertices == 4 + 12 + 6 + 4 + 4);
    CHECK(stream.hash() == 0xd663d06f6ca2e312ULL);
}

TEST_CASE("the textured mesh scene's draw stream has its expected hash", "[platform][draw-stream]") {
    // Expected: recorded from this scene when the layer was written (docs/engine/renderer.md#comparing).
    auto engine = startHeadless();
    const DrawStream stream = drawMeshScene(MeshScene{});
    CHECK(stream.counts().im3dDraws == 1);
    CHECK(stream.counts().vertices == 9);
    CHECK(stream.counts().cameraOps == 4);
    CHECK(stream.counts().rasterOps == 2); // made, then its pixels uploaded
    CHECK(stream.hash() == 0xef87362eabc42d62ULL);
}

TEST_CASE("the throw arc scene's draw stream has its expected hash", "[platform][draw-stream]") {
    // Expected: recorded from this scene when the layer was written (docs/engine/renderer.md#comparing).
    auto engine = startHeadless();
    const EmptyWad archive;
    const DrawStream stream = drawThrowArcScene(*archive.wad);
    CHECK(stream.counts().im3dDraws == 1);
    CHECK(stream.counts().vertices == coney::graphics::kThrowArcPairs * 2);
    CHECK(stream.hash() == 0x288a1770ecad4060ULL);
}

TEST_CASE("a scene drawn twice on fresh renderers hashes the same", "[platform][draw-stream]") {
    // Expected: docs/engine/renderer.md#comparing: the layer starts afresh with each renderer (draw::reset), so a
    // recording depends on the scene alone.
    std::uint64_t first = 0;
    {
        auto engine = startHeadless();
        first = drawTwoDimensionalScene(*engine).hash();
    }
    auto engine = startHeadless();
    CHECK(drawTwoDimensionalScene(*engine).hash() == first);
    CHECK(drawMeshScene(MeshScene{}).hash() == drawMeshScene(MeshScene{}).hash());
}

TEST_CASE("the draw stream sees a texel, a grid step and a state but not a repeated state", "[platform][draw-stream]") {
    // Expected: what docs/engine/renderer.md#comparing says the hash covers: the states in force at each draw, the
    // vertices on the 1/1024 grid and the pixels Coney uploads.
    auto engine = startHeadless();
    const std::uint64_t base = drawMeshScene(MeshScene{}).hash();
    MeshScene repeated;
    repeated.repeatStates = true;
    CHECK(drawMeshScene(repeated).hash() == base);
    MeshScene withinStep;
    withinStep.nudge = 0.0001F;
    CHECK(drawMeshScene(withinStep).hash() == base);
    MeshScene stepAway;
    stepAway.nudge = 1.0F / 1024.0F;
    CHECK(drawMeshScene(stepAway).hash() != base);
    MeshScene texel;
    texel.lightTexel = 231;
    CHECK(drawMeshScene(texel).hash() != base);
}

TEST_CASE("headless the draw layer records without reaching librw or OpenGL", "[platform][draw-stream]") {
    // Expected: docs/engine/renderer.md#comparing: under the NULL renderer nothing is forwarded (there is no OpenGL
    // context); the named operations are recorded, a read-back reports that nothing was read, and states read back
    // are the layer's own.
    auto engine = startHeadless();
    CHECK_FALSE(draw::forwarding());
    CHECK_FALSE(draw::recording());
    CHECK_FALSE(draw::stopRecording().has_value());
    draw::startRecording();
    CHECK(draw::recording());
    rw::Raster* raster =
        draw::createRaster(64, 32, 32, static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888);
    REQUIRE(raster != nullptr);
    CHECK(raster->width == 64);
    draw::copyFrameToRaster(raster, 0, 0, 64, 32, 448);
    std::vector<std::uint8_t> rgba(8192); // 64 x 32 pixels of 4 bytes
    CHECK_FALSE(draw::readFrame(0, 0, 64, 32, rgba.data()));
    draw::setWireframe(true);
    draw::setWireframe(false);
    draw::bindWindowTarget();
    draw::setRasterLevels(raster, 1);
    draw::setRenderState(rw::ALPHATESTREF, 77);
    CHECK(draw::getRenderState(rw::ALPHATESTREF) == 77);
    draw::setRenderState(rw::TEXTUREADDRESS, rw::Texture::CLAMP);
    CHECK(draw::getRenderState(rw::TEXTUREADDRESSV) == rw::Texture::CLAMP);
    const std::optional<DrawStream> recorded = draw::stopRecording();
    raster->destroy();
    REQUIRE(recorded.has_value());
    const DrawStream stream = coney::test::got(recorded);
    CHECK(stream.counts().frameOps == 5);  // the copy, the read-back, two wireframe switches, the window target
    CHECK(stream.counts().rasterOps == 2); // made, its levels set
    CHECK(stream.hash() == 0xe7a0be90f5dddce0ULL);
}

TEST_CASE("a PS2 raster is known by its texels and palette, not by where librw keeps them", "[platform][draw-stream]") {
    // Expected: docs/engine/renderer.md#comparing: a raster's digest is of the pixels Coney uploads, so two runs of one
    // frame hash the same. A new-style PS2 raster's data block starts with a pointer to its own texels (ps2raster.cpp,
    // rasterCreate), which differs between two rasters and between runs; the digest must not see it.
    auto engine = startHeadless();
    draw::setTrackRasters(true);
    draw::startRecording();
    // Both alive at once, so librw keeps them at two addresses.
    std::array<rw::Raster*, 2> rasters{};
    for (rw::Raster*& raster : rasters) {
        raster = rw::Raster::create(16, 16, 8,
                                    static_cast<rw::int32>(rw::Raster::TEXTURE) | rw::Raster::C8888 | rw::Raster::PAL8,
                                    rw::PLATFORM_PS2);
        REQUIRE(raster != nullptr);
        const rw::ps2::Ps2Raster* native = GETPS2RASTEREXT(raster);
        REQUIRE((native->flags & rw::ps2::Ps2Raster::NEWSTYLE) != 0);
        // The same Coney-made texels and palette in both, where librw reads a texture's from the disc.
        auto* blocks = reinterpret_cast<rw::ps2::Ps2Raster::PixelPtr*>(native->data);
        std::fill(blocks->pixels, blocks->pixels + native->pixelSize, rw::uint8{0x5A});
        REQUIRE(raster->palette != nullptr);
        std::fill(raster->palette - 0x50, raster->palette - 0x50 + native->paletteSize, rw::uint8{0x33});
    }
    for (rw::Raster* raster : rasters) {
        CHECK(draw::convertRasterForDrawing(raster) == raster); // headless, nothing is converted
    }
    for (rw::Raster* raster : rasters) {
        raster->destroy();
    }
    const std::optional<DrawStream> recorded = draw::stopRecording();
    REQUIRE(recorded.has_value());
    const DrawStream stream = coney::test::got(recorded);
    REQUIRE(stream.counts().rasterOps == 2); // one upload each
    const std::vector<std::uint64_t> records = stream.recordHashes();
    REQUIRE(records.size() == 2);
    CHECK(records[0] == records[1]);
}
