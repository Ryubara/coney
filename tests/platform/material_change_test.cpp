// SPDX-License-Identifier: GPL-3.0-or-later
// MaterialChange (src/platform/lit_atomic.h), the one place a 3D draw's colour change is made, on librw's NULL device
// with no window or GPU. The geometry is built here by hand through librw's own calls; nothing comes from the game.

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "graphics/frame_description.h"
#include "platform/lit_atomic.h"
#include "platform/render_engine.h"

namespace {

// Whether two librw colours are the same, channel for channel.
bool sameColour(const rw::RGBA& a, const rw::RGBA& b) {
    return a.red == b.red && a.green == b.green && a.blue == b.blue && a.alpha == b.alpha;
}

// An atomic of one triangle whose geometry holds two materials of their own colours, as a model of several materials
// does: the first `first`, the second `second`. The caller destroys it, and its geometry goes with it.
rw::Atomic* twoMaterialAtomic(rw::RGBA first, rw::RGBA second) {
    rw::Geometry* geometry = rw::Geometry::create(3, 1, rw::Geometry::POSITIONS);
    rw::MorphTarget& target = geometry->morphTargets[0];
    target.vertices[0] = rw::makeV3d(0.0F, 0.0F, 0.0F);
    target.vertices[1] = rw::makeV3d(1.0F, 0.0F, 0.0F);
    target.vertices[2] = rw::makeV3d(0.0F, 1.0F, 0.0F);
    geometry->triangles[0].v[0] = 0;
    geometry->triangles[0].v[1] = 1;
    geometry->triangles[0].v[2] = 2;
    geometry->triangles[0].matId = 0;
    for (const rw::RGBA colour : {first, second}) {
        rw::Material* material = rw::Material::create();
        material->color = colour;
        geometry->matList.appendMaterial(material);
        material->destroy(); // the list holds its own reference
    }
    geometry->buildMeshes();
    rw::Atomic* atomic = rw::Atomic::create();
    atomic->setGeometry(geometry, 0);
    geometry->destroy(); // the atomic holds its own reference
    return atomic;
}

} // namespace

TEST_CASE("an object's tint replaces its first material's colour for the draw and leaves the others",
          "[platform][objects][frame-description]") {
    // Expected: docs/research/objects.md#tint, confirmed (code): ObjectRender_Draw (0x0017fd78) writes the tint word,
    // its alpha faded, as the colour of the geometry's first material (materials[0] +4), replacing it, and leaves the
    // other materials as they are.
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const rw::RGBA first = rw::makeRGBA(200, 100, 50, 255);
    const rw::RGBA second = rw::makeRGBA(10, 20, 30, 240);
    rw::Atomic* atomic = twoMaterialAtomic(first, second);
    const rw::Material* const* materials = atomic->geometry->matList.materials;
    {
        const coney::platform::MaterialChange change(
            *atomic, coney::graphics::Material3d{.colour = coney::graphics::MaterialColour::Tint,
                                                 .tint = coney::graphics::Rgba{0x80, 0xC0, 0xFF, 0x80}});
        CHECK(sameColour(materials[0]->color, rw::makeRGBA(0x80, 0xC0, 0xFF, 0x80)));
        CHECK(sameColour(materials[1]->color, second));
    }
    // Put back once the draw is done: the materials are shared by every object of the type.
    CHECK(sameColour(materials[0]->color, first));
    CHECK(sameColour(materials[1]->color, second));
    atomic->destroy();
}

TEST_CASE("a human's shade scales his materials' own colours for the draw and his fade is kept",
          "[platform][humans][frame-description]") {
    // Expected: docs/research/graphics.md#human-draw, confirmed (code): HumanRender_Draw (0x00174320) writes the
    // material colour as the human's colour with R, G and B times the shadow dimming f and its alpha the distance fade.
    // Coney keeps each material's own colour as the human's colour, so the next draw scales the same colours, and keeps
    // the alpha, which the Entity inspector reads as the fade the human was last drawn at.
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    const rw::RGBA first = rw::makeRGBA(200, 100, 50, 255);
    const rw::RGBA second = rw::makeRGBA(10, 20, 255, 240);
    rw::Atomic* atomic = twoMaterialAtomic(first, second);
    const rw::Material* const* materials = atomic->geometry->matList.materials;
    {
        const coney::platform::MaterialChange change(
            *atomic,
            coney::graphics::Material3d{.colour = coney::graphics::MaterialColour::Shade, .fade = 0x60, .shade = 0.5F});
        CHECK(sameColour(materials[0]->color, rw::makeRGBA(100, 50, 25, 0x60)));
        CHECK(sameColour(materials[1]->color, rw::makeRGBA(5, 10, 127, 0x60)));
    }
    CHECK(sameColour(materials[0]->color, rw::makeRGBA(200, 100, 50, 0x60)));
    CHECK(sameColour(materials[1]->color, rw::makeRGBA(10, 20, 255, 0x60)));
    atomic->destroy();
}
