// SPDX-License-Identifier: GPL-3.0-or-later
// The debug tools' world edits (docs/guides/running-viewers.md#world-edits), their plain-data side: connected pieces,
// the box rules, collision from up-facing faces and from a box, the spread layout, moved pieces' offsets and the edit
// file's statements. Synthetic meshes only; no game, no disc, no window.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "debug/world_split.h"
#include "raycast/collision_builder.h"
#include "raycast/collision_mesh.h"
#include "support/optional_value.h"

using Catch::Approx;
using coney::debug::CutRule;
using coney::debug::GameBox;
using coney::debug::MeshTriangle;
using coney::debug::Vec3;

namespace {

// Two unit quads side by side on the ground (x 0-1 and 2-3), each two triangles, plus a quad standing on the second
// (a wall at x 3, z 0-1) that shares its two corners with copies of the second quad's vertices.
struct Fixture {
    std::vector<Vec3> positions{
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},  // quad A
        {2, 0, 0}, {3, 0, 0}, {3, 1, 0}, {2, 1, 0},  // quad B
        {3, 0, 0}, {3, 1, 0}, {3, 1, 1}, {3, 0, 1}}; // wall on B's edge, its own copies of B's corners
    std::vector<MeshTriangle> triangles{{0, 1, 2}, {0, 2, 3}, {4, 5, 6}, {4, 6, 7}, {8, 9, 10}, {8, 10, 11}};
};

// The number of bytes set in `mask`.
std::size_t taken(const std::vector<std::uint8_t>& mask) {
    std::size_t n = 0;
    for (const std::uint8_t b : mask) {
        n += b != 0 ? 1 : 0;
    }
    return n;
}

// A flat collision mesh of one 4 x 4 m square at z 0 (two triangles), built as the sandbox builds one.
std::unique_ptr<coney::raycast::CollisionMesh> floorMesh() {
    std::vector<coney::raycast::BuildTriangle> tris(2);
    tris[0].corners = {coney::raycast::Vec3{0, 0, 0}, coney::raycast::Vec3{4, 0, 0}, coney::raycast::Vec3{4, 4, 0}};
    tris[1].corners = {coney::raycast::Vec3{0, 0, 0}, coney::raycast::Vec3{4, 4, 0}, coney::raycast::Vec3{0, 4, 0}};
    tris[1].material = 16;
    tris[1].flags = 0x0080;
    auto built = coney::raycast::buildCollisionMesh(tris, 2.0F);
    REQUIRE(built.has_value());
    return std::move(*built);
}

} // namespace

TEST_CASE("connected pieces join triangles through welded copies of a vertex", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own debug tool: pieces by shared, welded vertices)
    const Fixture f;
    const coney::debug::MeshPieces pieces = coney::debug::connectedPieces(f.positions, f.triangles, 0.01F);
    REQUIRE(pieces.pieceOf.size() == f.triangles.size());
    CHECK(pieces.count == 2);
    CHECK(pieces.pieceOf[0] == 0);
    CHECK(pieces.pieceOf[1] == 0);
    // Quad B and the wall share no vertex index, only positions: welded, they are one piece.
    CHECK(pieces.pieceOf[2] == 1);
    CHECK(pieces.pieceOf[4] == 1);
    CHECK(pieces.pieceOf[5] == 1);
    // With no weld at all the copies stay apart: three pieces.
    CHECK(coney::debug::connectedPieces(f.positions, f.triangles, 0.0F).count == 3);
}

TEST_CASE("a box takes triangles by centroid, by corners or by whole pieces", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own debug tool)
    const Fixture f;
    // A box over quad B's right half and the wall: centroids of B's first triangle (x 2.67) and the wall's.
    const GameBox box{{2.5F, -0.5F, -0.5F}, {3.5F, 1.5F, 1.5F}};
    const std::vector<std::uint8_t> byCentroid =
        coney::debug::trianglesInBox(f.positions, f.triangles, box, CutRule::Centroid);
    CHECK(taken(byCentroid) == 3);
    CHECK(byCentroid[2] == 1);
    CHECK(byCentroid[3] == 0); // centroid x 2.33
    // Every corner inside: only the wall.
    const std::vector<std::uint8_t> byCorners =
        coney::debug::trianglesInBox(f.positions, f.triangles, box, CutRule::AllVertices);
    CHECK(taken(byCorners) == 2);
    CHECK(byCorners[4] == 1);
    CHECK(byCorners[5] == 1);
    // Pieces: B and the wall are one piece that is not wholly inside, so nothing.
    CHECK(taken(coney::debug::trianglesInBox(f.positions, f.triangles, box, CutRule::Pieces)) == 0);
    // A box round B and the wall takes that piece whole and leaves A.
    const GameBox wide{{1.5F, -0.5F, -0.5F}, {3.5F, 1.5F, 1.5F}};
    const std::vector<std::uint8_t> byPieces =
        coney::debug::trianglesInBox(f.positions, f.triangles, wide, CutRule::Pieces);
    CHECK(taken(byPieces) == 4);
    CHECK(byPieces[0] == 0);
}

