// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's scene passes (docs/engine/gpu-renderer.md#scene, item 13 of docs/engine/renderer.md#order): Coney-
// made meshes and a texture in the engine's scene assets, drawn through the frame description's camera on a hidden
// window and read back pixel by pixel. No game data: every vertex, texel and light is made here. The case needs SDL's
// video and a GPU device, and skips where there is none. The expected colours are librw's GL3 device's arithmetic, the
// picture the GPU renderer is compared with (scene_batch_test.cpp names each source).
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "graphics/frame_description.h"
#include "graphics/texture_data.h"
#include "platform/character_skin.h"
#include "platform/render_engine.h"
#include "platform/scene_assets.h"
#include "support/optional_value.h"

using coney::graphics::Rgba;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;
using coney::platform::WindowDesc;

namespace {

// Whether SDL's video starts here, sees a display and has a GPU driver that takes Coney's shader formats. Leaves the
// video stopped again.
bool gpuHere() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return false;
    }
    int displays = 0;
    SDL_DisplayID* list = SDL_GetDisplays(&displays);
    SDL_free(list);
    const bool ready =
        displays > 0 && SDL_GPUSupportsShaderFormats(
                            SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, nullptr);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return ready;
}

// What a test quad is: its corners, how its geometry is flagged and coloured, and its texture.
struct QuadSpec {
    std::array<float, 3> topLeft{};
    std::array<float, 3> topRight{};
    std::array<float, 3> bottomLeft{};
    std::array<float, 3> bottomRight{};
    bool lit = false;      // RenderWare's LIGHT, with normals facing the camera (-z)
    bool modulate = false; // MODULATE
    bool prelit = false;   // PRELIT, every vertex `prelight`
    Rgba prelight{255, 255, 255, 255};
    Rgba colour{255, 255, 255, 255}; // the material's colour
    rw::Texture* texture = nullptr;
    bool facingAway = false; // wound clockwise as the camera sees it, so back-face culling drops it
    float normalZ = -1.0F;   // the normals' z: -1 faces the camera, 1 away from it
};

// A librw atomic of one quad as `spec` says, at the world's origin: texture coordinates (0, 0) at its top left to
// (1, 1) at its bottom right, wound counter-clockwise as the camera sees it unless it faces away. The caller destroys
// it and its frame.
rw::Atomic* makeQuad(const QuadSpec& spec) {
    rw::uint32 flags = rw::Geometry::POSITIONS | rw::Geometry::NORMALS | rw::Geometry::TEXTURED;
    if (spec.lit) {
        flags |= rw::Geometry::LIGHT;
    }
    if (spec.modulate) {
        flags |= rw::Geometry::MODULATE;
    }
    if (spec.prelit) {
        flags |= rw::Geometry::PRELIT;
    }
    rw::Geometry* geometry = rw::Geometry::create(4, 2, flags);
    rw::MorphTarget& target = geometry->morphTargets[0];
    const std::array<std::array<float, 3>, 4> corners{spec.topLeft, spec.topRight, spec.bottomLeft, spec.bottomRight};
    const std::array<rw::TexCoords, 4> uvs{{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}}};
    for (rw::int32 i = 0; i < 4; ++i) {
        const auto& corner = corners[static_cast<std::size_t>(i)];
        target.vertices[i] = rw::makeV3d(corner[0], corner[1], corner[2]);
        target.normals[i] = rw::makeV3d(0.0F, 0.0F, spec.normalZ);
        geometry->texCoords[0][i] = uvs[static_cast<std::size_t>(i)];
        if (spec.prelit) {
            geometry->colors[i] = rw::makeRGBA(spec.prelight.r, spec.prelight.g, spec.prelight.b, spec.prelight.a);
        }
    }
    // Corners 0 top left, 1 top right, 2 bottom left, 3 bottom right: (0, 2, 1) and (1, 2, 3) wind counter-clockwise
    // on a screen whose y is up, the front faces OpenGL keeps.
    const std::array<std::array<rw::uint16, 3>, 2> front{{{0, 2, 1}, {1, 2, 3}}};
    for (std::size_t t = 0; t < 2; ++t) {
        rw::Triangle& triangle = geometry->triangles[t];
        triangle.v[0] = front[t][0];
        triangle.v[1] = spec.facingAway ? front[t][2] : front[t][1];
        triangle.v[2] = spec.facingAway ? front[t][1] : front[t][2];
        triangle.matId = 0;
    }
    rw::Material* material = rw::Material::create();
    material->color = rw::makeRGBA(spec.colour.r, spec.colour.g, spec.colour.b, spec.colour.a);
    if (spec.texture != nullptr) {
        material->setTexture(spec.texture);
    }
    geometry->matList.appendMaterial(material);
    material->destroy(); // the list holds its own reference
    geometry->buildMeshes();
    geometry->calculateBoundingSphere();
    rw::Atomic* atomic = rw::Atomic::create();
    atomic->setGeometry(geometry, 0);
    geometry->destroy(); // the atomic holds its own reference
    atomic->setFrame(rw::Frame::create());
    return atomic;
}

