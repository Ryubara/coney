// SPDX-License-Identifier: GPL-3.0-or-later
// A sandbox layout opened in Coney Studio: one entity per placed thing, in the Hierarchy's order, and the templates a
// new project starts from (docs/guides/studio/editor-experience.md#level-format-needs, #new-level-coney).
#include "studio/level_document.h"

#include <cstddef>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sandbox/sandbox_layout.h"
#include "studio/level_templates.h"
#include "support/test_paths.h"

using Catch::Approx;
using coney::studio::EntityKind;
using coney::studio::LevelDocument;

namespace {

// A layout with one of every placed kind: two boxes from a repeat, a ramp, a spawn, a target, a fighter and a view.
constexpr std::string_view kLayout = "title Yard\n"
                                     "box at=0,0,0 size=10,10,1 repeat=2 step=20,0,0\n"
                                     "ramp at=0,10,0 width=4 height=2 angle=20\n"
                                     "spawn start at=1,2,0 heading=90\n"
                                     "target dummy at=3,4,0\n"
                                     "fighter brawler at=5,6,0\n"
                                     "view high at=0,-10,8 yaw=0 pitch=-30\n";

// The document of kLayout.
LevelDocument yard() {
    auto layout = coney::sandbox::parseSandboxLayout(kLayout);
    REQUIRE(layout.has_value());
    return LevelDocument::fromLayout(std::move(*layout), "yard.layout");
}

} // namespace

TEST_CASE("a layout opens as one entity per placed thing, primitives first, ids in order", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#level-format-needs, a layout as a flat list of entities
    // (design).
    const LevelDocument level = yard();
    const auto& entities = level.entities();
    REQUIRE(entities.size() == 7);
    for (std::size_t i = 0; i < entities.size(); ++i) {
        CHECK(entities[i].id == i);
    }
    CHECK(entities[0].kind == EntityKind::Primitive);
    CHECK(entities[0].name == "Box 1");
    CHECK(entities[1].name == "Box 2"); // the repeat's copy
    CHECK(entities[1].position.x == Approx(20.0F));
    CHECK(entities[2].name == "Ramp 1");
    CHECK(entities[3].kind == EntityKind::Spawn);
    CHECK(entities[3].name == "start");
    CHECK(entities[4].kind == EntityKind::Target);
    CHECK(entities[5].kind == EntityKind::Fighter);
    CHECK(entities[6].kind == EntityKind::View);
    CHECK(entities[6].name == "high");
    CHECK(level.title() == "Yard");
    CHECK(level.entityOfPrimitive(2) == 2U);
    CHECK_FALSE(level.entityOfPrimitive(3).has_value());
    CHECK(level.entity(7) == nullptr);
}

TEST_CASE("an entity's bounds are its primitive's triangles or a human-sized box", "[studio]") {
    const LevelDocument level = yard();
    // Expected: the box is 10 x 10 x 1 m round its base point at the origin (docs/guides/sandbox.md#shapes).
    const auto box = level.bounds(level.entities()[0]);
    CHECK(box.min.x == Approx(-5.0F));
    CHECK(box.max.y == Approx(5.0F));
    CHECK(box.max.z == Approx(1.0F));
    // A spawn point: Coney's choice of a 0.6 m by 1.8 m box round the feet, about a human's size.
    const auto spawn = level.bounds(level.entities()[3]);
    CHECK(spawn.min.x == Approx(0.7F));
    CHECK(spawn.max.z == Approx(1.8F));
    CHECK(level.pickMesh().triangles.size() > 0);
}

TEST_CASE("every shipped sandbox layout opens as a level", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#level-format-needs, a layout as a flat list of entities
    // (design).
    for (const char* name : {"default", "combat", "parkour"}) {
        const auto level = LevelDocument::load(coney::test::assetsDir() / "sandbox" / (std::string(name) + ".layout"));
        REQUIRE(level.has_value());
        CHECK(level->entities().size() > 10);
    }
}

TEST_CASE("the Basic template parses and has a spawn point and a view", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#new-level-coney, the design: a 60 m ground, a spawn point at
    // its middle and a camera view behind it.
    const auto layout = coney::sandbox::parseSandboxLayout(coney::studio::basicLevelText());
    REQUIRE(layout.has_value());
    REQUIRE(layout->primitives.size() == 1);
    CHECK(layout->primitives[0].size.x == Approx(60.0F));
    REQUIRE(layout->spawns.size() == 1);
    CHECK(layout->spawns[0].name == "start");
    CHECK(layout->views.size() == 1);
    CHECK(layout->textures.at(0).file == "../textures/dark_grid.png");
}

TEST_CASE("a shipped layout copied into a project finds its textures in textures/", "[studio]") {
    // Expected: docs/guides/studio/editor-experience.md#level-format-needs, a layout as a flat list of entities
    // (design).
    const std::string text = coney::studio::rehomeTextures("# floor\ntexture floor dark_grid.png\n"
                                                           "texture  wall   light_grid.png  # walls\n"
                                                           "box at=0,0,0 size=1,1,1 texture=floor\n",
                                                           "../textures/");
    CHECK(text == "# floor\ntexture floor ../textures/dark_grid.png\n"
                  "texture wall ../textures/light_grid.png  # walls\n"
                  "box at=0,0,0 size=1,1,1 texture=floor\n");
    const auto templates = coney::studio::levelTemplates();
    REQUIRE(templates.size() == 4);
    CHECK(templates[0] == "basic");
}
