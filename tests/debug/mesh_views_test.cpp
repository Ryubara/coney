// SPDX-License-Identifier: GPL-3.0-or-later
// The mesh views and the Entity inspector's Mesh group (docs/guides/debug-menu.md#mesh-views): the statistics of a
// synthetic character model, the switches' state and the plans they give each model, the colours, the normals and
// bone axes, `--mesh-view`'s value and the Performance page's Scene line. No game, no disc, no window.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/character_model.h"
#include "debug/debug_session.h"
#include "debug/entity_inspect.h"
#include "debug/menu_model.h"
#include "debug/mesh_stats.h"
#include "debug/mesh_views.h"
#include "debug/play_controls.h"

using Catch::Approx;
using coney::anim::Mat34;
using coney::anim::Vec3;
using coney::debug::MeshDrawn;
using coney::debug::MeshDrawPlan;
using coney::debug::MeshKind;
using coney::debug::MeshScope;
using coney::debug::MeshShading;
using coney::debug::MeshStats;
using coney::debug::MeshViewOptions;

namespace {

// A vertex at `x` weighted to `weights` (bones 0, 1, 2, 3 in turn).
coney::characters::SkinVertex vertexAt(float x, std::array<float, 4> weights) {
    coney::characters::SkinVertex vertex;
    vertex.position = Vec3{x, 0.0F, 0.0F};
    vertex.normal = Vec3{0.0F, 0.0F, 1.0F};
    vertex.bones = {0, 1, 2, 3};
    vertex.weights = weights;
    return vertex;
}

// A synthetic character model, built here and not read from any file: three materials (the first with no triangles),
// six vertices carrying one to four weights, four triangles (one on material 1, three on material 2), a hierarchy of
// five nodes whose skin gives five inverse matrices and whose vertices use four of them.
coney::characters::CharacterModel syntheticModel() {
    coney::characters::CharacterModel model;
    model.clump.materials = {
        coney::characters::ClumpMaterial{.colour = {255, 0, 0, 255}, .texture = "skin", .textureFilter = 0},
        coney::characters::ClumpMaterial{.colour = {0, 255, 0, 255}, .texture = "skin", .textureFilter = 0},
        coney::characters::ClumpMaterial{.colour = {0, 0, 255, 128}, .texture = "skin", .textureFilter = 0}};
    model.clump.hierarchy.resize(5);
    model.clump.skin.boneCount = 5;
    model.clump.skin.maxWeights = 4;
    model.clump.skin.usedBones = {0, 1, 2, 3};
    model.vertices = {vertexAt(0.0F, {1.0F, 0.0F, 0.0F, 0.0F}),     vertexAt(1.0F, {0.5F, 0.5F, 0.0F, 0.0F}),
                      vertexAt(2.0F, {0.5F, 0.5F, 0.0F, 0.0F}),     vertexAt(3.0F, {0.4F, 0.3F, 0.3F, 0.0F}),
                      vertexAt(4.0F, {0.25F, 0.25F, 0.25F, 0.25F}), vertexAt(5.0F, {1.0F, 0.0F, 0.0F, 0.0F})};
    model.triangles = {coney::characters::ModelTriangle{.vertices = {0, 1, 2}, .material = 1},
                       coney::characters::ModelTriangle{.vertices = {1, 2, 3}, .material = 2},
                       coney::characters::ModelTriangle{.vertices = {2, 3, 4}, .material = 2},
                       coney::characters::ModelTriangle{.vertices = {3, 4, 5}, .material = 2}};
    return model;
}

// The value of `name` in the Mesh group `group`, or "missing".
std::string meshValue(const coney::debug::WatchGroup& group, std::string_view name) {
    const std::vector<coney::debug::WatchGroup> groups{group};
    return coney::debug::watchValue(groups, "Mesh", name).value_or("missing");
}

// Whether two points are within a millimetre.
bool near(Vec3 a, Vec3 b) { return coney::anim::distance(a, b) < 1e-3F; }

} // namespace

