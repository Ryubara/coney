// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's batch of a frame description's 2D draws, built headless (docs/engine/gpu-renderer.md#drawing),
// and the cache of its uploaded textures (#textures). The expected values are Coney's own (the GPU renderer has no
// counterpart in the original) except the alpha test's, which are librw's GL3 device's, the picture the GPU renderer is
// compared with: its start state (resetRenderState: ALPHAGREATEREQUAL, 10 of 255) and DoAlphaTest's bounds (-1000 and
// 1000 for none, header.frag), and the GL3 renderer's own reference (round(ref * 255), gl3_renderer.cpp).
#include "graphics/overlay_batch.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "graphics/frame_description.h"
#include "graphics/render_device.h"
#include "graphics/texture_data.h"
#include "graphics/upload_cache.h"

using coney::graphics::AlphaRange;
using coney::graphics::batchOverlays;
using coney::graphics::BlurPulse;
using coney::graphics::FrameDescription;
using coney::graphics::FrameStart;
using coney::graphics::kNoFrameTexture;
using coney::graphics::LogicalQuad;
using coney::graphics::LogicalVertex;
using coney::graphics::Material2d;
using coney::graphics::Overlay2dVertex;
using coney::graphics::OverlayBatch;
using coney::graphics::OverlayDraw;
using coney::graphics::Rgba;
using coney::graphics::ScreenPass;
using coney::graphics::ScreenRect;
using coney::graphics::TextureAddress;
using coney::graphics::UploadCache;
using coney::graphics::UvRect;

namespace {

// A texture of a size, standing in for a sprite sheet's.
class FakeTexture final : public coney::graphics::Texture {
  public:
    [[nodiscard]] int width() const override { return 4; }
    [[nodiscard]] int height() const override { return 4; }
};

// The logical screen filling a 640 x 448 window: logical and window pixels are the same.
constexpr ScreenRect kWholeScreen{0, 0, 640, 448};

} // namespace