// Destroys `atomic` and its frame.
void destroyQuad(rw::Atomic* atomic) {
    rw::Frame* frame = atomic->getFrame();
    atomic->setFrame(nullptr);
    atomic->destroy();
    frame->destroy();
}

// The colour of pixel (`x`, `y`) of an RGBA image `width` wide, rows top first.
Rgba pixelAt(const std::vector<std::uint8_t>& rgba, int width, int x, int y) {
    const std::size_t at = ((static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + x) * 4;
    return Rgba{rgba[at], rgba[at + 1], rgba[at + 2], rgba[at + 3]};
}

// Whether `got` is `want` give or take `slack` in each colour channel (a GPU rounds its own way).
bool near(Rgba got, Rgba want, int slack) {
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

TEST_CASE("the GPU renderer draws a scene pass's meshes lit, textured, blended, fogged and depth tested",
          "[render][renderer][window][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene, Coney's own renderer drawing what librw's GL3 device draws
    // (scene_batch_test.cpp): a lit mesh with no prelighting is black plus the ambient (0.25) and the directional
    // light facing it (0.5) times its texel; a translucent painted mesh is blended by its alpha; a mesh at 75 of a fog
    // from 50 to 100 is half the fog's colour; the nearest of three meshes drawn far to near and then farther is the
    // one seen; a mesh facing away is culled. The window, camera and meshes are the test's own: 320 x 240, the view
    // 0.5 by 0.375 at a distance of 1, so at a distance of 10 each 80-pixel column is 2.5 units wide.
    if (!gpuHere()) {
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
    REQUIRE(engine.frameSize().width == 320);
    coney::platform::SceneAssets& assets = engine.sceneAssets();

    // A 2 x 2 texture taken to the nearest texel: red, green over blue, white.
    rw::Texture* texture = rw::Texture::create(nullptr);
    std::memcpy(texture->name, "check", 6); // the name and its terminator
    coney::graphics::TextureData texels;
    texels.name = "check";
    texels.levels.push_back({2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}});
    texels.sampler = {coney::graphics::TextureFilter::Nearest, coney::graphics::TextureAddress::Clamp,
                      coney::graphics::TextureAddress::Clamp};
    auto textureId = assets.addTexture(*texture, texels);
    REQUIRE(textureId.has_value());

    // The column of a distance `z`: from `left` to `right` pixels, the whole height.
    const auto column = [](float z, int left, int right, QuadSpec spec) {
        const float halfWidth = 0.5F * z;
        const float halfHeight = 0.375F * z;
        const auto x = [halfWidth](int pixel) { return (static_cast<float>(pixel) / 160.0F - 1.0F) * halfWidth; };
        spec.topLeft = {x(left), halfHeight, z};
        spec.topRight = {x(right), halfHeight, z};
        spec.bottomLeft = {x(left), -halfHeight, z};
        spec.bottomRight = {x(right), -halfHeight, z};
        return spec;
    };
    std::vector<rw::Atomic*> quads;
    std::vector<coney::graphics::MeshId> meshes;
    const auto add = [&](const QuadSpec& spec) {
        quads.push_back(makeQuad(spec));
        auto id = assets.addAtomic(*quads.back());
        REQUIRE(id.has_value());
        meshes.push_back(*id);
        return meshes.size() - 1;
    };
    const std::size_t textured = add(column(10.0F, 0, 80, QuadSpec{.lit = true, .texture = texture}));
    const std::size_t blue = add(column(10.0F, 80, 160, QuadSpec{.modulate = true, .prelit = true}));
    const std::size_t fogged = add(column(75.0F, 160, 240, QuadSpec{.prelit = true}));
    const std::size_t middle = add(column(30.0F, 240, 320, QuadSpec{.prelit = true, .prelight = {0, 255, 0, 255}}));
    const std::size_t nearest = add(column(20.0F, 240, 320, QuadSpec{.prelit = true, .prelight = {255, 0, 0, 255}}));
    const std::size_t farthest = add(column(40.0F, 240, 320, QuadSpec{.prelit = true, .prelight = {255, 255, 0, 255}}));
    const std::size_t away =
        add(column(5.0F, 0, 320, QuadSpec{.prelit = true, .prelight = {255, 0, 255, 255}, .facingAway = true}));

    const std::string path = (std::filesystem::temp_directory_path() / "coney-gpu-scene-capture.png").string();
    engine.requestCapture(0, path);
    const Rgba clear{100, 100, 100, 255};
    engine.beginWindowFrame(clear);
    coney::graphics::SceneCamera camera;
    camera.halfWidth = 0.5F;
    camera.halfHeight = 0.375F;
    camera.nearClip = 1.0F;
    camera.farClip = 100.0F;
    camera.fogStart = 50.0F;
    engine.setSceneCamera(camera);
    const std::array<coney::graphics::SceneLight, 2> lights{{
        {.type = coney::graphics::LightType::Ambient, .colour = {0.25F, 0.25F, 0.25F}},
        {.type = coney::graphics::LightType::Directional,
         .colour = {0.5F, 0.5F, 0.5F},
         .direction = {0.0F, 0.0F, 1.0F}},
    }};
    coney::graphics::SceneStates states;
    states.fogColour = Rgba{0, 200, 0, 255};
    const auto draw = [&](std::size_t which, coney::graphics::Material3d material = {}) {
        coney::graphics::Draw3d scene;
        scene.mesh = meshes[which];
        scene.material = material;
        scene.states = states;
        engine.drawScene(scene, lights);
    };
    draw(textured);
    draw(blue, {.colour = coney::graphics::MaterialColour::Paint, .tint = Rgba{0, 0, 255, 128}});
    draw(fogged);
    draw(middle);
    draw(nearest);
    draw(farthest);
    draw(away);
    engine.present();

    const auto captured = coney::test::got(engine.capture());
    REQUIRE(captured.has_value());
    int width = 0;
    int height = 0;
    const std::vector<std::uint8_t> rgba = readCapture(path, width, height);
    REQUIRE(width == 320);
    REQUIRE(height == 240);
    // Lit at 0.75: the texture's quarters, red at the top left.
    CHECK(near(pixelAt(rgba, width, 20, 60), Rgba{191, 0, 0, 255}, 2));
    CHECK(near(pixelAt(rgba, width, 60, 60), Rgba{0, 191, 0, 255}, 2));
    CHECK(near(pixelAt(rgba, width, 20, 180), Rgba{0, 0, 191, 255}, 2));
    CHECK(near(pixelAt(rgba, width, 60, 180), Rgba{191, 191, 191, 255}, 2));
    // Blue at half alpha over the clear colour.
    CHECK(near(pixelAt(rgba, width, 120, 120), Rgba{50, 50, 178, 255}, 2));
    // White, half fogged.
    CHECK(near(pixelAt(rgba, width, 200, 120), Rgba{128, 228, 128, 255}, 2));
    // The nearest of the three.
    CHECK(near(pixelAt(rgba, width, 280, 120), Rgba{255, 0, 0, 255}, 2));

    for (rw::Atomic* atomic : quads) {
        destroyQuad(atomic);
    }
    for (const coney::graphics::MeshId mesh : meshes) {
        assets.removeMesh(mesh);
    }
    assets.removeTexture(*textureId);
    texture->destroy();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("the GPU renderer skins a posed mesh by its bones", "[render][renderer][window][scene][skinning]") {
    // Expected: ../engine/gpu-renderer.md#skinning: a posed draw's mesh, its vertices weighted to bones, is placed by
    // its pose as the GL3 renderer's skinning shader places it (platform/character_skin.cpp; the formula is checked
    // headless against characters::skinVertices() in scene_batch_test.cpp): its positions moved and its normals turned
    // by each bone times the weight, then by the draw's transform. Two quads at a distance of 10 in the test's 320 x
    // 240 view (an 80-pixel column is 2.5 units): a red prelit one in the first column, all on bone 0, which moves it 5
    // units right into the third; and a lit one in the fourth column, textured with the scene test's 2 x 2 check,
    // whose normals face away from the light, split between bones 1 and 2, each turning it half a turn about its own
    // centre, so it faces the light and is lit at 0.25 + 0.5 (an ambient and a directional light, as the scene test's),
    // its texture mirrored left to right: the bottom row white, then blue.
    if (!gpuHere()) {
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
    coney::platform::SceneAssets& assets = engine.sceneAssets();
    rw::Texture* texture = rw::Texture::create(nullptr);
    std::memcpy(texture->name, "check", 6); // the name and its terminator
    coney::graphics::TextureData texels;
    texels.name = "check";
    texels.levels.push_back({2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}});
    texels.sampler = {coney::graphics::TextureFilter::Nearest, coney::graphics::TextureAddress::Clamp,
                      coney::graphics::TextureAddress::Clamp};
    auto textureId = assets.addTexture(*texture, texels);
    REQUIRE(textureId.has_value());

    const auto column = [](int left, int right, QuadSpec spec) {
        const auto x = [](int pixel) { return (static_cast<float>(pixel) / 160.0F - 1.0F) * 5.0F; };
        spec.topLeft = {x(left), 3.75F, 10.0F};
        spec.topRight = {x(right), 3.75F, 10.0F};
        spec.bottomLeft = {x(left), -3.75F, 10.0F};
        spec.bottomRight = {x(right), -3.75F, 10.0F};
        return spec;
    };
    // Every vertex of a quad on the same bones and weights.
    const auto skinOf = [](std::array<std::uint8_t, 4> bones, std::array<float, 4> weights) {
        coney::platform::SkinVertexData skin;
        skin.weights.assign(4, weights);
        skin.indices.assign(4, bones);
        skin.bones = 3;
        return skin;
    };
    const coney::platform::SkinVertexData moved = skinOf({0, 0, 0, 0}, {1.0F, 0.0F, 0.0F, 0.0F});
    const coney::platform::SkinVertexData turned = skinOf({1, 2, 0, 0}, {0.5F, 0.5F, 0.0F, 0.0F});
    rw::Atomic* red = makeQuad(column(0, 80, QuadSpec{.prelit = true, .prelight = {255, 0, 0, 255}}));
    rw::Atomic* white = makeQuad(column(240, 320, QuadSpec{.lit = true, .texture = texture, .normalZ = 1.0F}));
    auto redId = assets.addAtomic(*red, &moved);
    auto whiteId = assets.addAtomic(*white, &turned);
    REQUIRE(redId.has_value());
    REQUIRE(whiteId.has_value());

    const std::string path = (std::filesystem::temp_directory_path() / "coney-gpu-skin-capture.png").string();
    engine.requestCapture(0, path);
    engine.beginWindowFrame(Rgba{100, 100, 100, 255});
    coney::graphics::SceneCamera camera;
    camera.halfWidth = 0.5F;
    camera.halfHeight = 0.375F;
    camera.nearClip = 1.0F;
    camera.farClip = 100.0F;
    camera.fogStart = 50.0F; // beyond the quads
    engine.setSceneCamera(camera);
    const std::array<coney::graphics::SceneLight, 2> lights{{
        {.type = coney::graphics::LightType::Ambient, .colour = {0.25F, 0.25F, 0.25F}},
        {.type = coney::graphics::LightType::Directional,
         .colour = {0.5F, 0.5F, 0.5F},
         .direction = {0.0F, 0.0F, 1.0F}},
    }};
    // Bone 0 moves 5 units right; bones 1 and 2 turn half a turn about y round the white quad's centre (3.75, 0, 10).
    const std::array<coney::graphics::Transform3d, 3> bones{{
        {.position = {5.0F, 0.0F, 0.0F}},
        {.right = {-1.0F, 0.0F, 0.0F},
         .up = {0.0F, 1.0F, 0.0F},
         .at = {0.0F, 0.0F, -1.0F},
         .position = {7.5F, 0.0F, 20.0F}},
        {.right = {-1.0F, 0.0F, 0.0F},
         .up = {0.0F, 1.0F, 0.0F},
         .at = {0.0F, 0.0F, -1.0F},
         .position = {7.5F, 0.0F, 20.0F}},
    }};
    coney::graphics::Draw3d draw;
    draw.states.cull = coney::graphics::CullFaces::None; // the half turn winds the white quad the other way
    draw.mesh = *redId;
    engine.drawScene(draw, lights, bones);
    draw.mesh = *whiteId;
    engine.drawScene(draw, lights, bones);
    engine.present();

    const auto captured = coney::test::got(engine.capture());
    REQUIRE(captured.has_value());
    int width = 0;
    int height = 0;
    const std::vector<std::uint8_t> rgba = readCapture(path, width, height);
    REQUIRE(width == 320);
    REQUIRE(height == 240);
    CHECK(near(pixelAt(rgba, width, 40, 120), Rgba{100, 100, 100, 255}, 2));  // moved away
    CHECK(near(pixelAt(rgba, width, 200, 120), Rgba{255, 0, 0, 255}, 2));     // to here
    CHECK(near(pixelAt(rgba, width, 260, 180), Rgba{191, 191, 191, 255}, 2)); // turned to the light: white
    CHECK(near(pixelAt(rgba, width, 300, 180), Rgba{0, 0, 191, 255}, 2));     // and blue

    destroyQuad(red);
    destroyQuad(white);
    assets.removeMesh(*redId);
    assets.removeMesh(*whiteId);
    assets.removeTexture(*textureId);
    texture->destroy();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("the GPU renderer clears the depth for a scene pass that asks and lights a draw that is lit",
          "[render][renderer][window][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene and docs/research/level-loading.md#render-order: after the level's
    // skyline the depth alone is cleared (LevelObject_RenderBackground 0x0040d0a8 step 4), so the world drawn next,
    // farther than the skyline, still shows (the left half: a near red quad, then the clear, then a far green one);
    // the background's models have no light flag and are lit all the same (docs/research/lighting.md
    // #coneys-implementation): a prelit grey (60) quad lit by an ambient of 0.25 is 60 + 64 = 124, unlit 60 (the right
    // half, its top lit and its bottom not). The window and camera are the test's own, as in the case above; a strip
    // along the bottom of the left half is a linearly filtered 2 x 2 texture stretched over it, so the picture holds
    // more colours than a palette (the capture's PNG is then one librw reads).
    if (!gpuHere()) {
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
    coney::platform::SceneAssets& assets = engine.sceneAssets();

    // A quad at distance `z` from `left` to `right` pixels and from `top` to `bottom` pixels.
    const auto quadAt = [](float z, int left, int right, int top, int bottom, QuadSpec spec) {
        const float halfWidth = 0.5F * z;
        const float halfHeight = 0.375F * z;
        const auto x = [halfWidth](int pixel) { return (static_cast<float>(pixel) / 160.0F - 1.0F) * halfWidth; };
        const auto y = [halfHeight](int pixel) { return (1.0F - static_cast<float>(pixel) / 120.0F) * halfHeight; };
        spec.topLeft = {x(left), y(top), z};
        spec.topRight = {x(right), y(top), z};
        spec.bottomLeft = {x(left), y(bottom), z};
        spec.bottomRight = {x(right), y(bottom), z};
        return spec;
    };
    std::vector<rw::Atomic*> quads;
    std::vector<coney::graphics::MeshId> meshes;
    const auto add = [&](const QuadSpec& spec) {
        quads.push_back(makeQuad(spec));
        auto id = assets.addAtomic(*quads.back());
        REQUIRE(id.has_value());
        meshes.push_back(*id);
        return meshes.size() - 1;
    };
    // A 2 x 2 texture sampled linearly: red, green over blue, white.
    rw::Texture* texture = rw::Texture::create(nullptr);
    std::memcpy(texture->name, "blend", 6); // the name and its terminator
    coney::graphics::TextureData texels;
    texels.name = "blend";
    texels.levels.push_back({2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}});
    texels.sampler = {coney::graphics::TextureFilter::Linear, coney::graphics::TextureAddress::Clamp,
                      coney::graphics::TextureAddress::Clamp};
    auto textureId = assets.addTexture(*texture, texels);
    REQUIRE(textureId.has_value());
    const std::size_t nearRed =
        add(quadAt(10.0F, 0, 160, 0, 200, QuadSpec{.prelit = true, .prelight = {255, 0, 0, 255}}));
    const std::size_t farGreen =
        add(quadAt(40.0F, 0, 160, 0, 200, QuadSpec{.prelit = true, .prelight = {0, 255, 0, 255}}));
    const std::size_t strip = add(quadAt(10.0F, 0, 160, 200, 240, QuadSpec{.prelit = true, .texture = texture}));
    const std::size_t litGrey =
        add(quadAt(10.0F, 160, 320, 0, 120, QuadSpec{.prelit = true, .prelight = {60, 60, 60, 255}}));
    const std::size_t unlitGrey =
        add(quadAt(10.0F, 160, 320, 120, 240, QuadSpec{.prelit = true, .prelight = {60, 60, 60, 255}}));

    const std::string path = (std::filesystem::temp_directory_path() / "coney-gpu-depth-clear.png").string();
    engine.requestCapture(0, path);
    engine.beginWindowFrame(Rgba{0, 0, 0, 255});
    coney::graphics::SceneCamera camera;
    camera.halfWidth = 0.5F;
    camera.halfHeight = 0.375F;
    camera.nearClip = 1.0F;
    camera.farClip = 100.0F;
    camera.fogStart = 50.0F;
    engine.setSceneCamera(camera);
    const std::array<coney::graphics::SceneLight, 1> ambient{
        {{.type = coney::graphics::LightType::Ambient, .colour = {0.25F, 0.25F, 0.25F}}}};
    coney::graphics::SceneStates states;
    states.fog = false;
    const auto draw = [&](std::size_t which, coney::graphics::Material3d material = {}) {
        coney::graphics::Draw3d scene;
        scene.mesh = meshes[which];
        scene.material = material;
        scene.states = states;
        engine.drawScene(scene, ambient);
    };
    draw(nearRed);
    draw(strip);
    const coney::graphics::Material3d background{.colour = coney::graphics::MaterialColour::Own, .lit = true};
    draw(litGrey, background);
    draw(unlitGrey, coney::graphics::Material3d{.colour = coney::graphics::MaterialColour::Own});
    engine.clearSceneDepth();
    draw(farGreen);
    engine.present();

    const auto captured = coney::test::got(engine.capture());
    REQUIRE(captured.has_value());

    int width = 0;
    int height = 0;
    const std::vector<std::uint8_t> rgba = readCapture(path, width, height);
    REQUIRE(width == 320);
    REQUIRE(height == 240);
    CHECK(near(pixelAt(rgba, width, 80, 120), Rgba{0, 255, 0, 255}, 2));
    CHECK(near(pixelAt(rgba, width, 240, 60), Rgba{124, 124, 124, 255}, 2));
    CHECK(near(pixelAt(rgba, width, 240, 180), Rgba{60, 60, 60, 255}, 2));

    for (rw::Atomic* atomic : quads) {
        destroyQuad(atomic);
    }
    for (const coney::graphics::MeshId mesh : meshes) {
        assets.removeMesh(mesh);
    }
    assets.removeTexture(*textureId);
    texture->destroy();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

TEST_CASE("the GPU renderer cuts away what lies past the camera's far clip", "[render][renderer][window][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: what lies past the far clip is not drawn, as librw's GL3 device and
    // OpenGL clip it (the GL3 picture the GPU renderer is compared with); SDL's default clamps its depth onto the far
    // plane instead, which drew the level world's far light glows as fogged disks over the sky. A red quad at 150 with
    // a far clip of 100 leaves the black clear (the left half); a green one at 50 shows (the right half). A strip along
    // the bottom is a linearly filtered 2 x 2 texture stretched over it, so the picture holds more colours than a
    // palette (the capture's PNG is then one librw reads), as in the case above.
    if (!gpuHere()) {
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
    coney::platform::SceneAssets& assets = engine.sceneAssets();

    // A prelit quad at distance `z` from `left` to `right` pixels and from `top` to `bottom` pixels.
    const auto quadAt = [](float z, int left, int right, int top, int bottom, Rgba prelight, rw::Texture* texture) {
        const float halfWidth = 0.5F * z;
        const float halfHeight = 0.375F * z;
        const auto x = [halfWidth](int pixel) { return (static_cast<float>(pixel) / 160.0F - 1.0F) * halfWidth; };
        const auto y = [halfHeight](int pixel) { return (1.0F - static_cast<float>(pixel) / 120.0F) * halfHeight; };
        return QuadSpec{.topLeft = {x(left), y(top), z},
                        .topRight = {x(right), y(top), z},
                        .bottomLeft = {x(left), y(bottom), z},
                        .bottomRight = {x(right), y(bottom), z},
                        .prelit = true,
                        .prelight = prelight,
                        .texture = texture};
    };
    // A 2 x 2 texture sampled linearly: red, green over blue, white.
    rw::Texture* texture = rw::Texture::create(nullptr);
    std::memcpy(texture->name, "blend", 6); // the name and its terminator
    coney::graphics::TextureData texels;
    texels.name = "blend";
    texels.levels.push_back({2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}});
    texels.sampler = {coney::graphics::TextureFilter::Linear, coney::graphics::TextureAddress::Clamp,
                      coney::graphics::TextureAddress::Clamp};
    auto textureId = assets.addTexture(*texture, texels);
    REQUIRE(textureId.has_value());
    std::vector<rw::Atomic*> quads;
    std::vector<coney::graphics::MeshId> meshes;
    const std::array<QuadSpec, 3> specs{quadAt(150.0F, 0, 160, 0, 200, Rgba{255, 0, 0, 255}, nullptr),
                                        quadAt(50.0F, 160, 320, 0, 200, Rgba{0, 255, 0, 255}, nullptr),
                                        quadAt(10.0F, 0, 320, 200, 240, Rgba{255, 255, 255, 255}, texture)};
    for (const QuadSpec& spec : specs) {
        quads.push_back(makeQuad(spec));
        auto id = assets.addAtomic(*quads.back());
        REQUIRE(id.has_value());
        meshes.push_back(*id);
    }

    const std::string path = (std::filesystem::temp_directory_path() / "coney-gpu-far-clip.png").string();
    engine.requestCapture(0, path);
    engine.beginWindowFrame(Rgba{0, 0, 0, 255});
    coney::graphics::SceneCamera camera;
    camera.halfWidth = 0.5F;
    camera.halfHeight = 0.375F;
    camera.nearClip = 1.0F;
    camera.farClip = 100.0F;
    camera.fogStart = 90.0F;
    engine.setSceneCamera(camera);
    coney::graphics::SceneStates states;
    states.fog = false;
    for (const coney::graphics::MeshId mesh : meshes) {
        coney::graphics::Draw3d scene;
        scene.mesh = mesh;
        scene.states = states;
        engine.drawScene(scene, {});
    }
    engine.present();

    const auto captured = coney::test::got(engine.capture());
    REQUIRE(captured.has_value());

    int width = 0;
    int height = 0;
    const std::vector<std::uint8_t> rgba = readCapture(path, width, height);
    REQUIRE(width == 320);
    REQUIRE(height == 240);
    CHECK(near(pixelAt(rgba, width, 80, 120), Rgba{0, 0, 0, 255}, 2));
    CHECK(near(pixelAt(rgba, width, 240, 120), Rgba{0, 255, 0, 255}, 2));

    for (rw::Atomic* atomic : quads) {
        destroyQuad(atomic);
    }
    for (const coney::graphics::MeshId mesh : meshes) {
        assets.removeMesh(mesh);
    }
    assets.removeTexture(*textureId);
    texture->destroy();
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}