TEST_CASE("the mesh statistics count a synthetic model as the play mode draws it", "[debug][mesh]") {
    // Expected: unverified, #210 (Coney's own renderer: one librw atomic, one mesh and draw per material)
    const MeshStats stats =
        coney::debug::characterMeshStats(syntheticModel(), {.name = "skin", .width = 128, .height = 64});
    CHECK(stats.atomics == 1);
    CHECK(stats.geometries == 1);
    CHECK(stats.triangles == 4);
    CHECK(stats.vertices == 6);
    REQUIRE(stats.materials.size() == 3);
    CHECK(stats.materials[0].triangles == 0);
    CHECK(stats.materials[1].triangles == 1);
    CHECK(stats.materials[2].triangles == 3);
    CHECK(stats.materials[2].colour == std::array<std::uint8_t, 4>{0, 0, 255, 128});
    CHECK(stats.materials[1].texture == "skin");
    CHECK(stats.materials[1].textureWidth == 128);
    CHECK(stats.materials[1].textureHeight == 64);
    CHECK(stats.textures == 1);
    CHECK(stats.hierarchyBones == 5);
    CHECK(stats.skinBones == 5);
    CHECK(stats.usedBones == 4);
    CHECK(stats.declaredWeights == 4);
    CHECK(stats.verticesByWeights == std::array<std::size_t, 5>{0, 2, 2, 1, 1});
    CHECK(coney::debug::meanWeights(stats) == Approx(13.0 / 6.0));
    // An empty material still costs librw a draw (a mesh with no indices).
    CHECK(stats.drawsPerPass == 3);
}

TEST_CASE("an untextured model counts no texture", "[debug][mesh]") {
    // Expected: unverified, #210
    const MeshStats stats = coney::debug::characterMeshStats(syntheticModel(), {});
    CHECK(stats.textures == 0);
    CHECK(stats.materials[0].texture.empty());
    CHECK(stats.materials[0].textureWidth == 0);
}

TEST_CASE("a model's draw calls are its materials times the passes drawn", "[debug][mesh]") {
    // Expected: unverified, #210 (the texture pass, then the blood layer: research/rendering.md#characters)
    const MeshStats stats =
        coney::debug::characterMeshStats(syntheticModel(), {.name = "skin", .width = 0, .height = 0});
    CHECK(coney::debug::drawCallsOf(stats, MeshDrawn{.passes = 1, .fade = 1.0F, .skinning = "CPU"}) == 3);
    CHECK(coney::debug::drawCallsOf(stats, MeshDrawn{.passes = 2, .fade = 1.0F, .skinning = "CPU"}) == 6);
    CHECK(coney::debug::drawCallsOf(stats, MeshDrawn{.passes = 0, .fade = 1.0F, .skinning = "CPU"}) == 0);
}

TEST_CASE("the Mesh group shows every number of the model", "[debug][mesh]") {
    // Expected: unverified, #210
    const MeshStats stats =
        coney::debug::characterMeshStats(syntheticModel(), {.name = "skin", .width = 128, .height = 64});
    const coney::debug::WatchGroup group =
        coney::debug::describeMesh(stats, MeshDrawn{.passes = 2, .fade = 1.0F, .skinning = "CPU"});
    CHECK(group.name == "Mesh");
    CHECK(meshValue(group, "Atomics") == "1");
    CHECK(meshValue(group, "Geometries") == "1");
    CHECK(meshValue(group, "Triangles") == "4");
    CHECK(meshValue(group, "Vertices") == "6");
    CHECK(meshValue(group, "Materials") == "3");
    CHECK(meshValue(group, "Material 2") == "3 triangles, colour (0, 0, 255, 128), texture skin 128x64");
    CHECK(meshValue(group, "Material 0") == "0 triangles, colour (255, 0, 0, 255), texture skin 128x64");
    CHECK(meshValue(group, "Textures") == "1");
    CHECK(meshValue(group, "Bones") == "5 in the hierarchy, 5 skinned, 4 used");
    CHECK(meshValue(group, "Weights per vertex") == "most 4 (skin allows 4), mean 2.17; 1: 2, 2: 2, 3: 1, 4: 1");
    CHECK(meshValue(group, "Draw calls last frame") == "6 (3 materials x 2 passes)");
    CHECK(meshValue(group, "LOD in use") == "0 of 1 (one model), fade 1.00");
    CHECK(meshValue(group, "Skinned on") == "CPU");
}

