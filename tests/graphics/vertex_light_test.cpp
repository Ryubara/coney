// SPDX-License-Identifier: GPL-3.0-or-later
// The CPU reference of the PS2's vertex lighting (docs/research/lighting.md#vertex-reference): the colours the
// original's GS dumps showed, each term of `min(1, prelight + light) × material` on synthetic meshes (a grid with
// normals in every direction, a sphere, a capsule figure), and Coney's own light manager feeding it. Synthetic
// meshes and lights only.
#include "graphics/vertex_light.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "graphics/light_manager.h"

using Catch::Approx;
using coney::graphics::GsColour;
using coney::graphics::LightColour;
using coney::graphics::LightDescriptor;
using coney::graphics::LightManager;
using coney::graphics::LightSphere;
using coney::graphics::LightType;
using coney::graphics::LightView;
using coney::graphics::ReferenceVertex;
using coney::graphics::Vu1Light;
using coney::world::Vec3;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// A white material, opaque.
constexpr LightColour kWhite{1.0F, 1.0F, 1.0F, 1.0F};

// An ambient light of colour (r, g, b).
Vu1Light ambient(float r, float g, float b) { return Vu1Light{.type = LightType::Ambient, .colour = {r, g, b, 1.0F}}; }

// A directional light of grey `level` travelling along `direction` (unit).
Vu1Light directional(float level, Vec3 direction) {
    return Vu1Light{.type = LightType::Directional, .colour = {level, level, level, 1.0F}, .direction = direction};
}

// A point light of grey `level` at `at` with radius `radius`.
Vu1Light point(float level, Vec3 at, float radius) {
    return Vu1Light{.type = LightType::Point, .colour = {level, level, level, 1.0F}, .position = at, .radius = radius};
}

// A vertex at `at` facing `normal` with no prelighting.
ReferenceVertex unlit(Vec3 at, Vec3 normal) { return ReferenceVertex{.position = at, .normal = normal}; }

// The unit vector along `v`.
Vec3 unit(Vec3 v) {
    const float length = std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
    return Vec3{v.x / length, v.y / length, v.z / length};
}

// A grid of vertices on the plane y = 0 whose normals point in every direction: the 26 directions to a cube's faces,
// edges and corners, one per vertex, so each term meets normals facing towards, across and away from each light.
std::vector<ReferenceVertex> normalGrid() {
    std::vector<ReferenceVertex> grid;
    float x = 0.0F;
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            for (int k = -1; k <= 1; ++k) {
                if (i == 0 && j == 0 && k == 0) {
                    continue;
                }
                const Vec3 normal = unit(Vec3{static_cast<float>(i), static_cast<float>(j), static_cast<float>(k)});
                grid.push_back(unlit(Vec3{x, 0.0F, 0.0F}, normal));
                x += 0.25F;
            }
        }
    }
    return grid;
}

// A sphere of radius `radius` round `centre`, `rings` × `segments` vertices, each with its outward normal.
std::vector<ReferenceVertex> sphere(Vec3 centre, float radius, int rings, int segments) {
    std::vector<ReferenceVertex> vertices;
    for (int ring = 0; ring <= rings; ++ring) {
        const float polar = kPi * static_cast<float>(ring) / static_cast<float>(rings);
        for (int segment = 0; segment < segments; ++segment) {
            const float azimuth = 2.0F * kPi * static_cast<float>(segment) / static_cast<float>(segments);
            const Vec3 normal{std::sin(polar) * std::cos(azimuth), std::cos(polar),
                              std::sin(polar) * std::sin(azimuth)};
            vertices.push_back(unlit(
                Vec3{centre.x + (radius * normal.x), centre.y + (radius * normal.y), centre.z + (radius * normal.z)},
                normal));
        }
    }
    return vertices;
}

