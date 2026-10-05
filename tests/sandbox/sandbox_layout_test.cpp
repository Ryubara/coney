// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_layout.h"

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "support/fixtures.h"

using Catch::Approx;
using coney::sandbox::parseSandboxLayout;
using coney::sandbox::Shape;

namespace {

// The error message parsing `text` gives; the parse must fail.
std::string errorOf(std::string_view text) {
    auto layout = parseSandboxLayout(text);
    REQUIRE(!layout.has_value());
    CHECK(layout.error().code == coney::ErrorCode::Invalid);
    return layout.error().message;
}

// Writes `text` as a file in `dir`.
void writeText(const coney::test::TempDir& dir, std::string_view name, std::string_view text) {
    coney::test::Bytes bytes;
    bytes.text(text);
    dir.write(name, bytes.span());
}

} // namespace

TEST_CASE("a layout reads every statement: settings, textures, places and each shape", "[sandbox][layout]") {
    // A small synthetic layout using every statement once.
    const auto layout = parseSandboxLayout(R"(# a test layout
title   Test course   # trailing comment
texture floor floor.png
texture wall wall.png
sky colour=0.5,0.6,0.7
sun direction=-1,0,-1 colour=0.9,0.8,0.7
ambient sky=0.4,0.4,0.5 ground=0.2,0.2,0.1
fog start=50 end=150
occlusion radius=2 strength=0.5
shadows off
tessellate edge=0.5
spawn start at=1,2,0 heading=90
view overview at=0,-20,10 yaw=10 pitch=-30

box at=0,0,-0.5 size=100,100,0.5 texture=floor uv=0.5
ramp at=5,0,0 width=3 height=1 angle=45 yaw=90 texture=wall tint=1,0.5,0.5
ramp at=10,0,0 width=3 height=1 length=4
stairs at=-5,0,0 width=2 steps=4 rise=0.25 run=0.3 flags=0x10 material=35 area=9
cylinder at=0,8,0 radius=0.5 height=3 segments=12 texture=none
sphere at=3,8,0 radius=1
capsule at=6,8,0 radius=0.4 height=1.8 solid=no
)");
    REQUIRE(layout.has_value());
    CHECK(layout->title == "Test course");
    REQUIRE(layout->textures.size() == 2);
    CHECK(layout->textures[1].file == "wall.png");
    CHECK(layout->lighting.sky == coney::sandbox::Colour{0.5F, 0.6F, 0.7F});
    CHECK(layout->lighting.sunDirection.x == -1.0F);
    CHECK(layout->lighting.groundAmbient.b == Approx(0.1F));
    CHECK(layout->lighting.fogEnd == 150.0F);
    CHECK(layout->lighting.occlusionRadius == 2.0F);
    CHECK_FALSE(layout->lighting.shadows);
    CHECK(layout->tessellation == 0.5F);
    REQUIRE(layout->spawns.size() == 1);
    CHECK(layout->spawns[0].headingDegrees == 90.0F);
    REQUIRE(layout->views.size() == 1);
    CHECK(layout->views[0].pitchDegrees == -30.0F);

    REQUIRE(layout->primitives.size() == 7);
    const auto& floor = layout->primitives[0];
    CHECK(floor.shape == Shape::Box);
    CHECK(floor.line == 15);
    CHECK(floor.size.x == 100.0F);
    CHECK(floor.uvScale == 0.5F);
    CHECK(floor.texture == 0U);
    // A ramp at 45° is as long as it is high; a given length is kept.
    CHECK(layout->primitives[1].size.y == Approx(1.0F));
    CHECK(layout->primitives[1].texture == 1U);
    CHECK(layout->primitives[1].yawDegrees == 90.0F);
    CHECK(layout->primitives[1].tint.g == 0.5F);
    CHECK(layout->primitives[2].size.y == 4.0F);
    // Stairs: the footprint and height follow the steps; the collision tags are kept.
    const auto& stairs = layout->primitives[3];
    CHECK(stairs.steps == 4);
    CHECK(stairs.size.y == Approx(1.2F));
    CHECK(stairs.size.z == Approx(1.0F));
    CHECK(stairs.surface.flags == 0x10);
    CHECK(stairs.surface.material == 35);
    CHECK(stairs.surface.area == 9);
    CHECK(stairs.texture == 0U); // the first texture when none is named
    CHECK_FALSE(layout->primitives[4].texture.has_value());
    CHECK(layout->primitives[4].segments == 12);
    CHECK(layout->primitives[5].size.z == 2.0F);
    CHECK(layout->primitives[5].segments == 24);
    CHECK_FALSE(layout->primitives[6].surface.solid);
}

