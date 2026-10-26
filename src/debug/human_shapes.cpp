// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/human_shapes.h"

#include <cstdint>
#include <vector>

#include "human/climb.h"
#include "human/strike_shapes.h"

namespace coney::debug {

namespace {

// The wireframe's colour when it is not classified: the Debug draw page's collision colour.
constexpr graphics::Rgba kWireColour{80, 220, 255, 255};
// How long a contact's or the climb's normal is drawn, in metres.
constexpr float kNormalLength = 0.5F;

// The bodies: the sphere the physics moves the human with now, the capsule others meet, the ground probe and the climb.
void addBodies(DebugLines& lines, const human::Human& human) {
    const anim::Vec3 feet = human.position();
    // Grounded, the walking sweep's sphere; airborne, the push-out sphere (docs/research/characters.md#walls).
    if (human.airborne() && !human.flying()) {
        const float radius = human.airSphereRadius();
        lines.sphere(anim::add(feet, anim::Vec3{0.0F, 0.0F, radius + human::bodyTuning().footGap}), radius,
                     kAirSphereColour);
    } else {
        lines.sphere(anim::add(feet, anim::Vec3{0.0F, 0.0F, human.walkingSphereCentreHeight()}),
                     human.walkingSphereRadius(), kWalkingSphereColour);
    }
    const human::UprightCapsule capsule = human.uprightCapsule();
    lines.uprightCapsule(capsule.feet, capsule.top, capsule.radius, kCapsuleColour);
    // The ground snap's ray: from kSnapAbove over the feet, kSnapLength straight down (human::Human).
    if (!human.airborne() && !human.flying()) {
        const anim::Vec3 from = anim::add(feet, anim::Vec3{0.0F, 0.0F, human::Human::kSnapAbove});
        lines.add(from, anim::subtract(from, anim::Vec3{0.0F, 0.0F, human::Human::kSnapLength}), kGroundProbeColour);
    }
    // A climb under way: the face it found, its normal and the top behind it.
    if (const auto& climb = human.climb(); climb) {
        lines.cross(climb->face, 0.3F, kClimbColour);
        lines.arrow(climb->face, anim::scale(climb->normal, kNormalLength), kClimbColour);
        const anim::Vec3 top = anim::add(climb->face, anim::Vec3{0.0F, 0.0F, climb->top});
        lines.add(climb->face, top, kClimbColour);
        lines.cross(top, 0.2F, kClimbColour);
    }
}

// The strike shapes switched on, and the spine and head they are tested against (docs/research/combat/index.md
// #moving-strikes); none for a human without a skeleton.
void addStrikes(DebugLines& lines, const human::Human& human) {
    for (const human::PosedShape& shape : human.posedStrikeShapes(false)) {
        lines.sweptSphere(shape.a, shape.b, shape.radius, kStrikeColour);
    }
    for (const human::PosedShape& shape : human.posedStrikeShapes(true)) {
        lines.sweptSphere(shape.a, shape.b, shape.radius, kStruckColour);
    }
}

// The contacts the last walking sweep slid along, each where the sphere meets it now, with its normal.
void addContacts(DebugLines& lines, const human::Human& human) {
    const anim::Vec3 centre = anim::add(human.position(), anim::Vec3{0.0F, 0.0F, human.walkingSphereCentreHeight()});
    for (const anim::Vec3 normal : human.contacts()) {
        const anim::Vec3 point = contactPoint(centre, human.walkingSphereRadius(), normal);
        lines.cross(point, 0.15F, kContactColour);
        lines.arrow(point, anim::scale(normal, kNormalLength), kContactColour);
    }
}

} // namespace

void addHumanShapes(DebugLines& lines, const human::Human& human, const ShapeLayers& layers, anim::Vec3 offset) {
    // Built where the human is, then moved by the offset between its newest and drawn feet (the pose is not blended).
    DebugLines here;
    if (layers.bodies) {
        addBodies(here, human);
    }
    if (layers.strikes) {
        addStrikes(here, human);
    }
    if (layers.contacts) {
        addContacts(here, human);
    }
    lines.appendOffset(here, offset);
}

anim::Vec3 contactPoint(anim::Vec3 centre, float radius, anim::Vec3 normal) {
    if (anim::length(normal) <= 1e-6F) {
        return centre;
    }
    return anim::subtract(centre, anim::scale(anim::normalise(normal), radius));
}

graphics::Rgba wallClassColour(human::WallClass kind) {
    switch (kind) {
    case human::WallClass::Wall:
        return {255, 60, 60, 255};
    case human::WallClass::LowWall:
        return {255, 220, 40, 255};
    case human::WallClass::Floor:
        return {60, 220, 90, 255};
    case human::WallClass::Ceiling:
        return {80, 120, 255, 255};
    case human::WallClass::Disabled:
        return {90, 90, 90, 255};
    case human::WallClass::Degenerate:
        return {255, 0, 255, 255};
    }
    return kWireColour;
}

std::size_t addCollisionWireframe(DebugLines& lines, const raycast::CollisionMesh& mesh, anim::Vec3 centre,
                                  float radius, bool classify, std::size_t maxTriangles) {
    const auto vertices = mesh.vertices();
    const auto triangles = mesh.triangles();
    const float reach = radius * radius;
    std::size_t drawn = 0;
    for (std::size_t index = 0; index < triangles.size() && drawn < maxTriangles; ++index) {
        const raycast::CollisionTriangle& triangle = triangles[index];
        // Near by its first corner, as the plain wireframe chooses (src/platform/play_level_mode.cpp).
        const raycast::Vec3 a = vertices[triangle.vertices[0]];
        const anim::Vec3 first{a.x, a.y, a.z};
        const anim::Vec3 offset = anim::subtract(first, centre);
        if (anim::dot(offset, offset) > reach) {
            continue;
        }
        const graphics::Rgba colour =
            classify ? wallClassColour(human::wallClassOf(mesh, static_cast<std::uint16_t>(index))) : kWireColour;
        const raycast::Vec3 b = vertices[triangle.vertices[1]];
        const raycast::Vec3 c = vertices[triangle.vertices[2]];
        const anim::Vec3 second{b.x, b.y, b.z};
        const anim::Vec3 third{c.x, c.y, c.z};
        lines.add(first, second, colour);
        lines.add(second, third, colour);
        lines.add(third, first, colour);
        ++drawn;
    }
    return drawn;
}

void addHighlight(DebugLines& lines, anim::Vec3 position, float height) {
    const anim::Vec3 top = anim::add(position, anim::Vec3{0.0F, 0.0F, height});
    lines.add(position, top, kHighlightColour);
    // A diamond of four points round the top, joined, and to the top of the upright.
    constexpr float kSize = 0.25F;
    const std::vector<anim::Vec3> corners{
        anim::add(top, anim::Vec3{kSize, 0.0F, kSize}), anim::add(top, anim::Vec3{0.0F, kSize, kSize}),
        anim::add(top, anim::Vec3{-kSize, 0.0F, kSize}), anim::add(top, anim::Vec3{0.0F, -kSize, kSize})};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        lines.add(corners[i], corners[(i + 1) % corners.size()], kHighlightColour);
        lines.add(corners[i], top, kHighlightColour);
    }
    lines.circle(position, 0.6F, {0.0F, 0.0F, 1.0F}, kHighlightColour);
}

} // namespace coney::debug
