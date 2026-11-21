// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's reading of the frame description's scene passes, headless (docs/engine/gpu-renderer.md#scene):
// the camera's matrices, the lights, colours and passes each sub-mesh is drawn with, the texture level and the fog. The
// GPU renderer has no counterpart in the original; what it must match is librw's GL3 device, the picture it is compared
// with (docs/engine/renderer.md#comparing), so the expected values are librw's, at the pin cmake/deps.cmake names:
// the camera's view and projection (gl3device.cpp's beginUpdate), the lights an atomic gets (world.cpp's
// enumerateLights, gl3device.cpp's setLights), the material colour (rwgl3.h's setMaterial), the blending and the GS
// alpha test's two passes (gl3device.cpp's setVertexAlpha and setTexture, gl3render.cpp's drawInst_GSemu), the alpha
// of an image (image.cpp's hasAlpha), the fog (header.vert's DoFog), and the GL3 renderer's own (platform/
// texture_lod.cpp's level of detail, platform/lit_atomic.cpp's colours and dual layer).
#include "graphics/scene_batch.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "characters/character_model.h"
#include "characters/character_rig.h"
#include "graphics/frame_description.h"
#include "graphics/mesh_data.h"
#include "graphics/overlay_batch.h"
#include "graphics/texture_data.h"
#include "graphics/texture_lod.h"

using Catch::Approx;
using coney::graphics::AlphaRange;
using coney::graphics::AlphaTest;
using coney::graphics::DrawColours;
using coney::graphics::DrawLights;
using coney::graphics::LightType;
using coney::graphics::Material3d;
using coney::graphics::MaterialColour;
using coney::graphics::MeshData;
using coney::graphics::MeshMaterial;
using coney::graphics::MeshVertex;
using coney::graphics::Rgba;
using coney::graphics::SceneCamera;
using coney::graphics::SceneDrawPass;
using coney::graphics::SceneLight;
using coney::graphics::SceneStates;
using coney::graphics::SubMesh;
using coney::graphics::TextureData;
using coney::graphics::Transform3d;

namespace {

// A camera 20 units behind (10, 5, 0), looking along RenderWare's z with the screen's right along x, as the world
// scene of the draw-stream hashes views its sectors (world_draw_stream_test.cpp).
SceneCamera testCamera() {
    SceneCamera camera;
    camera.frame.right = {1.0F, 0.0F, 0.0F};
    camera.frame.up = {0.0F, 1.0F, 0.0F};
    camera.frame.at = {0.0F, 0.0F, 1.0F};
    camera.frame.position = {10.0F, 5.0F, -20.0F};
    camera.halfWidth = 0.6F;
    camera.halfHeight = 0.45F;
    camera.nearClip = 0.5F;
    camera.farClip = 100.0F;
    camera.fogStart = 50.0F;
    return camera;
}

// `point` through the camera's view and projection, divided by w: x and y from -1 to 1 across the view (y up), the
// depth from 0 to 1, and w itself.
std::array<float, 4> project(const SceneCamera& camera, const std::array<float, 3>& point) {
    const auto clip = coney::graphics::transformPoint(
        coney::graphics::multiply(coney::graphics::projectionOf(camera), coney::graphics::viewMatrixOf(camera)), point);
    return {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3], clip[3]};
}

// A lit mesh of one triangle with normals and a sphere of radius 1 round (0, 0, 0), materials as given.
MeshData litMesh(std::vector<MeshMaterial> materials = {MeshMaterial{}}) {
    MeshData mesh;
    mesh.vertices.resize(3);
    mesh.indices = {0, 1, 2};
    mesh.subMeshes = {SubMesh{0, 3, 0}};
    mesh.materials = std::move(materials);
    mesh.attributes.normals = true;
    mesh.bounds = {{0.0F, 0.0F, 0.0F}, 1.0F};
    mesh.lit = true;
    return mesh;
}

// A light of `type` coloured (`r`, 0, 0).
SceneLight light(LightType type, float r) {
    SceneLight made;
    made.type = type;
    made.colour = {r, 0.0F, 0.0F};
    return made;
}

