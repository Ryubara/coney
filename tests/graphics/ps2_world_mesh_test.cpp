// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "graphics/ps2_world_mesh.h"
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