TEST_CASE("up-facing collision keeps only floors, wound to face up", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own debug tool; one-sided tests hit the side the normal points to:
    // research/collision.md#triangles, confirmed (code))
    const Fixture f;
    // A floor triangle wound clockwise seen from above (its normal points down) and the wall.
    std::vector<MeshTriangle> triangles{{0, 2, 1}, {8, 9, 10}};
    const std::vector<std::uint8_t> all{1, 1};
    const std::vector<coney::raycast::BuildTriangle> floors =
        coney::debug::upFacingCollision(f.positions, triangles, all, 0.9F, 5);
    REQUIRE(floors.size() == 1);
    const auto& t = floors[0];
    const Vec3 e1{t.corners[1].x - t.corners[0].x, t.corners[1].y - t.corners[0].y, t.corners[1].z - t.corners[0].z};
    const Vec3 e2{t.corners[2].x - t.corners[0].x, t.corners[2].y - t.corners[0].y, t.corners[2].z - t.corners[0].z};
    CHECK(e1.x * e2.y - e1.y * e2.x > 0.0F); // the normal's z
    CHECK(t.material == 5);
    // Untaken triangles are left out.
    const std::vector<std::uint8_t> none{0, 0};
    CHECK(coney::debug::upFacingCollision(f.positions, triangles, none, 0.9F, 5).empty());
}

TEST_CASE("collision in a box comes back moved, with its flags and material", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own debug tool; the box rule is CollisionMesh_SetEnabledInBox's:
    // research/collision.md#enable, confirmed (code))
    const auto mesh = floorMesh();
    const GameBox box{{-1, -1, -1}, {5, 5, 1}};
    const auto moved = coney::debug::collisionInBox(*mesh, box, Vec3{10, 0, 2});
    REQUIRE(moved.size() == 2);
    float minX = 1e9F;
    for (const auto& t : moved) {
        for (const auto& c : t.corners) {
            minX = std::min(minX, c.x);
            CHECK(c.z == Approx(2.0F));
        }
    }
    CHECK(minX == Approx(10.0F));
    CHECK((moved[1].material == 16 || moved[0].material == 16));
    // A box round only part of a triangle takes nothing of it; a disabled triangle is left out.
    CHECK(coney::debug::collisionInBox(*mesh, GameBox{{-1, -1, -1}, {2, 2, 1}}, Vec3{}).empty());
    mesh->setEnabledInBox(coney::raycast::Vec3{-1, -1, -1}, coney::raycast::Vec3{5, 5, 1}, false);
    CHECK(coney::debug::collisionInBox(*mesh, box, Vec3{}).empty());
}

TEST_CASE("the spread layout puts scaled boxes on a grid, standing on its floor", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own debug tool)
    const std::array<GameBox, 3> boxes{GameBox{{0, 0, 0}, {10, 10, 10}}, GameBox{{100, 50, -5}, {104, 54, 5}},
                                       GameBox{{-20, -20, 3}, {0, 0, 7}}};
    const auto placed = coney::debug::spreadLayout(boxes, 2, 1.0F, 0.5F, Vec3{0, 0, 0});
    REQUIRE(placed.size() == 3);
    // Cells are the largest footprint (20 m) times 0.5 plus 1 m: 11 m apart.
    const auto where = [&](std::size_t i, Vec3 p) {
        const auto& s = placed[i];
        return Vec3{s.centre.x + (p.x - s.centre.x) * 0.5F + s.translation.x,
                    s.centre.y + (p.y - s.centre.y) * 0.5F + s.translation.y,
                    s.centre.z + (p.z - s.centre.z) * 0.5F + s.translation.z};
    };
    // Box 0's centre at the origin cell, its floor on z 0.
    const Vec3 c0 = where(0, Vec3{5, 5, 5});
    CHECK(c0.x == Approx(0.0F));
    CHECK(c0.y == Approx(0.0F));
    CHECK(where(0, Vec3{5, 5, 0}).z == Approx(0.0F));
    // Box 1 in the next column (+x), box 2 in the next row (-y).
    const Vec3 c1 = where(1, Vec3{102, 52, 0});
    CHECK(c1.x == Approx(11.0F));
    CHECK(c1.y == Approx(0.0F));
    CHECK(where(1, Vec3{102, 52, -5}).z == Approx(0.0F));
    const Vec3 c2 = where(2, Vec3{-10, -10, 3});
    CHECK(c2.x == Approx(0.0F));
    CHECK(c2.y == Approx(-11.0F));
    CHECK(c2.z == Approx(0.0F));
}