// A standing capsule figure 1.8 m tall at `feet` (RenderWare's axes, y up): two hemispheres of radius 0.3 joined by a
// cylinder, the shape a human's body is lit as.
std::vector<ReferenceVertex> capsuleFigure(Vec3 feet) {
    constexpr float kRadius = 0.3F;
    constexpr float kHeight = 1.8F;
    constexpr int kSegments = 12;
    std::vector<ReferenceVertex> vertices;
    for (int ring = 0; ring <= 12; ++ring) {
        // Rings 0-5 the top hemisphere, 6 the cylinder's two ends, 7-12 the bottom hemisphere.
        const float polar = kPi * static_cast<float>(ring) / 12.0F;
        const float centreY = polar <= kPi / 2.0F ? kHeight - kRadius : kRadius;
        for (int segment = 0; segment < kSegments; ++segment) {
            const float azimuth = 2.0F * kPi * static_cast<float>(segment) / static_cast<float>(kSegments);
            const Vec3 normal{std::sin(polar) * std::cos(azimuth), std::cos(polar),
                              std::sin(polar) * std::sin(azimuth)};
            vertices.push_back(unlit(Vec3{feet.x + (kRadius * normal.x), feet.y + centreY + (kRadius * normal.y),
                                          feet.z + (kRadius * normal.z)},
                                     normal));
        }
    }
    return vertices;
}

// The colour a vertex gets, written out from the research page's formula term by term for one channel: the yardstick
// the reference's own code is held to on the synthetic meshes.
float expectedChannel(float prelight, float ambientLevel, float directionalLevel, Vec3 towardsLight, Vec3 normal,
                      float material) {
    const float facing =
        std::max(0.0F, (normal.x * towardsLight.x) + (normal.y * towardsLight.y) + (normal.z * towardsLight.z));
    return std::min(1.0F, prelight + ambientLevel + (directionalLevel * facing)) * material;
}

// Whether `gs` is `expected` (1.0 = 128) after the GS's truncation, within the stated ±1 of 128.
bool withinOneOf128(std::uint8_t gs, float expected) {
    return std::abs(static_cast<float>(gs) - (expected * 128.0F)) <= 1.0F;
}

// A camera at the origin looking along +z, draw distance 115.
LightView camera() {
    return LightView{.position = {}, .forward = {0.0F, 0.0F, 1.0F}, .nearClip = 0.5F, .farClip = 115};
}

} // namespace

TEST_CASE("Ambient light alone gives the colours the original's GS showed, the clamp before the material") {
    // Expected: lighting.md#world, confirmed (runtime)
    // A player lit by ambient light only, item 2: 1.2 with a white HuColor gives 128, with HuColor 0x80 64 (not 77),
    // 0.6 with 0x80 38, 0.3 with (255, 128, 64) gives (38, 19, 9).
    const ReferenceVertex vertex = unlit(Vec3{}, Vec3{0.0F, 1.0F, 0.0F});
    auto gsOf = [&](float level, std::array<std::uint8_t, 4> huColour) {
        const std::array<Vu1Light, 1> lights{ambient(level, level, level)};
        return coney::graphics::toGs(
            coney::graphics::referenceColour(vertex, lights, coney::graphics::materialFromRgba(huColour)));
    };
    CHECK(gsOf(1.2F, {255, 255, 255, 255}) == GsColour{128, 128, 128, 128});
    CHECK(gsOf(1.2F, {128, 128, 128, 255}).r == 64);
    CHECK(gsOf(0.6F, {128, 128, 128, 255}).r == 38);
    CHECK(gsOf(0.3F, {255, 128, 64, 255}) == GsColour{38, 19, 9, 128});
}

TEST_CASE("The world ambient alone gives an unlit world vertex 29 and the objects' ambient a human (27, 28, 29)") {
    // Expected: lighting.md#world, confirmed (runtime)
    // Item 4: world vertices with no prelighting show 29 = the world ambient 0.227 × 128; item 5: ambient-only humans
    // show (27, 28, 29) = the object ambient (0.212, 0.222, 0.232) × 128 with a white HuColor. Both truncated.
    const ReferenceVertex vertex = unlit(Vec3{}, Vec3{0.0F, 1.0F, 0.0F});
    const std::array<Vu1Light, 1> world{ambient(0.227F, 0.227F, 0.227F)};
    CHECK(coney::graphics::toGs(coney::graphics::referenceColour(vertex, world, kWhite)).r == 29);
    const std::array<Vu1Light, 1> objects{ambient(0.212F, 0.222F, 0.232F)};
    const GsColour human = coney::graphics::toGs(coney::graphics::referenceColour(vertex, objects, kWhite));
    CHECK(human.r == 27);
    CHECK(human.g == 28);
    CHECK(human.b == 29);
}