TEST_CASE("a layout without spawns or views gets one of each", "[sandbox][layout]") {
    const auto layout = parseSandboxLayout("box at=0,0,0 size=1,1,1\n");
    REQUIRE(layout.has_value());
    REQUIRE(layout->spawns.size() == 1);
    CHECK(layout->spawns[0].position.z == 0.0F);
    REQUIRE(layout->views.size() == 1);
    // Behind the spawn (which faces +y) and above it, looking down a little.
    CHECK(layout->views[0].position.y == Approx(-6.0F));
    CHECK(layout->views[0].position.z == Approx(3.0F));
    CHECK(layout->views[0].pitchDegrees < 0.0F);
    CHECK_FALSE(layout->primitives[0].texture.has_value()); // no textures declared: untextured
}

TEST_CASE("repeat places copies a step apart", "[sandbox][layout]") {
    const auto layout = parseSandboxLayout("box at=1,0,0 size=0.1,0.1,0.1 repeat=5 step=1,0,0.5\n");
    REQUIRE(layout.has_value());
    REQUIRE(layout->primitives.size() == 5);
    CHECK(layout->primitives[4].base.x == 5.0F);
    CHECK(layout->primitives[4].base.z == 2.0F);
    CHECK(layout->primitives[4].line == 1);
}

