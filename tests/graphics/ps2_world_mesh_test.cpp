// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "graphics/ps2_world_mesh.h"
#include "support/character_fixtures.h"
#include "support/world_fixtures.h"

using coney::ErrorCode;
using coney::graphics::decodePs2WorldMesh;
using coney::test::Bytes;
using coney::test::ps2MeshChain;
using coney::test::ps2Vertex;

TEST_CASE("a one-batch chain decodes to its vertices, the padding vertex dropped", "[ps2_world_mesh]") {
    // Three real vertices, unpacked as four (even count), ITOP 3.
    const Bytes chain = ps2MeshChain({{ps2Vertex(1, 2, 3), ps2Vertex(4, 5, 6), ps2Vertex(-7, 8, -9)}}, true);
    auto mesh = decodePs2WorldMesh(chain.span(), true);
    REQUIRE(mesh.has_value());
    REQUIRE(mesh->vertices.size() == 3);
    CHECK(mesh->batches == 1);
    CHECK(mesh->vertices[2].position[0] == -7);
    CHECK(mesh->vertices[2].position[2] == -9);
    CHECK(mesh->vertices[1].colour[3] == 0x80);
    CHECK(mesh->vertices[1].normal[2] == 0x7E);
    CHECK(mesh->vertices[0].texCoords[1] == 2 * 2);
    CHECK(mesh->attributes.texCoordSets == 2);
    CHECK(mesh->attributes.colours);
    CHECK(mesh->attributes.normals);
}

TEST_CASE("strip batches are joined without their two repeated vertices", "[ps2_world_mesh]") {
    const auto a = ps2Vertex(1, 0, 0);
    const auto b = ps2Vertex(2, 0, 0);
    const auto c = ps2Vertex(3, 0, 0);
    const auto d = ps2Vertex(4, 0, 0);
    const auto e = ps2Vertex(5, 0, 0);
    const Bytes chain = ps2MeshChain({{a, b, c, d}, {c, d, e}}, false);
    auto mesh = decodePs2WorldMesh(chain.span(), true);
    REQUIRE(mesh.has_value());
    CHECK(mesh->batches == 2);
    REQUIRE(mesh->vertices.size() == 5);
    CHECK(mesh->vertices[4].position[0] == 5);

    // As a list nothing is dropped.
    auto list = decodePs2WorldMesh(chain.span(), false);
    REQUIRE(list.has_value());
    CHECK(list->vertices.size() == 7);
}

TEST_CASE("strip batches that do not overlap are refused", "[ps2_world_mesh]") {
    const Bytes chain = ps2MeshChain({{ps2Vertex(1, 0, 0), ps2Vertex(2, 0, 0), ps2Vertex(3, 0, 0)},
                                      {ps2Vertex(9, 0, 0), ps2Vertex(3, 0, 0), ps2Vertex(4, 0, 0)}},
                                     false);
    auto mesh = decodePs2WorldMesh(chain.span(), true);
    REQUIRE_FALSE(mesh.has_value());
    CHECK(mesh.error().code == ErrorCode::Invalid);
}

TEST_CASE("a chain cut short or with an unknown tag fails cleanly", "[ps2_world_mesh]") {
    const Bytes chain = ps2MeshChain({{ps2Vertex(1, 2, 3), ps2Vertex(4, 5, 6), ps2Vertex(7, 8, 9)}}, false);
    auto cut = decodePs2WorldMesh(chain.span().first(chain.size() - 20), true);
    REQUIRE_FALSE(cut.has_value());
    CHECK(cut.error().code == ErrorCode::Truncated);

    Bytes call;
    call.u32(0x50000000).u32(0).u32(0).u32(0); // a call tag
    auto unknown = decodePs2WorldMesh(call.span(), true);
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().code == ErrorCode::Invalid);
}

TEST_CASE("an unpack in another format is not the world layout", "[ps2_world_mesh]") {
    // One batch whose positions come as V3_32, librw's default layout.
    Bytes chain;
    chain.u32(0x10000000 | 2).u32(0).u32(0x01000104).u32(0x68018000); // cnt, 2 qwords; STCYCL 4,1; UNPACK V3_32 x1
    chain.u32(0).u32(0).u32(0).u32(0x04000001);                       // the vector, ITOP 1
    chain.u32(0x15000000).u32(0).u32(0).u32(0);                       // MSCALF, padding
    chain.u32(0x60000000).u32(0).u32(0).u32(0);                       // ret
    auto mesh = decodePs2WorldMesh(chain.span(), true);
    REQUIRE_FALSE(mesh.has_value());
    CHECK(mesh.error().code == ErrorCode::Invalid);
}

