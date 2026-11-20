// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's start (docs/engine/renderer.md#migration, item 3): a window Coney makes through SDL3, SDL's GPU
// device and swapchain claimed for it, and librw opened with its NULL device for data only, as headless runs open it.
// The first case runs anywhere (librw's NULL device needs no window or GPU); the others need SDL's video, and the last
// a GPU device, and skip where there is none.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "graphics/render_device.h"
#include "graphics/texture_data.h"
#include "platform/pixel_texture.h"
#include "platform/present_mode.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "support/fixtures.h"
#include "support/optional_value.h"
#include "support/rw_fixtures.h"

using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WindowDesc;

namespace {

// Sets an SDL hint for one test and puts SDL's own value (its default, or the environment's) back after.
class HintScope {
  public:
    HintScope(const char* name, const char* value) : m_name(name) { SDL_SetHint(name, value); }
    HintScope(const HintScope&) = delete;
    HintScope& operator=(const HintScope&) = delete;
    HintScope(HintScope&&) = delete;
    HintScope& operator=(HintScope&&) = delete;
    ~HintScope() { SDL_ResetHint(m_name); }

  private:
    const char* m_name;
};

// Whether SDL's video starts here and sees a display, and, when `gpu` is set, whether SDL's GPU API has a driver here
// that takes one of `formats` (asked while the video runs, which the Vulkan driver's check needs). Leaves the video
// stopped again.
bool videoWithADisplay(bool gpu = false, SDL_GPUShaderFormat formats = 0) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return false;
    }
    int displays = 0;
    SDL_DisplayID* list = SDL_GetDisplays(&displays);
    SDL_free(list);
    const bool ready = displays > 0 && (!gpu || SDL_GPUSupportsShaderFormats(formats, nullptr));
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return ready;
}

// Reads the clump section `bytes` with librw, as a RenderWare stream; null when librw refuses it.
rw::Clump* readClump(const coney::test::Bytes& bytes) {
    std::vector<rw::uint8> copy(bytes.span().size());
    std::memcpy(copy.data(), bytes.span().data(), copy.size());
    rw::StreamMemory memory;
    memory.open(copy.data(), static_cast<rw::uint32>(copy.size()), static_cast<rw::uint32>(copy.size()));
    rw::Clump* clump = nullptr;
    if (rw::findChunk(&memory, rw::ID_CLUMP, nullptr, nullptr)) {
        clump = rw::Clump::streamRead(&memory);
    }
    memory.close();
    return clump;
}

} // namespace

TEST_CASE("librw opened for data only reads a synthetic clump and decodes a synthetic PS2 raster",
          "[render][renderer]") {
    // Expected: ../engine/gpu-renderer.md#librw, Coney's own start; the clump's and the raster's values are the
    // fixtures' own (rw_fixtures.h). The NULL device the GPU renderer opens librw with (render_engine.h), here
    // without the GPU's window, as on CI.
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    CHECK((*engine)->librwDataOnly());
    CHECK_FALSE((*engine)->drawsPixels());

    // A clump: its frame, its triangle's vertices and its material read as streamed (rw_fixtures.h).
    rw::Clump* clump = readClump(coney::test::rwTriangleClump({200, 100, 50, 255}));
    REQUIRE(clump != nullptr);
    CHECK(clump->countAtomics() == 1);
    rw::Atomic* atomic = nullptr;
    FORLIST(link, clump->atomics) { atomic = rw::Atomic::fromClump(link); }
    REQUIRE(atomic != nullptr);
    REQUIRE(atomic->geometry != nullptr);
    const rw::Geometry& geometry = *atomic->geometry;
    CHECK(geometry.numVertices == 3);
    CHECK(geometry.numTriangles == 1);
    CHECK(geometry.triangles[0].v[0] == 0);
    CHECK(geometry.triangles[0].v[1] == 1);
    CHECK(geometry.triangles[0].v[2] == 2);
    CHECK(geometry.morphTargets[0].vertices[1].x == 1.0F);
    CHECK(geometry.morphTargets[0].vertices[2].y == 1.0F);
    REQUIRE(geometry.matList.numMaterials == 1);
    const rw::RGBA colour = geometry.matList.materials[0]->color;
    CHECK(colour.red == 200);
    CHECK(colour.green == 100);
    CHECK(colour.blue == 50);
    CHECK(colour.alpha == 255);
    CHECK(atomic->getFrame() == clump->getFrame());
    clump->destroy();

    // A PS2 raster, decoded to RGBA: red, green / blue, white, its PS2 alpha 0x80 read as 255 and 0x40 as 127
    // (texture_dictionary_test.cpp has the same texel values).
    auto dictionary =
        coney::platform::TextureDictionary::read(coney::test::texDictionary({coney::test::rgbaTexture2x2("t")}).span());
    REQUIRE(dictionary.has_value());
    auto images = dictionary->toImages();
    REQUIRE(images.has_value());
    REQUIRE(images->size() == 1);
    const std::vector<std::uint8_t> expected{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 127};
    CHECK((*images)[0].pixels == expected);
}