TEST_CASE("Prelighting adds to the world ambient before the clamp, as the background's models reach the GS") {
    // Expected: lighting.md#coneys-implementation, inferred
    // level99's sky box (packed prelight 30, which Coney doubles to 60), cloud box (35) and skyline's unlit vertices
    // (0) reach the GS as 59, 64 and 29 of 128 under the world ambient 0.227.
    const std::array<Vu1Light, 1> world{ambient(0.227F, 0.227F, 0.227F)};
    auto gsOf = [&](std::uint8_t packed) {
        ReferenceVertex vertex = unlit(Vec3{}, Vec3{});
        vertex.prelight = coney::graphics::prelightFromPacked({packed, packed, packed, 255});
        return coney::graphics::toGs(coney::graphics::referenceColour(vertex, world, kWhite)).r;
    };
    CHECK(gsOf(30) == 59);
    CHECK(gsOf(35) == 64);
    CHECK(gsOf(0) == 29);
    // A bright prelight saturates: 120 of 128 plus the ambient's 29 stays at 128.
    CHECK(gsOf(120) == 128);
}

TEST_CASE("A point light falls off as 1 - d / radius and gives nothing at or beyond its radius") {
    // Expected: lighting.md#world, confirmed (runtime)
    // Item 3: the share against x = d / radius is 1 - x (measured 0.72, 0.47 and 0.13 at 0.3, 0.5 and 0.9).
    const Vec3 up{0.0F, 1.0F, 0.0F};
    constexpr float kRadius = 15.0F;
    for (const float x : {0.3F, 0.5F, 0.9F}) {
        // The lamp straight above the vertex, so the facing term is 1.
        const std::array<Vu1Light, 1> lamp{point(0.7F, Vec3{0.0F, x * kRadius, 0.0F}, kRadius)};
        const LightColour light = coney::graphics::lightAtVertex(Vec3{}, up, lamp);
        CHECK(light.r == Approx(0.7F * (1.0F - x)));
    }
    const std::array<Vu1Light, 2> beyond{point(0.7F, Vec3{0.0F, kRadius, 0.0F}, kRadius),
                                         point(0.7F, Vec3{0.0F, 20.0F, 0.0F}, kRadius)};
    CHECK(coney::graphics::lightAtVertex(Vec3{}, up, beyond).r == 0.0F);
}

TEST_CASE("A point light lights a surface by how far it faces the lamp") {
    // Expected: lighting.md#world, confirmed (runtime)
    // The point term is max(0, n · -L) × (1 - d / radius): a lamp at 45 degrees gives cos 45 of the full share, one
    // behind the surface nothing.
    constexpr float kRadius = 10.0F;
    const float side = 5.0F / std::numbers::sqrt2_v<float>;
    const std::array<Vu1Light, 1> slanted{point(1.0F, Vec3{side, side, 0.0F}, kRadius)};
    CHECK(coney::graphics::lightAtVertex(Vec3{}, Vec3{0.0F, 1.0F, 0.0F}, slanted).g ==
          Approx(std::numbers::sqrt2_v<float> / 2.0F * 0.5F));
    const std::array<Vu1Light, 1> behind{point(1.0F, Vec3{0.0F, -5.0F, 0.0F}, kRadius)};
    CHECK(coney::graphics::lightAtVertex(Vec3{}, Vec3{0.0F, 1.0F, 0.0F}, behind).g == 0.0F);
}

TEST_CASE("A directional light from behind adds nothing, and lights every grid normal by its facing") {
    // Expected: lighting.md#world, confirmed (runtime)
    // Every vertex of a grid with normals in all 26 directions, under ambient 0.1 and a directional light 0.6 that
    // travels straight down: exact in floats against the formula, and within 1 of 128 after the GS's truncation.
    const Vec3 down{0.0F, -1.0F, 0.0F};
    const std::array<Vu1Light, 2> lights{ambient(0.1F, 0.1F, 0.1F), directional(0.6F, down)};
    for (const ReferenceVertex& vertex : normalGrid()) {
        const LightColour colour = coney::graphics::referenceColour(vertex, lights, kWhite);
        const float expected = expectedChannel(0.0F, 0.1F, 0.6F, Vec3{0.0F, 1.0F, 0.0F}, vertex.normal, 1.0F);
        CHECK(colour.r == Approx(expected));
        CHECK(withinOneOf128(coney::graphics::toGs(colour).r, expected));
        if (vertex.normal.y <= 0.0F) {
            // Facing away from the light or across it: the ambient alone.
            CHECK(colour.r == Approx(0.1F));
        }
    }
}