TEST_CASE("layout errors name the line and what is wrong", "[sandbox][layout]") {
    CHECK(errorOf("\n\nwidget at=0,0,0\n") == "line 3: unknown statement \"widget\"");
    CHECK(errorOf("box size=1,1,1\n") == "line 1: at= is missing");
    CHECK(errorOf("box at=0,0,0 size=1,1,1 colour=red\n") == "line 1: unknown argument colour=");
    CHECK(errorOf("box at=0,0,0 at=1,1,1 size=1,1,1\n") == "line 1: at given twice");
    CHECK(errorOf("box at=0,0 size=1,1,1\n").starts_with("line 1: at= needs three numbers"));
    CHECK(errorOf("box at=0,0,0 size=1,-1,1\n").starts_with("line 1: size= needs three numbers from"));
    CHECK(errorOf("box at=0,0,0 size=1,1,1 bare\n") == "line 1: \"bare\" is not key=value");
    CHECK(errorOf("box at=0,0,0 size=1,1,1 texture=brick\n").starts_with("line 1: no texture called brick"));
    CHECK(errorOf("ramp at=0,0,0 width=1 height=1\n").starts_with("line 1: a ramp needs one of angle="));
    CHECK(errorOf("ramp at=0,0,0 width=1 height=1 angle=30 length=2\n").starts_with("line 1: a ramp needs one"));
    CHECK(errorOf("ramp at=0,0,0 width=1 height=1 angle=90\n").starts_with("line 1: angle= needs a number"));
    CHECK(errorOf("stairs at=0,0,0 width=1 steps=0 rise=0.2 run=0.3\n").starts_with("line 1: steps= needs a whole"));
    CHECK(errorOf("capsule at=0,0,0 radius=1 height=1\n").starts_with("line 1: a capsule's height="));
    CHECK(errorOf("box at=0,0,0 size=1,1,1 repeat=3\n") == "line 1: step= is missing");
    CHECK(errorOf("box at=0,0,0 size=1,1,1 solid=maybe\n") == "line 1: solid= needs yes or no, got \"maybe\"");
    CHECK(errorOf("box at=0,0,0 size=1,1,1 flags=0x10000\n").starts_with("line 1: flags= needs a whole number"));
    CHECK(errorOf("fog start=10 end=5\n") == "line 1: the fog's end= must be beyond its start=");
    CHECK(errorOf("sky colour=1,1,1\nsky colour=0,0,0\n") == "line 2: sky given twice");
    CHECK(errorOf("texture a a.png\ntexture a b.png\n") == "line 2: texture a given twice");
    CHECK(errorOf("texture a\n").starts_with("line 1: texture needs a name and a file"));
    CHECK(errorOf("spawn at=0,0,0\n").starts_with("line 1: spawn needs a name first"));
    CHECK(errorOf("spawn a at=0,0,0\nspawn a at=1,0,0\n") == "line 2: spawn a given twice");
    CHECK(errorOf("shadows maybe\n") == "line 1: shadows needs on or off");
    CHECK(errorOf("sun direction=0,0,0\n") == "line 1: the sun's direction= cannot be 0,0,0");
    CHECK(errorOf("box at=0,0,0 size=1,1,1 repeat=1000 step=1,0,0\nbox at=0,0,0 size=1,1,1 repeat=1000 "
                  "step=1,0,0\nbox at=0,0,0 size=1,1,1 repeat=1000 step=1,0,0\nbox at=0,0,0 size=1,1,1 repeat=1000 "
                  "step=1,0,0\nbox at=0,0,0 size=1,1,1 repeat=1000 step=1,0,0\n")
              .starts_with("line 5: a layout holds at most"));
}

TEST_CASE("layouts are listed and found by name in their folder", "[sandbox][layout]") {
    const coney::test::TempDir dir;
    writeText(dir, "default.layout", "box at=0,0,0 size=1,1,1\n");
    writeText(dir, "parkour.layout", "box at=0,0,0 size=2,2,2\n");
    writeText(dir, "notes.txt", "not a layout");
    CHECK(coney::sandbox::listSandboxLayouts(dir.path()) == std::vector<std::string>{"default", "parkour"});
    CHECK(coney::sandbox::listSandboxLayouts(dir.path() / "missing").empty());

    const auto found = coney::sandbox::findSandboxLayout(dir.path(), "parkour");
    REQUIRE(found.has_value());
    CHECK(found->filename() == "parkour.layout");
    // A path to a file works as well as a name.
    const auto byPath =
        coney::sandbox::findSandboxLayout(dir.path() / "missing", (dir.path() / "default.layout").string());
    REQUIRE(byPath.has_value());
    const auto layout = coney::sandbox::loadSandboxLayout(*found);
    REQUIRE(layout.has_value());
    CHECK(layout->primitives[0].size.x == 2.0F);

    const auto missing = coney::sandbox::findSandboxLayout(dir.path(), "maze");
    REQUIRE(!missing.has_value());
    CHECK(missing.error().code == coney::ErrorCode::NotFound);
    CHECK(missing.error().message.find("default, parkour") != std::string::npos);
}

TEST_CASE("loading a layout file reports the file with a parse error", "[sandbox][layout]") {
    const coney::test::TempDir dir;
    writeText(dir, "broken.layout", "box at=0,0,0\n");
    const auto layout = coney::sandbox::loadSandboxLayout(dir.path() / "broken.layout");
    REQUIRE(!layout.has_value());
    CHECK(layout.error().message.ends_with("broken.layout: line 1: size= is missing"));
    CHECK(coney::sandbox::loadSandboxLayout(dir.path() / "none.layout").error().code == coney::ErrorCode::NotFound);
}