TEST_CASE("a GPU start where no GPU device opens fails with a clear message and leaves SDL's video stopped",
          "[render][renderer][window]") {
    // Expected: ../engine/gpu-renderer.md#fallback, Coney's own message.
    // SDL's offscreen video driver needs no display, and a GPU driver of no such name opens no device anywhere.
    const HintScope video(SDL_HINT_VIDEO_DRIVER, "offscreen");
    const HintScope gpu(SDL_HINT_GPU_DRIVER, "no-such-driver");
    if (!videoWithADisplay()) {
        SKIP("SDL's offscreen video driver does not start here: " << SDL_GetError());
    }
    WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    REQUIRE_FALSE(started.has_value());
    CHECK(started.error().code == coney::ErrorCode::PlatformFailure);
    // What the run's warning says before it falls back to GL3 (game_run.h, startRenderer()).
    CHECK(started.error().message.find("no GPU device") != std::string::npos);
    // Everything it had started is stopped: SDL's video, so GL3 can start afresh, and librw, so a NULL start works.
    CHECK(SDL_WasInit(SDL_INIT_VIDEO) == 0);
    auto headless = RenderEngine::start(RenderBackend::Null, {});
    CHECK(headless.has_value());
}

TEST_CASE("the GPU backend draws the scene, so a level loads its objects and cars for it", "[render][renderer]") {
    // Expected: ../engine/gpu-renderer.md#scene. The GPU renderer draws the world, objects and cars from the frame
    // description, so play loads them on it as on GL3 (their textures held as read, not converted for librw); the
    // NULL backend draws no scene and loads none.
    CHECK(drawsSceneOn(RenderBackend::OpenGl));
    CHECK(drawsSceneOn(RenderBackend::Gpu));
    CHECK_FALSE(drawsSceneOn(RenderBackend::Null));
    auto engine = RenderEngine::start(RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    CHECK_FALSE((*engine)->drawsScene());
}

TEST_CASE("the GPU renderer claims its window and presents cleared frames where a GPU device opens",
          "[render][renderer][window]") {
    // Expected: ../engine/gpu-renderer.md#device and #present-mode, Coney's own start.
    // The shader formats SDL_shadercross makes (renderer.md#migration): a device that takes one of them.
    if (!videoWithADisplay(true, SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL)) {
        SKIP("no display or no GPU device here");
    }
    WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = 320;
    desc.height = 240;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    REQUIRE(started.has_value());
    RenderEngine& engine = **started;
    CHECK(engine.backend() == RenderBackend::Gpu);
    CHECK(engine.librwDataOnly());
    CHECK_FALSE(engine.drawsPixels());
    CHECK(engine.drawsScene());
    CHECK(engine.window().has_value());
    CHECK(engine.camera() == nullptr);
    CHECK_FALSE(engine.gpuDriver().empty());
    CHECK(engine.frameSize().width == 320);
    // Vsync on (the default): VSYNC, which every window offers.
    CHECK(engine.presentMode() == coney::platform::PresentMode::Vsync);
    for (int frame = 0; frame < 3; ++frame) {
        engine.beginFrame(coney::graphics::Rgba{20, 40, 60, 255});
        engine.present();
    }
    // Vsync off and no cap: MAILBOX where the window offers it, else IMMEDIATE or VSYNC, set at the next present.
    engine.setVsync(false);
    engine.setFpsCap(0);
    engine.beginFrame(coney::graphics::kBlack);
    engine.present();
    CHECK(engine.presentMode() == coney::platform::choosePresentMode(false, 0, engine.offeredPresentModes()));
    // Every present() ended its frame; a hidden window may hand out no swapchain image (SDL then skips the frame), so
    // of those only the bound is checked here, and CI's smoke run checks that a shown window's frames reach the screen.
    CHECK(engine.framesPresented() == 4);
    CHECK(engine.framesShown() <= engine.framesPresented());
}

namespace {

// The colour of pixel (`x`, `y`) of an RGBA image `width` wide, rows top first.
coney::graphics::Rgba pixelAt(const std::vector<std::uint8_t>& rgba, int width, int x, int y) {
    const std::size_t at = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + x) * 4;
    return coney::graphics::Rgba{rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

// Whether `got` is `want` give or take `slack` in each colour channel (a GPU's blending rounds its own way).
bool near(coney::graphics::Rgba got, coney::graphics::Rgba want, int slack) {
    const auto close = [slack](int a, int b) { return a - b <= slack && b - a <= slack; };
    return close(got.r, want.r) && close(got.g, want.g) && close(got.b, want.b);
}

// Reads back the PNG a capture wrote, as RGBA rows top first; empty when librw cannot read it.
std::vector<std::uint8_t> readCapture(const std::string& path, int& width, int& height) {
    rw::Image* image = rw::readPNG(path.c_str());
    if (image == nullptr) {
        return {};
    }
    image->convertTo32();
    width = image->width;
    height = image->height;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        std::memcpy(&rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4],
                    image->pixels + (static_cast<std::ptrdiff_t>(y) * image->stride),
                    static_cast<std::size_t>(width) * 4);
    }
    image->destroy();
    return rgba;
}

} // namespace