TEST_CASE("the Draw calls line reads in the singular after a count of one", "[debug][mesh]") {
    // Expected: unverified, #210 (a "1 materials" reads wrongly)
    MeshStats stats;
    stats.drawsPerPass = 1;
    CHECK(meshValue(coney::debug::describeMesh(stats, MeshDrawn{.passes = 1, .fade = 1.0F, .skinning = "CPU"}),
                    "Draw calls last frame") == "1 (1 material x 1 pass)");
    CHECK(meshValue(coney::debug::describeMesh(stats, MeshDrawn{.passes = 2, .fade = 1.0F, .skinning = "CPU"}),
                    "Draw calls last frame") == "2 (1 material x 2 passes)");
    stats.drawsPerPass = 3;
    CHECK(meshValue(coney::debug::describeMesh(stats, MeshDrawn{.passes = 1, .fade = 1.0F, .skinning = "CPU"}),
                    "Draw calls last frame") == "3 (3 materials x 1 pass)");
    CHECK(meshValue(coney::debug::describeMesh(stats, MeshDrawn{.passes = 0, .fade = 1.0F, .skinning = "CPU"}),
                    "Draw calls last frame") == "0 (3 materials x 0 passes)");
}

TEST_CASE("with every mesh view off each model gets the game's own draw alone", "[debug][mesh]") {
    // Expected: unverified, #210 (faithful first: the views change nothing while off)
    const coney::debug::DebugDrawOptions defaults;
    CHECK_FALSE(coney::debug::meshViewsOn(defaults.mesh));
    for (const MeshKind kind : {MeshKind::Scenery, MeshKind::Human, MeshKind::Object}) {
        for (const bool selected : {false, true}) {
            CHECK(coney::debug::meshPlanFor(defaults.mesh, kind, selected) == MeshDrawPlan{});
        }
    }
    // The view's scope alone, with the view shaded, is still off.
    MeshViewOptions scoped;
    scoped.viewScope = MeshScope::Selection;
    CHECK_FALSE(coney::debug::meshViewsOn(scoped));
    // The normals and bone axes are lines alone: no pass of their own over the models.
    MeshViewOptions lines;
    lines.normals = true;
    lines.boneAxes = true;
    CHECK(coney::debug::meshViewsOn(lines));
    CHECK_FALSE(coney::debug::meshPassesOn(lines));
    CHECK_FALSE(coney::debug::meshPassesOn(defaults.mesh));
}

TEST_CASE("the wireframe switch picks the selection, every human or everything", "[debug][mesh]") {
    // Expected: unverified, #210
    MeshViewOptions options;
    options.wireframe = MeshScope::Selection;
    CHECK(coney::debug::meshViewsOn(options));
    CHECK(coney::debug::meshPlanFor(options, MeshKind::Human, true).wire);
    CHECK_FALSE(coney::debug::meshPlanFor(options, MeshKind::Human, false).wire);
    options.wireframe = MeshScope::Humans;
    CHECK(coney::debug::meshPlanFor(options, MeshKind::Human, false).wire);
    CHECK(coney::debug::meshPlanFor(options, MeshKind::Object, true).wire);
    CHECK_FALSE(coney::debug::meshPlanFor(options, MeshKind::Object, false).wire);
    CHECK_FALSE(coney::debug::meshPlanFor(options, MeshKind::Scenery, false).wire);
    options.wireframe = MeshScope::Everything;
    const MeshDrawPlan scenery = coney::debug::meshPlanFor(options, MeshKind::Scenery, false);
    CHECK(scenery.wire);
    CHECK(scenery.shaded); // over the shaded scene
    CHECK_FALSE(scenery.flat);
    CHECK(scenery.wireColour == coney::debug::meshWireColour(MeshKind::Scenery, false));
    CHECK(coney::debug::meshPlanFor(options, MeshKind::Human, true).wireColour ==
          coney::debug::meshWireColour(MeshKind::Human, true));
}

TEST_CASE("the wireframe-only and flat views replace the shaded draw of the models in their scope", "[debug][mesh]") {
    // Expected: unverified, #210
    MeshViewOptions options;
    options.view = MeshShading::Wireframe;
    const MeshDrawPlan wireOnly = coney::debug::meshPlanFor(options, MeshKind::Object, false);
    CHECK_FALSE(wireOnly.shaded);
    CHECK_FALSE(wireOnly.flat);
    CHECK(wireOnly.wire);
    options.view = MeshShading::Material;
    options.viewScope = MeshScope::Humans;
    const MeshDrawPlan human = coney::debug::meshPlanFor(options, MeshKind::Human, false);
    CHECK_FALSE(human.shaded);
    CHECK(human.flat);
    CHECK(human.colouring == MeshShading::Material);
    CHECK_FALSE(human.wire);
    // Outside the view's scope the scenery stays as the game draws it.
    CHECK(coney::debug::meshPlanFor(options, MeshKind::Scenery, false) == MeshDrawPlan{});
    // The wireframe switch draws over a flat view too.
    options.wireframe = MeshScope::Everything;
    const MeshDrawPlan both = coney::debug::meshPlanFor(options, MeshKind::Human, false);
    CHECK(both.flat);
    CHECK(both.wire);
}

