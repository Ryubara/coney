// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/character_model.h"

#include <array>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/character_fixtures.h"

using Catch::Approx;
using coney::ErrorCode;
using coney::test::Bytes;
using coney::test::ClumpFields;

TEST_CASE("a character clump decodes into merged skinned vertices and strip triangles", "[character_model]") {
    const Bytes clump = coney::test::characterClump(coney::test::testCharacterFields());
    auto model = coney::characters::decodeCharacterModel(clump.span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    CHECK(model->clump.frames.size() == 3);
    REQUIRE(model->clump.hierarchy.size() == 2);
    CHECK(model->clump.hierarchy[1].id == 1);
    CHECK(model->clump.frames[2].hanimId == 1);
    CHECK(model->clump.skin.boneCount == 2);
    CHECK(model->clump.skin.inverseBind[1].t.x == -1.0F);
    CHECK(model->clump.materials.size() == 1);
    CHECK(model->clump.atomicPipeline == 0x30080U);
    CHECK(model->stripVertices == 5);
    REQUIRE(model->vertices.size() == 4);                                         // the repeated vertex merged
    REQUIRE(model->triangles.size() == 2);                                        // the degenerate one dropped
    CHECK(model->triangles[1].vertices == std::array<std::uint16_t, 3>{2, 1, 3}); // the second turned round
    CHECK(model->vertices[1].position.x == Approx(1.0F));                         // 2 × the 0.5 scale
    CHECK(model->vertices[1].bones[0] == 1);
    CHECK(model->vertices[1].weights[0] == Approx(1.0F).margin(1e-3));
    CHECK(model->vertices[3].weights[0] == Approx(0.5F).margin(1e-3));
    CHECK(model->vertices[3].bones[1] == 1);
    CHECK(model->vertices[0].texCoords[1] == 0.0F);
    CHECK(model->vertices[0].normal.z == Approx(126.0F / 128.0F));
    CHECK(model->boneOffsets[3].x == 1.0F);
}

TEST_CASE("a character clump without its skin, or with a wrong bone offset chunk, is refused", "[character_model]") {
    ClumpFields fields = coney::test::testCharacterFields();
    fields.withSkin = false;
    auto noSkin = coney::characters::decodeCharacterModel(coney::test::characterClump(fields).span(),
                                                          coney::test::testBoneOffsets().span());
    REQUIRE_FALSE(noSkin.has_value());
    CHECK(noSkin.error().code == ErrorCode::Invalid);

    const Bytes clump = coney::test::characterClump(coney::test::testCharacterFields());
    auto shortOffsets =
        coney::characters::decodeCharacterModel(clump.span(), coney::test::testBoneOffsets().span().first(540));
    REQUIRE_FALSE(shortOffsets.has_value());
    CHECK(shortOffsets.error().code == ErrorCode::Invalid);

    auto cut = coney::characters::decodeCharacterModel(clump.span().first(clump.size() - 40),
                                                       coney::test::testBoneOffsets().span());
    REQUIRE_FALSE(cut.has_value());
}
