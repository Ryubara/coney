// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_mesh.h"

#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "characters/character_model.h"
#include "platform/render_engine.h"
#include "support/character_fixtures.h"

TEST_CASE("a character model becomes librw geometry whose vertices follow the skinning", "[character_mesh]") {
    // librw on its NULL device: no window or GPU.
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    {
        coney::platform::CharacterMesh mesh(*model, nullptr);
        rw::Geometry* geometry = mesh.atomic()->geometry;
        CHECK(geometry->numVertices == 4);
        CHECK(geometry->numTriangles == 2);
        CHECK(geometry->matList.numMaterials == 1);
        CHECK(geometry->morphTargets[0].vertices[1].x == 1.0F);

        std::vector<coney::anim::Vec3> positions(4, coney::anim::Vec3{0.0F, 0.0F, 5.0F});
        std::vector<coney::anim::Vec3> normals(4, coney::anim::Vec3{0.0F, 0.0F, 1.0F});
        mesh.update(positions, normals);
        CHECK(geometry->morphTargets[0].vertices[3].z == 5.0F);
        CHECK(geometry->morphTargets[0].boundingSphere.center.z == 5.0F);
    }
}