TEST_CASE("the flat views' colours are fixed by their key", "[debug][mesh]") {
    // Expected: unverified, #210 (Coney's own palette)
    std::set<std::uint32_t> colours;
    for (std::uint32_t key = 0; key < 12; ++key) {
        const coney::graphics::Rgba colour = coney::debug::meshPaletteColour(key);
        CHECK(colour.a == 255);
        colours.insert((std::uint32_t{colour.r} << 16U) | (std::uint32_t{colour.g} << 8U) | colour.b);
    }
    CHECK(colours.size() == 12);
    CHECK(coney::debug::meshPaletteColour(12) == coney::debug::meshPaletteColour(0));
    CHECK(coney::debug::meshMaterialKey("skin", 1) == coney::debug::meshMaterialKey("skin", 1));
    CHECK(coney::debug::meshMaterialKey("skin", 1) != coney::debug::meshMaterialKey("skin", 2));
    CHECK(coney::debug::meshMaterialKey("skin", 1) != coney::debug::meshMaterialKey("road", 1));
    CHECK(coney::debug::meshLodColour(1.0F) != coney::debug::meshLodColour(0.5F));
    CHECK(coney::debug::meshLodColour(0.2F) == coney::debug::meshLodColour(0.5F));
}

TEST_CASE("a character's placement leans, turns and moves it as the play mode draws it", "[debug][mesh]") {
    // Expected: unverified, #210 (the play mode's own placement, heading 0 faces +y)
    const Vec3 feet{10.0F, 20.0F, 1.0F};
    const Mat34 straight = coney::debug::characterPlacement(feet, 0.0F, 0.0F);
    CHECK(near(coney::anim::transformPoint(straight, Vec3{0.0F, 1.0F, 0.0F}), Vec3{10.0F, 21.0F, 1.0F}));
    const Mat34 turned = coney::debug::characterPlacement(feet, std::numbers::pi_v<float> / 2.0F, 0.0F);
    CHECK(near(coney::anim::transformPoint(turned, Vec3{0.0F, 1.0F, 0.0F}), Vec3{9.0F, 20.0F, 1.0F}));
    // A lean tips the head sideways about the forward axis, at the feet.
    const Mat34 leaned = coney::debug::characterPlacement(Vec3{}, 0.0F, std::numbers::pi_v<float> / 2.0F);
    const Vec3 head = coney::anim::transformPoint(leaned, Vec3{0.0F, 0.0F, 1.0F});
    CHECK(std::abs(head.z) < 1e-3F);
    CHECK(std::abs(head.x) == Approx(1.0F));
}