TEST_CASE("an overlay batch holds a quad as four corners and two triangles", "[graphics][gpu-renderer]") {
    // Expected: docs/engine/gpu-renderer.md#drawing; corners clockwise from the top left as
    // Gl3Renderer::drawWindowQuads makes them, indices 0, 1, 2 and 0, 2, 3.
    FrameDescription frame;
    frame.begin(FrameStart{});
    const LogicalQuad quad{10.0F, 20.0F, 30.0F, 40.0F, UvRect{0.25F, 0.5F, 0.75F, 1.0F}, Rgba{1, 2, 3, 4}};
    frame.addQuads(Material2d{}, std::span<const LogicalQuad>(&quad, 1), kWholeScreen);

    const OverlayBatch batch = batchOverlays(frame);
    const Rgba colour{1, 2, 3, 4};
    const std::vector<Overlay2dVertex> corners{{10.0F, 20.0F, 0.25F, 0.5F, colour},
                                               {40.0F, 20.0F, 0.75F, 0.5F, colour},
                                               {40.0F, 60.0F, 0.75F, 1.0F, colour},
                                               {10.0F, 60.0F, 0.25F, 1.0F, colour}};
    CHECK(batch.vertices == corners);
    CHECK(batch.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    REQUIRE(batch.draws.size() == 1);
    CHECK(batch.draws[0].firstIndex == 0);
    CHECK(batch.draws[0].indexCount == 6);
    CHECK(batch.draws[0].texture == kNoFrameTexture);
    CHECK(batch.draws[0].address == TextureAddress::Clamp);
    CHECK(batch.skippedPasses == 0);
}

TEST_CASE("an overlay batch keeps the draws' order, textures and addressing across passes",
          "[graphics][gpu-renderer]") {
    // Expected: docs/engine/gpu-renderer.md#drawing: the draws in the frame's order, the screen passes left out and
    // counted, each draw's indices after the last.
    FrameDescription frame;
    frame.begin(FrameStart{});
    const FakeTexture sheet;
    const std::array<LogicalQuad, 2> quads{
        {{0.0F, 0.0F, 8.0F, 8.0F, UvRect{}, Rgba{}}, {8.0F, 0.0F, 8.0F, 8.0F, UvRect{}, Rgba{}}}};
    frame.addQuads(Material2d{frame.texture(&sheet), TextureAddress::Wrap, {}}, quads, kWholeScreen);
    frame.addScreenPass(ScreenPass{kWholeScreen, BlurPulse{2, 0.01F, 0.01F}});
    const std::array<LogicalVertex, 4> corners{{{0.0F, 0.0F, 0.0F, 0.0F, Rgba{9, 9, 9, 255}},
                                                {1.0F, 0.0F, 1.0F, 0.0F, Rgba{9, 9, 9, 255}},
                                                {0.0F, 1.0F, 0.0F, 1.0F, Rgba{9, 9, 9, 255}},
                                                {5.0F, 5.0F, 0.0F, 0.0F, Rgba{9, 9, 9, 255}}}};
    frame.addTriangles(Material2d{kNoFrameTexture, TextureAddress::Clamp, 0.5F}, corners, kWholeScreen);

    const OverlayBatch batch = batchOverlays(frame);
    // Two quads (8 corners, 12 indices) then one triangle: the fourth corner is no whole triangle.
    CHECK(batch.vertices.size() == 11);
    REQUIRE(batch.indices.size() == 15);
    CHECK(batch.indices[6] == 4); // the second quad's first corner
    CHECK(batch.indices[12] == 8);
    CHECK(batch.indices[14] == 10);
    CHECK(batch.vertices[8] == Overlay2dVertex{0.0F, 0.0F, 0.0F, 0.0F, Rgba{9, 9, 9, 255}});
    REQUIRE(batch.draws.size() == 2);
    CHECK(batch.draws[0].texture == 1);
    CHECK(batch.draws[0].address == TextureAddress::Wrap);
    CHECK(batch.draws[0].firstIndex == 0);
    CHECK(batch.draws[0].indexCount == 12);
    CHECK(batch.draws[1].texture == kNoFrameTexture);
    CHECK(batch.draws[1].firstIndex == 12);
    CHECK(batch.draws[1].indexCount == 3);
    CHECK(batch.skippedPasses == 1);
}

TEST_CASE("an overlay draw keeps the alpha GL3 keeps", "[graphics][gpu-renderer]") {
    // Expected: librw's GL3 device (the file comment): the quads with its start state, at least 10 of 255; a reference
    // above 0 as at least round(ref * 255) / 255; 0 as every fragment, -1000 to 1000.
    CHECK(coney::graphics::alphaRangeOf(Material2d{}) == AlphaRange{10.0F / 255.0F, 1000.0F});
    CHECK(coney::graphics::alphaRangeOf(Material2d{kNoFrameTexture, TextureAddress::Clamp, 0.5F}) ==
          AlphaRange{128.0F / 255.0F, 1000.0F});
    CHECK(coney::graphics::alphaRangeOf(Material2d{kNoFrameTexture, TextureAddress::Clamp, 0.0F}) ==
          AlphaRange{-1000.0F, 1000.0F});
    // The batch carries it.
    FrameDescription frame;
    frame.begin(FrameStart{});
    const LogicalQuad quad{0.0F, 0.0F, 1.0F, 1.0F, UvRect{}, Rgba{}};
    frame.addQuads(Material2d{}, std::span<const LogicalQuad>(&quad, 1), kWholeScreen);
    const OverlayBatch batch = batchOverlays(frame);
    REQUIRE(batch.draws.size() == 1);
    CHECK(batch.draws[0].alpha == AlphaRange{10.0F / 255.0F, 1000.0F});
}

TEST_CASE("a frame clears the window to black unless it clears the whole window", "[graphics][gpu-renderer]") {
    // Expected: RenderEngine::beginFrame() (the window black, the logical screen filled by the first draw) and
    // beginWindowFrame() (the whole window in the colour), src/platform/render_engine.h.
    CHECK(coney::graphics::windowClearOf(FrameStart{Rgba{10, 20, 30, 255}, false}) == coney::graphics::kBlack);
    CHECK(coney::graphics::windowClearOf(FrameStart{Rgba{10, 20, 30, 255}, true}) == Rgba{10, 20, 30, 255});
}

TEST_CASE("every texture has a serial of its own", "[graphics][gpu-renderer]") {
    // Expected: Texture::serial()'s contract (src/graphics/render_device.h): from 1 up, never repeated, so a texture
    // made where another was freed is not taken for it.
    std::uint64_t first = 0;
    {
        const FakeTexture gone;
        first = gone.serial();
    }
    const FakeTexture next;
    const FakeTexture other;
    CHECK(first >= 1);
    CHECK(next.serial() > first);
    CHECK(other.serial() > next.serial());
}

TEST_CASE("the upload cache finds, replaces and drops textures by serial and use", "[graphics][gpu-renderer]") {
    // Expected: UploadCache's contract (src/graphics/upload_cache.h).
    UploadCache<std::unique_ptr<int>> cache;
    CHECK(cache.find(7, 1) == nullptr);
    cache.put(7, 3, 1, std::make_unique<int>(70));
    cache.put(8, 0, 1, std::make_unique<int>(80));
    CHECK(cache.size() == 2);

    // Found, with its revision, and marked used in the frame that found it.
    auto* seven = cache.find(7, 5);
    REQUIRE(seven != nullptr);
    CHECK(*seven->value == 70);
    CHECK(seven->revision == 3);
    CHECK(seven->lastUsed == 5);

    // A put replaces the entry.
    cache.put(7, 4, 6, std::make_unique<int>(71));
    seven = cache.find(7, 6);
    REQUIRE(seven != nullptr);
    CHECK(*seven->value == 71);
    CHECK(seven->revision == 4);
    CHECK(cache.size() == 2);

    // Serial 8, last used in frame 1, is dropped once more than 3 frames have passed without it; 7 stays.
    CHECK(cache.evict(4, 3) == 0);
    CHECK(cache.evict(5, 3) == 1);
    CHECK(cache.find(8, 5) == nullptr);
    CHECK(cache.find(7, 5) != nullptr);
    cache.clear();
    CHECK(cache.size() == 0);
}