TEST_CASE("the GPU renderer draws a frame's 2D passes and reads the frame back", "[render][renderer][window]") {
    // Expected: ../engine/gpu-renderer.md#drawing and #capture, Coney's own renderer, drawing what the GL3 renderer
    // draws: the window black round the logical screen and the logical screen in the frame's colour (beginFrame()),
    // flat and textured quads in the 2D pass's blending, the alpha test as GL3 has it (quads at least 10 of 255, a
    // triangle's own reference: overlay_batch_test.cpp). The window's sizes are the test's own: a 400 x 224 window
    // holds the 640 x 448 logical screen at half size from x 40 (graphics::fitLogicalScreen(), screen_test.cpp).
    if (!videoWithADisplay(true, SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL)) {
        SKIP("no display or no GPU device here");
    }
    WindowDesc desc;
    desc.activate = false;
    desc.hidden = true;
    desc.width = 400;
    desc.height = 224;
    auto started = RenderEngine::start(RenderBackend::Gpu, desc);
    REQUIRE(started.has_value());
    RenderEngine& engine = **started;
    REQUIRE(engine.frameSize().width == 400);
    const std::string path = (std::filesystem::temp_directory_path() / "coney-gpu-2d-capture.png").string();
    engine.requestCapture(0, path);

    using coney::graphics::LogicalQuad;
    using coney::graphics::LogicalVertex;
    using coney::graphics::Rgba;
    using coney::graphics::UvRect;
    const Rgba fill{20, 40, 60, 255};
    engine.beginFrame(fill);
    // A red square at the logical screen's top left.
    const LogicalQuad red{0.0F, 0.0F, 64.0F, 64.0F, UvRect{}, Rgba{255, 0, 0, 255}};
    engine.drawQuads(nullptr, std::span<const LogicalQuad>(&red, 1));
    // A 2 x 2 texture enlarged with the nearest texel: red, green over blue, white.
    coney::platform::PixelTexture texture(coney::graphics::TextureFilter::Nearest);
    const std::vector<std::uint8_t> texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    REQUIRE(texture.write(texels, 2, 2));
    const LogicalQuad textured{320.0F, 0.0F, 128.0F, 128.0F, UvRect{}, coney::graphics::kWhite};
    engine.drawQuads(&texture, std::span<const LogicalQuad>(&textured, 1));
    // A white band too faint for the quads' alpha test: nothing drawn.
    const LogicalQuad faint{0.0F, 256.0F, 640.0F, 64.0F, UvRect{}, Rgba{255, 255, 255, 5}};
    engine.drawQuads(nullptr, std::span<const LogicalQuad>(&faint, 1));
    // White triangles at a quarter alpha: dropped by a test at a half, blended over the fill by a test at 0.
    const auto square = [](float left) {
        const Rgba white{255, 255, 255, 64};
        return std::vector<LogicalVertex>{
            {left, 320.0F, 0.0F, 0.0F, white},          {left + 128.0F, 320.0F, 0.0F, 0.0F, white},
            {left + 128.0F, 448.0F, 0.0F, 0.0F, white}, {left, 320.0F, 0.0F, 0.0F, white},
            {left + 128.0F, 448.0F, 0.0F, 0.0F, white}, {left, 448.0F, 0.0F, 0.0F, white}};
    };
    engine.drawTriangles(nullptr, square(0.0F), coney::graphics::TriangleStates{false, 0.5F});
    engine.drawTriangles(nullptr, square(512.0F), coney::graphics::TriangleStates{false, 0.0F});
    engine.present();

    const auto captured = coney::test::got(engine.capture());
    REQUIRE(captured.has_value());
    CHECK(captured->size.width == 400);
    CHECK(captured->size.height == 224);
    int width = 0;
    int height = 0;
    const std::vector<std::uint8_t> rgba = readCapture(path, width, height);
    REQUIRE(width == 400);
    REQUIRE(height == 224);
    CHECK(near(pixelAt(rgba, width, 10, 100), coney::graphics::kBlack, 0)); // the border
    CHECK(near(pixelAt(rgba, width, 120, 100), fill, 0));                   // the logical screen
    CHECK(near(pixelAt(rgba, width, 56, 16), Rgba{255, 0, 0, 255}, 0));     // the red square, window 40 to 72
    CHECK(near(pixelAt(rgba, width, 216, 16), Rgba{255, 0, 0, 255}, 0));    // the texture's quarters, window 200 to 264
    CHECK(near(pixelAt(rgba, width, 248, 16), Rgba{0, 255, 0, 255}, 0));
    CHECK(near(pixelAt(rgba, width, 216, 48), Rgba{0, 0, 255, 255}, 0));
    CHECK(near(pixelAt(rgba, width, 248, 48), Rgba{255, 255, 255, 255}, 0));
    CHECK(near(pixelAt(rgba, width, 150, 144), fill, 0)); // the faint band, dropped
    CHECK(near(pixelAt(rgba, width, 72, 192), fill, 0));  // tested at a half, dropped
    // Tested at 0: a quarter of white over the fill.
    CHECK(near(pixelAt(rgba, width, 328, 192), Rgba{79, 94, 109, 255}, 2));
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