// A point light of radius `radius` at `position`, coloured (`r`, 0, 0).
SceneLight pointLight(std::array<float, 3> position, float radius, float r) {
    SceneLight made = light(LightType::Point, r);
    made.position = position;
    made.radius = radius;
    return made;
}

} // namespace

TEST_CASE("a scene camera projects as librw's GL3 camera does", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: librw's beginUpdate, view x along the screen's right, y up, z ahead;
    // projection x / half width, y / half height, w the distance, depth (far * (z - near)) / (z * (far - near)).
    const SceneCamera camera = testCamera();
    const auto ahead = project(camera, {10.0F, 5.0F, -10.0F}); // 10 in front, on the axis
    CHECK(ahead[0] == Approx(0.0F).margin(1e-6));
    CHECK(ahead[1] == Approx(0.0F).margin(1e-6));
    CHECK(ahead[3] == Approx(10.0F));
    CHECK(ahead[2] == Approx(100.0F * 9.5F / (10.0F * 99.5F)));
    const auto right = project(camera, {16.0F, 5.0F, -10.0F}); // 6 to the right: the view's right edge at 0.6 x 10
    CHECK(right[0] == Approx(1.0F));
    const auto up = project(camera, {10.0F, 9.5F, -10.0F}); // 4.5 up: the top edge
    CHECK(up[1] == Approx(1.0F));
    CHECK(project(camera, {10.0F, 5.0F, -19.5F})[2] == Approx(0.0F).margin(1e-6)); // the near plane
    CHECK(project(camera, {10.0F, 5.0F, 80.0F})[2] == Approx(1.0F));               // the far plane

    // A turned camera: looking along x, the screen's right along -z.
    SceneCamera turned = camera;
    turned.frame.right = {0.0F, 0.0F, -1.0F};
    turned.frame.at = {1.0F, 0.0F, 0.0F};
    const auto side = project(turned, {20.0F, 5.0F, -26.0F}); // 10 ahead, 6 to the right
    CHECK(side[0] == Approx(1.0F));
    CHECK(side[3] == Approx(10.0F));
}

TEST_CASE("a mesh's world matrix places it by its frame", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: librw's world matrix, x right + y up + z at + position.
    Transform3d where;
    where.right = {0.0F, 1.0F, 0.0F};
    where.up = {-1.0F, 0.0F, 0.0F};
    where.at = {0.0F, 0.0F, 2.0F};
    where.position = {5.0F, 6.0F, 7.0F};
    const auto placed = coney::graphics::transformPoint(coney::graphics::worldMatrixOf(where), {1.0F, 2.0F, 3.0F});
    CHECK(placed[0] == Approx(3.0F));
    CHECK(placed[1] == Approx(7.0F));
    CHECK(placed[2] == Approx(13.0F));
    CHECK(placed[3] == Approx(1.0F));
}

TEST_CASE("a mesh takes the lights librw's GL3 device gives an atomic", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: enumerateLights and setLights: the ambients summed, the directional
    // lights with normals, the local lights with normals whose sphere meets the atomic's, directionals first.
    const std::vector<SceneLight> lights{pointLight({1.5F, 0.0F, 0.0F}, 1.0F, 0.1F), light(LightType::Ambient, 0.2F),
                                         light(LightType::Directional, 0.3F), light(LightType::Ambient, 0.25F),
                                         pointLight({5.0F, 0.0F, 0.0F}, 1.0F, 0.4F)};
    const MeshData mesh = litMesh();
    const DrawLights got = coney::graphics::lightsFor(lights, mesh, Transform3d{});
    CHECK(got.ambient[0] == Approx(0.45F));
    REQUIRE(got.count == 2); // the far point light's sphere misses the mesh's
    CHECK(got.lights[0].type == LightType::Directional);
    CHECK(got.lights[0].colour[0] == Approx(0.3F));
    CHECK(got.lights[0].direction[1] == Approx(-1.0F)); // a SceneLight's default direction, made unit
    CHECK(got.lights[1].type == LightType::Point);
    CHECK(got.lights[1].colour[0] == Approx(0.1F));
    CHECK(got.lights[1].position[0] == Approx(1.5F));
    CHECK(got.lights[1].radius == Approx(1.0F));

    // The mesh moved next to the far light: that one meets it instead.
    Transform3d moved;
    moved.position = {4.0F, 0.0F, 0.0F};
    const DrawLights beside = coney::graphics::lightsFor(lights, mesh, moved);
    REQUIRE(beside.count == 2);
    CHECK(beside.lights[1].colour[0] == Approx(0.4F));

    // Without normals only the ambients; not lit, nothing.
    MeshData flat = mesh;
    flat.attributes.normals = false;
    const DrawLights unshaded = coney::graphics::lightsFor(lights, flat, Transform3d{});
    CHECK(unshaded.count == 0);
    CHECK(unshaded.ambient[0] == Approx(0.45F));
    MeshData unlit = mesh;
    unlit.lit = false;
    const DrawLights none = coney::graphics::lightsFor(lights, unlit, Transform3d{});
    CHECK(none.count == 0);
    CHECK(none.ambient[0] == Approx(0.0F));
}