TEST_CASE("A dark material stays dark under any amount of light") {
    // Expected: lighting.md#world, confirmed (runtime)
    // Item 2: the clamp comes before the material, so a bright light cannot lift a material above its own colour.
    const ReferenceVertex vertex = unlit(Vec3{}, Vec3{0.0F, 1.0F, 0.0F});
    const LightColour dark = coney::graphics::materialFromRgba({32, 32, 32, 255});
    const std::array<Vu1Light, 2> bright{ambient(2.0F, 2.0F, 2.0F), directional(5.0F, Vec3{0.0F, -1.0F, 0.0F})};
    const GsColour gs = coney::graphics::toGs(coney::graphics::referenceColour(vertex, bright, dark));
    CHECK(gs.r == 16); // 32 / 255 × 128 = 16.06
    CHECK(gs.a == 128);
}

TEST_CASE("A sphere under a lamp and the moon matches the formula at every vertex") {
    // Expected: lighting.md#world, confirmed (runtime)
    // A sphere of radius 1 at (0, 1, 0), a lamp 2 m above its top (radius 6, colour 0.25) and a moonlight from the
    // side, prelit at 10 of 128: each vertex against the formula with the lamp's falloff and facing worked out here.
    const Vec3 lampAt{0.0F, 4.0F, 0.0F};
    constexpr float kRadius = 6.0F;
    const Vec3 moonTravels = unit(Vec3{1.0F, -1.0F, 0.0F});
    const std::array<Vu1Light, 3> lights{ambient(0.227F, 0.227F, 0.227F), directional(0.3F, moonTravels),
                                         point(0.25F, lampAt, kRadius)};
    const LightColour prelight = coney::graphics::prelightFromPacked({10, 10, 10, 255});
    for (ReferenceVertex vertex : sphere(Vec3{0.0F, 1.0F, 0.0F}, 1.0F, 8, 12)) {
        vertex.prelight = prelight;
        const Vec3 towards{lampAt.x - vertex.position.x, lampAt.y - vertex.position.y, lampAt.z - vertex.position.z};
        const float distance = std::sqrt((towards.x * towards.x) + (towards.y * towards.y) + (towards.z * towards.z));
        const Vec3 l = unit(towards);
        const float lampFacing =
            std::max(0.0F, (vertex.normal.x * l.x) + (vertex.normal.y * l.y) + (vertex.normal.z * l.z));
        const float lamp = 0.25F * lampFacing * std::max(0.0F, 1.0F - (distance / kRadius));
        const float expected = expectedChannel(10.0F / 128.0F, 0.227F + lamp, 0.3F,
                                               Vec3{-moonTravels.x, -moonTravels.y, 0.0F}, vertex.normal, 1.0F);
        const LightColour colour = coney::graphics::referenceColour(vertex, lights, kWhite);
        CHECK(colour.g == Approx(expected));
        CHECK(withinOneOf128(coney::graphics::toGs(colour).g, expected));
    }
}