TEST_CASE("the fitted spread scales every box to its own cell", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own debug tool)
    const std::array<GameBox, 2> boxes{GameBox{{0, 0, 0}, {10, 20, 10}}, GameBox{{100, 50, -5}, {104, 54, 5}}};
    const auto placed = coney::debug::spreadLayout(boxes, 2, 1.0F, 0.5F, Vec3{0, 0, 0}, 4.0F);
    REQUIRE(placed.size() == 2);
    // Footprints 20 and 4 m fit 4 m: scales 0.2 and 1; cells 5 m apart.
    CHECK(placed[0].scale == Approx(0.2F));
    CHECK(placed[1].scale == Approx(1.0F));
    const auto& s = placed[1];
    CHECK(s.centre.x + s.translation.x == Approx(5.0F));
    CHECK(s.centre.z + s.translation.z == Approx(5.0F)); // half its 10 m height above the floor
    // The plain layout carries its one scale in every placement.
    CHECK(coney::debug::spreadLayout(boxes, 2, 1.0F, 0.5F, Vec3{})[0].scale == Approx(0.5F));
}

TEST_CASE("a moved piece's offset runs straight from key to key", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own debug tool)
    const std::array<coney::debug::OffsetKey, 2> keys{coney::debug::OffsetKey{10, Vec3{0, 0, 0}},
                                                      coney::debug::OffsetKey{30, Vec3{4, -2, 8}}};
    CHECK(coney::debug::offsetAt(keys, 0).x == Approx(0.0F));
    const Vec3 half = coney::debug::offsetAt(keys, 20);
    CHECK(half.x == Approx(2.0F));
    CHECK(half.y == Approx(-1.0F));
    CHECK(half.z == Approx(4.0F));
    CHECK(coney::debug::offsetAt(keys, 99).z == Approx(8.0F));
    CHECK(coney::debug::offsetAt({}, 5).x == Approx(0.0F));
}

TEST_CASE("a world edit file reads every statement", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own file, docs/guides/running-viewers.md#world-edits)
    const auto edits = coney::debug::parseWorldEdits("# the experiment\n"
                                                     "hide s\n"
                                                     "spread columns=12 gap=3 scale=0.2 at=1,2,3\n"
                                                     "camera at=40,50,6 heading=180 pitch=-20\n"
                                                     "cut shop box=50,11,-1,65,31,20 by=pieces collision=no\n"
                                                     "move shop step=30 offset=0,0,0\n"
                                                     "move shop step=90 offset=0,6,4  # up and out\n"
                                                     "roof box=28,12,3,45,31,12 material=16\n"
                                                     "debugdraw collision radius=25\n");
    REQUIRE(edits.has_value());
    CHECK(edits->hidden[0]);
    CHECK_FALSE(edits->hidden[1]);
    const auto spread = coney::test::got(edits->spread);
    CHECK(spread.columns == 12);
    CHECK(spread.scale == Approx(0.2F));
    CHECK(spread.origin.z == Approx(3.0F));
    const auto camera = coney::test::got(edits->camera);
    CHECK(camera.heading == Approx(180.0F));
    CHECK(camera.pitch == Approx(-20.0F));
    REQUIRE(edits->cuts.size() == 1);
    CHECK(edits->cuts[0].name == "shop");
    CHECK(edits->cuts[0].rule == CutRule::Pieces);
    CHECK_FALSE(edits->cuts[0].collision);
    CHECK(edits->cuts[0].box.max.y == Approx(31.0F));
    REQUIRE(edits->cuts[0].keys.size() == 2);
    CHECK(edits->cuts[0].keys[1].step == 90);
    CHECK(edits->cuts[0].keys[1].offset.z == Approx(4.0F));
    REQUIRE(edits->roofs.size() == 1);
    CHECK(edits->roofs[0].material == 16);
    CHECK(coney::test::got(edits->collisionWire) == Approx(25.0F));
    // A box's corners may come in either order: they are sorted.
    const auto swapped = coney::debug::parseWorldEdits("cut a box=10,10,10,0,0,0\n");
    REQUIRE(swapped.has_value());
    CHECK(swapped->cuts[0].box.min.x == Approx(0.0F));
    CHECK(swapped->cuts[0].box.max.z == Approx(10.0F));
    // A fitted spread names its cell, and not a scale besides.
    const auto fitted = coney::debug::parseWorldEdits("spread cell=6\n");
    REQUIRE(fitted.has_value());
    CHECK(coney::test::got(coney::test::got(fitted->spread).cell) == Approx(6.0F));
    CHECK_FALSE(coney::debug::parseWorldEdits("spread cell=6 scale=0.5\n").has_value());
    CHECK_FALSE(coney::debug::parseWorldEdits("spread cell=0\n").has_value());
}