TEST_CASE("the normals and bone axes are lines from the posed model", "[debug][mesh]") {
    // Expected: unverified, #210
    coney::debug::DebugLines lines;
    const std::vector<Vec3> positions{{0.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const std::vector<Vec3> normals{{0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F}};
    const Mat34 placement = coney::debug::characterPlacement(Vec3{0.0F, 0.0F, 2.0F}, 0.0F, 0.0F);
    CHECK(coney::debug::addNormals(lines, positions, normals, placement, 0.5F) == 2);
    REQUIRE(lines.size() == 2);
    CHECK(near(lines.lines()[0].a, Vec3{0.0F, 0.0F, 2.0F}));
    CHECK(near(lines.lines()[0].b, Vec3{0.0F, 0.0F, 2.5F}));
    CHECK(near(lines.lines()[1].b, Vec3{1.5F, 0.0F, 2.0F}));
    CHECK(lines.lines()[0].colour == coney::debug::kNormalColour);

    // Three bones: the root's motion (left out), a bone at the origin and its child 1 m up.
    coney::debug::DebugLines bones;
    std::vector<Mat34> transforms(3);
    transforms[2].t = Vec3{0.0F, 0.0F, 1.0F};
    const std::vector<int> parents{-1, 0, 1};
    CHECK(coney::debug::addBoneAxes(bones, transforms, parents, Mat34{}, 0.1F) == 2);
    // Three axes for each bone drawn, and one line from bone 2 to its parent (bone 1's link to the root is left out).
    REQUIRE(bones.size() == 7);
    CHECK(bones.lines()[0].colour == coney::debug::kBoneAxisColours[0]);
    CHECK(near(bones.lines()[0].b, Vec3{0.1F, 0.0F, 0.0F}));
}

TEST_CASE("the mesh view option's value names the views, the selection and how long they stay on", "[debug][mesh]") {
    // Expected: unverified, #210 (docs/guides/running-options.md)
    const auto spec =
        coney::debug::parseMeshViewSpec("wire=all,view=material,scope=humans,normals,bones,select=nearest,"
                                        "frames=40");
    REQUIRE(spec.has_value());
    CHECK(spec->options.wireframe == MeshScope::Everything);
    CHECK(spec->options.view == MeshShading::Material);
    CHECK(spec->options.viewScope == MeshScope::Humans);
    CHECK(spec->options.normals);
    CHECK(spec->options.boneAxes);
    CHECK(spec->select == coney::debug::MeshViewSelect::Nearest);
    CHECK(spec->frames == 40);
    CHECK(coney::debug::parseMeshViewSpec("select=player")->select == coney::debug::MeshViewSelect::Player);
    CHECK(coney::debug::parseMeshViewSpec("normals")->select == coney::debug::MeshViewSelect::None);
    CHECK(coney::debug::parseMeshViewSpec("view=wire")->options.view == MeshShading::Wireframe);
    CHECK(coney::debug::parseMeshViewSpec("view=draw")->options.view == MeshShading::DrawCall);
    CHECK(coney::debug::parseMeshViewSpec("view=lod")->options.view == MeshShading::Lod);
    CHECK(coney::debug::parseMeshViewSpec("wire=selection")->options.wireframe == MeshScope::Selection);
    CHECK_FALSE(coney::debug::parseMeshViewSpec("wire=sometimes").has_value());
    CHECK_FALSE(coney::debug::parseMeshViewSpec("glow").has_value());
    CHECK_FALSE(coney::debug::parseMeshViewSpec("frames=0").has_value());
    CHECK_FALSE(coney::debug::parseMeshViewSpec("").has_value());
}

TEST_CASE("the Scene lines sum the scene counters of each kind", "[debug][mesh][perf]") {
    // Expected: unverified, #210 (docs/guides/performance.md#counters)
    const std::vector<coney::debug::NamedCount> counters{{.name = "gl.draws", .value = 999.0},
                                                         {.name = "scene.scenery.triangles", .value = 12345.0},
                                                         {.name = "scene.scenery.vertices", .value = 20100.0},
                                                         {.name = "scene.scenery.draws", .value = 310.0},
                                                         {.name = "scene.humans.triangles", .value = 800.0},
                                                         {.name = "scene.humans.vertices", .value = 2400.0},
                                                         {.name = "scene.humans.draws", .value = 6.0}};
    CHECK(coney::debug::sceneKindLine(counters, "scenery") == "12.3k tris 20.1k verts 310 draws");
    CHECK(coney::debug::sceneKindLine(counters, "humans") == "800 tris 2.4k verts 6 draws");
    // A kind with no counter shows a dash, and so does every kind with no counters at all.
    CHECK(coney::debug::sceneKindLine(counters, "effects") == "-");
    CHECK(coney::debug::sceneKindLine({}, "scenery") == "-");
}

TEST_CASE("the Debug draw page switches the mesh views", "[debug][mesh]") {
    // Expected: unverified, #210 (docs/guides/debug-menu.md#mesh-views)
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession session(tunables, coney::debug::DebugServices{}, nullptr);
    const auto page = session.model().openPage("Debug draw");
    REQUIRE(page != nullptr);
    const auto item = [&page](std::string_view label) -> const coney::debug::MenuItem& {
        const coney::debug::MenuItem* found = page->find(label);
        INFO(label);
        REQUIRE(found != nullptr);
        return *found;
    };
    CHECK(item("Model wireframe").choices == std::vector<std::string>{"off", "selection", "all humans", "everything"});
    CHECK(item("Mesh view").choices ==
          std::vector<std::string>{"shaded", "wireframe only", "by material", "by draw call", "by LOD"});
    item("Model wireframe").setChoice(2);
    item("Mesh view").setChoice(3);
    item("Mesh view on").setChoice(0);
    item("Normals").setBool(true);
    item("Bone axes").setBool(true);
    const MeshViewOptions& mesh = session.debugDraw().mesh;
    CHECK(mesh.wireframe == MeshScope::Humans);
    CHECK(mesh.view == MeshShading::DrawCall);
    CHECK(mesh.viewScope == MeshScope::Selection);
    CHECK(mesh.normals);
    CHECK(mesh.boneAxes);
    CHECK(item("Mesh view on").getChoice() == 0);
}