TEST_CASE("A capsule figure lit through Coney's light manager gets the reference's colours from the selection") {
    // Expected: lighting.md#world, confirmed (runtime)
    // Coney's own path: a manager with an objects' ambient, a moonlight (both with the brightness 40/255 added) and a
    // lamp, its selection for a human's sphere turned into the light block and the figure lit by it; every vertex
    // equals the same lights given by hand, so the vertex colour comes from the colours the manager shines with.
    LightManager manager;
    const float added = 40.0F / 255.0F;
    LightDescriptor objectAmbient;
    objectAmbient.type = LightType::Ambient;
    objectAmbient.colour = LightColour{0.055F, 0.065F, 0.075F, 1.0F};
    objectAmbient.lights = coney::graphics::kLightsObjects;
    manager.addLight(objectAmbient);
    LightDescriptor moon;
    moon.type = LightType::Directional;
    moon.direction = unit(Vec3{0.0F, -1.0F, 1.0F});
    moon.colour = LightColour{0.07F, 0.10F, 0.18F, 1.0F};
    moon.lights = coney::graphics::kLightsObjects;
    manager.addLight(moon);
    LightDescriptor lamp;
    lamp.type = LightType::Point;
    lamp.position = Vec3{1.0F, 3.0F, 5.0F};
    lamp.radius = 8.0F;
    lamp.colour = LightColour{0.6F, 0.5F, 0.3F, 1.0F};
    lamp.lights = coney::graphics::kLightsObjects;
    manager.addLight(lamp);
    manager.beginViewport(camera(), 0);

    const Vec3 feet{0.0F, 0.0F, 5.0F};
    const LightSphere body{.centre = Vec3{feet.x, feet.y + 0.9F, feet.z}, .radius = 0.9F};
    const coney::graphics::LightSelection selection = manager.select(25.0F, body, true, true, false);
    const std::vector<Vu1Light> block = coney::graphics::vu1Lights(manager, selection);
    REQUIRE(block.size() == 3);

    const std::array<Vu1Light, 3> byHand{
        ambient(0.055F + added, 0.065F + added, 0.075F + added),
        Vu1Light{.type = LightType::Directional,
                 .colour = {0.07F + added, 0.10F + added, 0.18F + added, 1.0F},
                 .direction = moon.direction},
        Vu1Light{.type = LightType::Point, .colour = lamp.colour, .position = lamp.position, .radius = 8.0F}};
    const LightColour skin = coney::graphics::materialFromRgba({200, 160, 120, 255});
    for (const ReferenceVertex& vertex : capsuleFigure(feet)) {
        const LightColour coney = coney::graphics::referenceColour(vertex, block, skin);
        const LightColour expected = coney::graphics::referenceColour(vertex, byHand, skin);
        CHECK(coney.r == Approx(expected.r));
        CHECK(coney.g == Approx(expected.g));
        CHECK(coney.b == Approx(expected.b));
    }
    // The lamp reaches the figure's front: the top of its head is brighter than its feet, which face down and away.
    const std::vector<GsColour> colours = coney::graphics::lightVertices(capsuleFigure(feet), block, skin);
    CHECK(colours.front().r > colours.back().r);
}

TEST_CASE("The light block holds a light's colour, its direction and its position with the radius as given") {
    // Expected: lighting.md#vertex-reference, confirmed (code)
    // 0x00476cc8 queues the RpLight's current colour, its frame's at vector for a directional light and its position
    // with the radius itself (not its inverse) for a point light.
    LightManager manager;
    LightDescriptor lamp;
    lamp.type = LightType::Point;
    lamp.position = Vec3{1.0F, 2.0F, 3.0F};
    lamp.radius = 12.0F;
    lamp.colour = LightColour{0.5F, 0.4F, 0.3F, 1.0F};
    lamp.lights = coney::graphics::kLightsObjects;
    const auto handle = manager.addLight(lamp);
    const Vu1Light queued = coney::graphics::vu1Light(*manager.light(handle));
    CHECK(queued.type == LightType::Point);
    CHECK(queued.radius == 12.0F);
    CHECK(queued.position.z == 3.0F);
    CHECK(queued.colour == manager.light(handle)->current);
    // The world ambient shines with its base plus the brightness: the block takes the current colour.
    const Vu1Light worldAmbient = coney::graphics::vu1Light(manager.record(LightManager::kWorldAmbient));
    CHECK(worldAmbient.colour.r == Approx(40.0F / 255.0F));
}

TEST_CASE("A light of radius 0 and a spot light outside its cone light nothing") {
    // Expected: unverified, #1254
    // Coney's stand-ins for the two open questions of lighting.md (q-zero-radius, q-spot-light): a corona-only light
    // the selection keeps adds nothing, and a spot light lights only inside its cone.
    const Vec3 up{0.0F, 1.0F, 0.0F};
    const std::array<Vu1Light, 1> coronaOnly{point(1.0F, Vec3{0.0F, 1.0F, 0.0F}, 0.0F)};
    CHECK(coney::graphics::lightAtVertex(Vec3{}, up, coronaOnly).r == 0.0F);
    Vu1Light spot = point(1.0F, Vec3{0.0F, 5.0F, 0.0F}, 10.0F);
    spot.type = LightType::Spot;
    spot.direction = Vec3{0.0F, -1.0F, 0.0F};
    spot.coneAngle = 0.5F;
    const std::array<Vu1Light, 1> inside{spot};
    CHECK(coney::graphics::lightAtVertex(Vec3{}, up, inside).r == Approx(0.5F));
    const std::array<Vu1Light, 1> outside{spot};
    CHECK(coney::graphics::lightAtVertex(Vec3{8.0F, 0.0F, 0.0F}, up, outside).r == 0.0F);
}