TEST_CASE("a mesh takes at most eight lights, the directional ones first", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: setLights's MAX_LIGHTS of 8 and enumerateLights's 8 locals.
    std::vector<SceneLight> lights;
    lights.reserve(13); // ten point lights, then three directional ones
    for (int i = 0; i < 10; ++i) {
        lights.push_back(pointLight({0.0F, 0.0F, 0.0F}, 2.0F, static_cast<float>(i)));
    }
    for (int i = 0; i < 3; ++i) {
        SceneLight directional = light(LightType::Directional, 100.0F + static_cast<float>(i));
        directional.direction = {0.0F, 0.0F, 2.0F};
        lights.push_back(directional);
    }
    SceneLight spot = pointLight({0.0F, 0.0F, 0.0F}, 2.0F, 50.0F);
    spot.type = LightType::Spot;
    spot.coneAngle = 0.5F;
    lights.insert(lights.begin(), spot);
    const DrawLights got = coney::graphics::lightsFor(lights, litMesh(), Transform3d{});
    REQUIRE(got.count == 8);
    CHECK(got.lights[0].colour[0] == Approx(100.0F));
    CHECK(got.lights[2].colour[0] == Approx(102.0F));
    CHECK(got.lights[2].direction[2] == Approx(1.0F)); // made unit
    CHECK(got.lights[3].type == LightType::Spot);
    CHECK(got.lights[3].minusCosAngle == Approx(-std::cos(0.5F)));
    CHECK(got.lights[4].colour[0] == Approx(0.0F));
    CHECK(got.lights[7].colour[0] == Approx(3.0F));
}