namespace {

// One vertex of RenderWare's default layout for the chains below: position (i, 2i, 3i), uv (i / 10, 1), colour i,
// normal (0, 127, 0).
coney::graphics::Ps2DefaultVertex defaultVertex(int i) {
    const auto f = static_cast<float>(i);
    const auto c = static_cast<std::uint8_t>(i);
    return {{f, 2 * f, 3 * f}, {f / 10, 1.0F}, {c, c, c, 255}, {0, 127, 0}};
}

// A chain in RenderWare's default PS2 layout: one ret tag carrying, per batch, STCYCL 4,1, the four UNPACKs (V3_32,
// V2_32, V4_8 unsigned, V3_8), ITOP and MSCALF, padded to whole 16-byte units. `positionFormat` replaces V3_32 to make
// a layout the decoder must refuse.
Bytes defaultChain(const std::vector<std::vector<int>>& batches, std::uint32_t positionFormat = 0x08) {
    Bytes vif;
    for (const std::vector<int>& batch : batches) {
        const auto n = static_cast<std::uint32_t>(batch.size());
        vif.u32(0x01000104); // STCYCL cycle 4, write 1
        vif.u32((0x60U | positionFormat) << 24U | n << 16U | 0x8000U);
        for (const int i : batch) {
            const auto v = defaultVertex(i);
            for (const float p : v.position) {
                coney::test::f32(vif, p);
            }
        }
        vif.u32(0x64000000U | n << 16U | 0x8001U);
        for (const int i : batch) {
            const auto v = defaultVertex(i);
            coney::test::f32(coney::test::f32(vif, v.texCoords[0]), v.texCoords[1]);
        }
        vif.u32(0x6E000000U | n << 16U | 0xC002U);
        for (const int i : batch) {
            for (const std::uint8_t b : defaultVertex(i).colour) {
                vif.u8(b);
            }
        }
        vif.u32(0x6A000000U | n << 16U | 0x8003U);
        Bytes normals;
        for (const int i : batch) {
            for (const std::int8_t b : defaultVertex(i).normal) {
                normals.u8(static_cast<std::uint8_t>(b));
            }
        }
        normals.padTo((normals.size() + 3) / 4 * 4);
        vif.append(normals.span());
        vif.u32(0x04000000U | n); // ITOP
        vif.u32(0x15000000U);     // MSCALF
    }
    vif.padTo((vif.size() + 15) / 16 * 16 + 8);
    // The ret tag's own two VIF words (NOPs) come first, then its data: the stream above minus those 8 bytes.
    Bytes chain;
    chain.u32(0x60000000U | static_cast<std::uint32_t>((vif.size() - 8) / 16)).u32(0).u32(0).u32(0);
    chain.append(vif.span());
    return chain;
}

} // namespace

TEST_CASE("RenderWare's default layout decodes to float vertices, strip batches joined", "[ps2_world_mesh]") {
    const Bytes chain = defaultChain({{0, 1, 2}, {1, 2, 3}});
    auto mesh = coney::graphics::decodePs2DefaultMesh(chain.span(), true);
    REQUIRE(mesh.has_value());
    CHECK(mesh->batches == 2);
    REQUIRE(mesh->vertices.size() == 4);
    for (int i = 0; i < 4; ++i) {
        CHECK(mesh->vertices[static_cast<std::size_t>(i)] == defaultVertex(i));
    }
}

TEST_CASE("the default decoder refuses the game's packed layout and broken strips", "[ps2_world_mesh]") {
    auto packed = coney::graphics::decodePs2DefaultMesh(defaultChain({{0, 1, 2}}, 0x0D).span(), true);
    REQUIRE_FALSE(packed.has_value());
    CHECK(packed.error().code == ErrorCode::Invalid);
    auto broken = coney::graphics::decodePs2DefaultMesh(defaultChain({{0, 1, 2}, {5, 6, 7}}).span(), true);
    REQUIRE_FALSE(broken.has_value());
    CHECK(broken.error().code == ErrorCode::Invalid);
}

TEST_CASE("a skinned chain decodes its fifth slot, the bone weights", "[ps2_world_mesh]") {
    // Two batches of a strip, five slots a vertex (STCYCL 5, 1), as the characters' geometry is.
    const auto a = coney::test::skinnedVertex(1, 0, 0, 3);
    const auto b = coney::test::skinnedVertex(2, 0, 0, 4, 0.75F, 9);
    const auto c = coney::test::skinnedVertex(3, 0, 0, 31);
    const auto d = coney::test::skinnedVertex(4, 0, 0, 0);
    const Bytes chain = coney::test::ps2SkinnedMeshChain({{a, b, c}, {b, c, d}});
    auto mesh = decodePs2WorldMesh(chain.span(), true);
    REQUIRE(mesh.has_value());
    CHECK(mesh->attributes.skin);
    CHECK(mesh->attributes.normals);
    REQUIRE(mesh->vertices.size() == 4);
    CHECK(mesh->vertices[1].skin == b.skin);
    CHECK(mesh->vertices[3].skin == d.skin);
    CHECK(mesh->vertices[3].position[0] == 4);

    // The world's chains carry no weights.
    auto world = decodePs2WorldMesh(ps2MeshChain({{ps2Vertex(1, 2, 3), ps2Vertex(4, 5, 6)}}, true).span(), false);
    REQUIRE(world.has_value());
    CHECK_FALSE(world->attributes.skin);
}