TEST_CASE("a world edit file refuses what it cannot read, naming the line", "[debug][world_split]") {
    // Expected: unverified, #1123 (Coney's own file)
    const auto unknown = coney::debug::parseWorldEdits("hide s\nexplode everything\n");
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().message.find("line 2") != std::string::npos);
    CHECK_FALSE(coney::debug::parseWorldEdits("move nothing step=1 offset=0,0,0\n").has_value()); // no such cut
    CHECK_FALSE(coney::debug::parseWorldEdits("cut a box=1,2,3\n").has_value());                  // six numbers
    CHECK_FALSE(coney::debug::parseWorldEdits("hide x\n").has_value());
    CHECK_FALSE(coney::debug::parseWorldEdits("cut a box=0,0,0,1,1,1\nmove a step=9 offset=0,0,0\n"
                                              "move a step=3 offset=0,0,0\n")
                    .has_value()); // steps go back
    CHECK_FALSE(coney::debug::parseWorldEdits("spread columns=0\n").has_value());
    CHECK_FALSE(coney::debug::parseWorldEdits("cut a box=0,0,0,1,1,1 by=magic\n").has_value());
}

TEST_CASE("a point's distance to a triangle reaches its face, edges and corners", "[debug][world_split]") {
    // Expected: unverified, #1123 (plain geometry)
    const Vec3 a{0, 0, 0};
    const Vec3 b{4, 0, 0};
    const Vec3 c{0, 4, 0};
    CHECK(coney::debug::pointTriangleDistance(Vec3{1, 1, 3}, a, b, c) == Approx(3.0F));   // over the face
    CHECK(coney::debug::pointTriangleDistance(Vec3{2, -3, 0}, a, b, c) == Approx(3.0F));  // beside edge ab
    CHECK(coney::debug::pointTriangleDistance(Vec3{-3, -4, 0}, a, b, c) == Approx(5.0F)); // past corner a
    CHECK(coney::debug::pointTriangleDistance(Vec3{3, 3, 0}, a, b, c) == Approx(std::sqrt(2.0F)));
}

TEST_CASE("a triangle grid finds the nearest triangle within its reach", "[debug][world_split]") {
    // Expected: unverified, #1123 (plain geometry)
    const Fixture f;
    const coney::debug::TriangleGrid grid(f.positions, f.triangles, 2.0F);
    // Over quad A, 0.5 m up.
    CHECK(coney::test::got(grid.nearest(Vec3{0.5F, 0.5F, 0.5F}, 2.0F)) == Approx(0.5F));
    // Beside the wall at x 3, 1 m out.
    CHECK(coney::test::got(grid.nearest(Vec3{4.0F, 0.5F, 0.5F}, 2.0F)) == Approx(1.0F));
    // Nothing within reach far away, or past the reach.
    CHECK_FALSE(grid.nearest(Vec3{50, 50, 50}, 2.0F).has_value());
    CHECK_FALSE(grid.nearest(Vec3{0.5F, 0.5F, 1.5F}, 1.0F).has_value());
}