TEST_CASE("a draw changes its mesh's material colours as the GL3 renderer does", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: platform::MaterialChange (the fade, the tint on the first material
    // with MODULATE while not opaque white, the paint) and setMaterial's white for a mesh that does not modulate.
    const MeshData mesh = litMesh({MeshMaterial{Rgba{10, 20, 30, 255}, ""}, MeshMaterial{Rgba{40, 50, 60, 200}, ""}});

    const DrawColours faded = coney::graphics::coloursOf(mesh, Material3d{.fade = 100});
    REQUIRE(faded.colours.size() == 2);
    CHECK(faded.colours[0] == Rgba{10, 20, 30, 100});
    CHECK(faded.colours[1] == Rgba{40, 50, 60, 100});
    CHECK_FALSE(faded.modulate);

    const DrawColours tinted =
        coney::graphics::coloursOf(mesh, Material3d{.colour = MaterialColour::Tint, .tint = Rgba{255, 0, 0, 128}});
    REQUIRE(tinted.colours.size() == 2);
    CHECK(tinted.colours[0] == Rgba{255, 0, 0, 128});
    CHECK(tinted.colours[1] == Rgba{40, 50, 60, 200});
    CHECK(tinted.modulate);
    CHECK_FALSE(
        coney::graphics::coloursOf(mesh, Material3d{.colour = MaterialColour::Tint, .tint = coney::graphics::kWhite})
            .modulate);

    MeshData modulated = mesh;
    modulated.modulate = true;
    const DrawColours painted =
        coney::graphics::coloursOf(modulated, Material3d{.colour = MaterialColour::Paint, .tint = Rgba{1, 2, 3, 4}});
    REQUIRE(painted.colours.size() == 2);
    CHECK(painted.colours[0] == Rgba{1, 2, 3, 4});
    CHECK(painted.colours[1] == Rgba{1, 2, 3, 4});
    CHECK(painted.modulate);

    // A dual layer: its own colours at the base's first alpha.
    const MeshData layer = litMesh({MeshMaterial{Rgba{90, 80, 70, 255}, ""}});
    const DrawColours over = coney::graphics::layerColoursOf(layer, faded);
    REQUIRE(over.colours.size() == 1);
    CHECK(over.colours[0] == Rgba{90, 80, 70, 100});
    CHECK_FALSE(over.modulate);
}

TEST_CASE("a human's draw shades his first material as the GL3 renderer does", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: platform::MaterialChange under MaterialColour::Shade (the human's
    // colour with red, green and blue times the shade, truncated, the fade as its alpha, in place of the first
    // material's own colour; the other materials and the mesh's own modulation untouched), as material_change_test.cpp
    // checks it on the librw atomic.
    const MeshData mesh =
        litMesh({MeshMaterial{Rgba{0x96, 0x96, 0x96, 255}, ""}, MeshMaterial{Rgba{10, 20, 255, 240}, ""}});

    // A white human (no HuColor) at half the dimming: 255 x 0.5 = 127.5, truncated.
    const DrawColours dimmed =
        coney::graphics::coloursOf(mesh, Material3d{.colour = MaterialColour::Shade, .fade = 0x60, .shade = 0.5F});
    REQUIRE(dimmed.colours.size() == 2);
    CHECK(dimmed.colours[0] == Rgba{127, 127, 127, 0x60});
    CHECK(dimmed.colours[1] == Rgba{10, 20, 255, 240});
    CHECK_FALSE(dimmed.modulate);

    // A coloured human undimmed takes his colour as it is; a shade past 1 is held at 1.
    MeshData modulated = mesh;
    modulated.modulate = true;
    const DrawColours coloured = coney::graphics::coloursOf(
        modulated,
        Material3d{.colour = MaterialColour::Shade, .fade = 0xFF, .tint = Rgba{200, 100, 50, 7}, .shade = 2.0F});
    CHECK(coloured.colours[0] == Rgba{200, 100, 50, 0xFF});
    CHECK(coloured.modulate);
}

TEST_CASE("a posed draw is skinned when its mesh has the bones and weights, and left out otherwise",
          "[graphics][gpu-renderer][scene][skinning]") {
    // Expected: ../engine/gpu-renderer.md#skinning: a draw with no pose is drawn as it is; a posed draw (a human,
    // FrameDescription::bonesOf()) is skinned when its mesh carries bones and weights and the pose holds every bone
    // its vertices name, and is left out otherwise rather than drawn in its bind pose. The bones a mesh needs are the
    // highest index a vertex weighs plus one; a slot with no weight names none.
    MeshData mesh = litMesh();
    mesh.vertices[0].bones = {0, 7, 0, 0};
    mesh.vertices[0].weights = {0.5F, 0.5F, 0.0F, 0.0F};
    mesh.vertices[1].bones = {2, 0, 9, 0};
    mesh.vertices[1].weights = {1.0F, 0.0F, 0.0F, 0.0F}; // bone 9 has no weight
    CHECK(coney::graphics::bonesNeededBy(mesh) == 0);    // not skinned
    mesh.attributes.skinned = true;
    CHECK(coney::graphics::bonesNeededBy(mesh) == 8);

    coney::graphics::FrameDescription frame;
    frame.begin(coney::graphics::FrameStart{});
    const std::vector<Transform3d> eight(8, Transform3d{.position = {1.0F, 2.0F, 3.0F}});
    coney::graphics::Draw3d draw;
    draw.mesh = coney::graphics::MeshId{3};
    frame.addSceneDraw(draw, {}, {}, {}, eight);
    frame.addSceneDraw(draw, {}, {}, {});
    frame.addSceneDraw(draw, {}, {}, {}, std::span<const Transform3d>(eight).first(7));
    const auto& scene = std::get<coney::graphics::ScenePass>(frame.passes()[0]);
    REQUIRE(scene.draws.size() == 3);
    using coney::graphics::Posing;
    CHECK(coney::graphics::posingOf(frame, scene.draws[0], mesh, 8) == Posing::Skinned);
    CHECK(coney::graphics::posingOf(frame, scene.draws[1], mesh, 8) == Posing::None);
    CHECK(coney::graphics::posingOf(frame, scene.draws[2], mesh, 8) == Posing::LeftOut); // bone 7 missing
    MeshData unskinned = mesh;
    unskinned.attributes.skinned = false;
    CHECK(coney::graphics::posingOf(frame, scene.draws[0], unskinned, 0) == Posing::LeftOut);
    CHECK(coney::graphics::posingOf(frame, scene.draws[1], unskinned, 0) == Posing::None);
    // The frame's poses at once, each draw's from its first bone.
    CHECK(frame.bones().size() == 15);
    CHECK(scene.draws[2].firstBone == 8);
}

TEST_CASE("a bone palette holds each skinning matrix column by column, as the GL3 renderer's",
          "[graphics][gpu-renderer][scene][skinning]") {
    // Expected: ../guides/performance.md#gpu-skinning: the GL3 renderer's palette, each skinning matrix a column-major
    // 4 x 4 (platform::fillBonePalette(), checked against it in character_mesh_skin_test.cpp): its x, y and z axes and
    // its translation as the columns, (0, 0, 0, 1) the last row.
    const std::array<Transform3d, 2> bones{Transform3d{.right = {1.0F, 2.0F, 3.0F},
                                                       .up = {4.0F, 5.0F, 6.0F},
                                                       .at = {7.0F, 8.0F, 9.0F},
                                                       .position = {10.0F, 11.0F, 12.0F}},
                                           Transform3d{.position = {-1.0F, 0.5F, 2.0F}}};
    const std::vector<coney::graphics::Matrix4> palette = coney::graphics::bonePaletteOf(bones);
    REQUIRE(palette.size() == 2);
    CHECK(palette[0] == coney::graphics::Matrix4{1.0F, 2.0F, 3.0F, 0.0F, 4.0F, 5.0F, 6.0F, 0.0F, 7.0F, 8.0F, 9.0F, 0.0F,
                                                 10.0F, 11.0F, 12.0F, 1.0F});
    CHECK(palette[1] == coney::graphics::Matrix4{1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
                                                 -1.0F, 0.5F, 2.0F, 1.0F});
}

TEST_CASE("the GPU skinning formula places a vertex as characters::skinVertices() does",
          "[graphics][gpu-renderer][scene][skinning]") {
    // Expected: ../engine/gpu-renderer.md#skinning: the shader blends a vertex as the GL3 renderer's skinning shader
    // (platform/character_skin.cpp) and the CPU skinning before it (characters::skinVertices(),
    // docs/research/characters.md#character-geometry): each weighted bone's placement of the position and turn of the
    // normal times the weight, summed with the weights as stored (a hair below 1 here), the normal made unit. A
    // synthetic mesh of three vertices under three bones: a turn, a move and a scaled turn.
    const float c = std::cos(0.6F);
    const float s = std::sin(0.6F);
    const std::array<coney::anim::Mat34, 3> matrices{
        coney::anim::Mat34{{c, s, 0.0F}, {-s, c, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.1F, 0.2F, 0.3F}},
        coney::anim::Mat34{{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {-2.0F, 1.0F, 4.0F}},
        coney::anim::Mat34{{0.0F, 0.0F, -1.2F}, {0.0F, 1.2F, 0.0F}, {1.2F, 0.0F, 0.0F}, {0.5F, -0.5F, 0.25F}}};
    std::vector<coney::characters::SkinVertex> vertices(3);
    vertices[0] = {.position = {1.0F, 0.0F, 0.0F},
                   .normal = {0.0F, 1.0F, 0.0F},
                   .bones = {0, 0, 0, 0},
                   .weights = {1.0F, 0.0F, 0.0F, 0.0F}};
    vertices[1] = {.position = {0.3F, 1.7F, -0.4F},
                   .normal = {0.6F, 0.0F, 0.8F},
                   .bones = {0, 1, 2, 0},
                   .weights = {0.25F, 0.5F, 0.2499F, 0.0F}};
    vertices[2] = {.position = {-1.0F, 0.5F, 2.0F},
                   .normal = {0.0F, 0.0F, -1.0F},
                   .bones = {2, 1, 0, 1},
                   .weights = {0.4F, 0.3F, 0.2F, 0.0999F}};
    std::vector<coney::anim::Vec3> positions(vertices.size());
    std::vector<coney::anim::Vec3> normals(vertices.size());
    coney::characters::skinVertices(vertices, matrices, positions, normals);

    std::vector<Transform3d> bones;
    bones.reserve(matrices.size());
    for (const coney::anim::Mat34& m : matrices) {
        bones.push_back(Transform3d{.right = {m.x.x, m.x.y, m.x.z},
                                    .up = {m.y.x, m.y.y, m.y.z},
                                    .at = {m.z.x, m.z.y, m.z.z},
                                    .position = {m.t.x, m.t.y, m.t.z}});
    }
    const std::vector<coney::graphics::Matrix4> palette = coney::graphics::bonePaletteOf(bones);
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        MeshVertex vertex;
        vertex.position = {vertices[i].position.x, vertices[i].position.y, vertices[i].position.z};
        vertex.normal = {vertices[i].normal.x, vertices[i].normal.y, vertices[i].normal.z};
        vertex.bones = vertices[i].bones;
        vertex.weights = vertices[i].weights;
        const coney::graphics::SkinnedVertex got = coney::graphics::skinVertex(vertex, palette);
        CHECK(got.position[0] == Approx(positions[i].x).margin(1e-5));
        CHECK(got.position[1] == Approx(positions[i].y).margin(1e-5));
        CHECK(got.position[2] == Approx(positions[i].z).margin(1e-5));
        CHECK(got.normal[0] == Approx(normals[i].x).margin(1e-5));
        CHECK(got.normal[1] == Approx(normals[i].y).margin(1e-5));
        CHECK(got.normal[2] == Approx(normals[i].z).margin(1e-5));
    }
    // A slot with no weight names a bone the palette need not hold.
    MeshVertex outside;
    outside.position = {1.0F, 0.0F, 0.0F};
    outside.normal = {0.0F, 1.0F, 0.0F};
    outside.bones = {1, 200, 0, 0};
    outside.weights = {1.0F, 0.0F, 0.0F, 0.0F};
    const coney::graphics::SkinnedVertex moved = coney::graphics::skinVertex(outside, palette);
    CHECK(moved.position == std::array<float, 3>{-1.0F, 1.0F, 4.0F});
}

TEST_CASE("a human's blood layer takes his colours, blended with no alpha test",
          "[graphics][gpu-renderer][scene][skinning]") {
    // Expected: ../engine/gpu-renderer.md#skinning: the GL3 renderer's blood pass (Gl3Renderer::drawHuman(), GS context
    // 2 of docs/research/rendering.md#characters): the blood atomic's materials changed as the body's are (his colour
    // times the shade with the fade as alpha), blended by source alpha over what is below with the alpha test off,
    // its depth test, depth write and culling the draw's. Any other dual layer keeps the base's alpha and the draw's
    // states (layerColoursOf()).
    const MeshData layer =
        litMesh({MeshMaterial{Rgba{0x96, 0x96, 0x96, 255}, ""}, MeshMaterial{Rgba{10, 20, 30, 255}, ""}});
    coney::graphics::Draw3d draw;
    draw.material = Material3d{.colour = MaterialColour::Shade, .fade = 0x80, .shade = 0.5F};
    draw.states.alphaBlend = false;
    draw.states.alphaTest = AlphaTest::GreaterEqual;
    draw.states.depthWrite = true;
    draw.states.cull = coney::graphics::CullFaces::Front;
    const DrawColours base = coney::graphics::coloursOf(layer, draw.material);

    draw.dualPass = coney::graphics::DualPass::Blood;
    const DrawColours blood = coney::graphics::dualColoursOf(draw, layer, base);
    REQUIRE(blood.colours.size() == 2);
    CHECK(blood.colours[0] == Rgba{127, 127, 127, 0x80});
    CHECK(blood.colours[1] == Rgba{10, 20, 30, 255});
    const SceneStates bloodStates = coney::graphics::dualStatesOf(draw);
    CHECK(bloodStates.alphaBlend);
    CHECK(bloodStates.alphaTest == AlphaTest::Always);
    CHECK(bloodStates.depthTest);
    CHECK(bloodStates.depthWrite);
    CHECK(bloodStates.cull == coney::graphics::CullFaces::Front);

    draw.dualPass = coney::graphics::DualPass::Layer;
    const DrawColours over = coney::graphics::dualColoursOf(draw, layer, base);
    CHECK(over.colours[0] == Rgba{0x96, 0x96, 0x96, 0x80});
    CHECK(over.colours[1] == Rgba{10, 20, 30, 0x80});
    CHECK(coney::graphics::dualStatesOf(draw) == draw.states);
}

TEST_CASE("a posed mesh takes the lights that meet its posed sphere", "[graphics][gpu-renderer][scene][skinning]") {
    // Expected: ../engine/gpu-renderer.md#skinning: librw's GL3 device tests a point light against the atomic's world
    // sphere (world.cpp's enumerateLights), which for a posed human is the sphere his pose gave his atomic
    // (CharacterMesh::pose()), carried by his draw (Draw3d::bounds); a draw without one takes the mesh's own sphere.
    const MeshData mesh = litMesh(); // radius 1 round its origin
    coney::graphics::Draw3d draw;
    CHECK(coney::graphics::boundsOf(draw, mesh) == mesh.bounds);
    draw.bounds = coney::graphics::MeshBounds{{0.0F, 3.0F, 0.0F}, 1.5F};
    CHECK(coney::graphics::boundsOf(draw, mesh) == coney::graphics::MeshBounds{{0.0F, 3.0F, 0.0F}, 1.5F});
    // A point light of radius 1 at (10, 5, 0) by a mesh placed at (10, 0, 0): 5 from the mesh's own sphere's centre,
    // beyond its reach (1 + 1); 2 from the posed sphere's, within 1.5 + 1.
    const std::array<SceneLight, 1> lights{pointLight({10.0F, 5.0F, 0.0F}, 1.0F, 0.5F)};
    const Transform3d where{.position = {10.0F, 0.0F, 0.0F}};
    CHECK(coney::graphics::lightsFor(lights, mesh, where).count == 0);
    const DrawLights posed = coney::graphics::lightsFor(lights, mesh, where, coney::graphics::boundsOf(draw, mesh));
    REQUIRE(posed.count == 1);
    CHECK(posed.lights[0].type == LightType::Point);
}

TEST_CASE("a sub-mesh has vertex alpha only where its prelighting is not opaque", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: librw's instColor over the mesh's vertices from its least index to
    // its most, for a prelit geometry only.
    MeshData mesh = litMesh();
    mesh.vertices.resize(6);
    mesh.indices = {0, 1, 2, 3, 4, 5};
    mesh.subMeshes = {SubMesh{0, 3, 0}, SubMesh{3, 3, 0}};
    mesh.vertices[4].prelit = Rgba{255, 255, 255, 254};
    CHECK_FALSE(coney::graphics::vertexAlphaOf(mesh, mesh.subMeshes[1])); // not prelit
    mesh.attributes.prelit = true;
    CHECK_FALSE(coney::graphics::vertexAlphaOf(mesh, mesh.subMeshes[0]));
    CHECK(coney::graphics::vertexAlphaOf(mesh, mesh.subMeshes[1]));
}

TEST_CASE("a texture has alpha when a texel of its level 0 is not opaque", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: librw's Image::hasAlpha on the image the GL3 raster is made from.
    TextureData texture;
    texture.levels.push_back({2, 1, {1, 2, 3, 255, 4, 5, 6, 255}});
    texture.levels.push_back({1, 1, {0, 0, 0, 0}});
    CHECK_FALSE(coney::graphics::hasAlpha(texture));
    texture.levels[0].texels[7] = 254;
    CHECK(coney::graphics::hasAlpha(texture));
}

TEST_CASE("a sub-mesh is drawn in the passes librw's GL3 device draws it in", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: blending and the alpha test only for a translucent mesh, the GS
    // test's two passes at 128 of 255 (drawInst_GSemu), librw's start reference of 10 of 255 otherwise.
    const SceneStates world; // depth written, blended by alpha, no alpha test
    const AlphaRange every;
    CHECK(coney::graphics::drawPassesOf(world, false, false) == std::vector<SceneDrawPass>{{false, every, true}});
    CHECK(coney::graphics::drawPassesOf(world, true, false) == std::vector<SceneDrawPass>{{true, every, true}});

    SceneStates first = world;
    first.alphaTest = AlphaTest::GreaterEqual;
    const float gs = coney::graphics::kGsAlphaRef;
    CHECK(coney::graphics::drawPassesOf(first, true, false) ==
          std::vector<SceneDrawPass>{{true, AlphaRange{gs, 1000.0F}, true}, {true, AlphaRange{-1000.0F, gs}, false}});
    CHECK(coney::graphics::drawPassesOf(first, false, false) == std::vector<SceneDrawPass>{{false, every, true}});
    CHECK(coney::graphics::drawPassesOf(first, true, true) == std::vector<SceneDrawPass>{{true, every, true}});
    SceneStates noWrite = first;
    noWrite.depthWrite = false;
    CHECK(coney::graphics::drawPassesOf(noWrite, true, false) == std::vector<SceneDrawPass>{{true, every, false}});

    SceneStates less = world;
    less.alphaTest = AlphaTest::Less;
    less.alphaBlend = false;
    const float start = coney::graphics::kStartAlphaRef;
    CHECK(coney::graphics::drawPassesOf(less, true, false) ==
          std::vector<SceneDrawPass>{{false, AlphaRange{-1000.0F, start}, true}});
}

TEST_CASE("a texture's level is picked by distance as the GL3 renderer picks it", "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: texture_lod.cpp's u_coneyLod (K, 2^L, by distance, not from the
    // disc) and its level max(log2(max(depth, 1/1024)) x 2^L + K, 0), research: rendering.md#world.
    TextureData disc;
    disc.lod = coney::graphics::TextureLod{.k = -4.0F, .l = 1};
    const auto levels = coney::graphics::textureLevelsOf(&disc, true);
    CHECK(levels.k == Approx(-4.0F));
    CHECK(levels.scale == Approx(2.0F));
    CHECK(levels.byDistance);
    CHECK_FALSE(levels.gpuChooses);
    CHECK(coney::graphics::textureLevelAt(levels, 8.0F) == Approx(2.0F)); // 3 x 2 - 4
    CHECK(coney::graphics::textureLevelAt(levels, 2.0F) == Approx(0.0F)); // -2, magnified
    CHECK(coney::graphics::textureLevelAt(coney::graphics::textureLevelsOf(&disc, false), 8.0F) == Approx(0.0F));
    CHECK(coney::graphics::textureLevelsOf(nullptr, true).gpuChooses);
    CHECK(coney::graphics::textureLevelsOf(&disc, false).byDistance == false);
    TextureData own; // Coney's own: no GS level of detail
    CHECK(coney::graphics::textureLevelsOf(&own, true).gpuChooses);
}

TEST_CASE("the scene's fog is librw's linear fog from the fog plane to the far plane",
          "[graphics][gpu-renderer][scene]") {
    // Expected: ../engine/gpu-renderer.md#scene: DoFog, clamp((w - far) / (fog plane - far), 0 or 1 without fog, 1).
    const SceneCamera camera = testCamera(); // fog from 50 to 100
    CHECK(coney::graphics::fogAt(camera, true, 10.0F) == Approx(1.0F));
    CHECK(coney::graphics::fogAt(camera, true, 75.0F) == Approx(0.5F));
    CHECK(coney::graphics::fogAt(camera, true, 150.0F) == Approx(0.0F));
    CHECK(coney::graphics::fogAt(camera, false, 150.0F) == Approx(1.0F));
}
