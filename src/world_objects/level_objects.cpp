// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/level_objects.h"

namespace coney::world_objects {

namespace {

// The animation set of thrown objects that hit as kind 0 (but a molotov).
constexpr int kLightThrownAnimSet = 5;

} // namespace

bool LevelObjects::humanHit(double object, const ObjectHit& hit) {
    const bool took = takeHumanHit(object, hit);
    // A strike that landed is damage done from where the attacker stands (Strike_Contact's message 6).
    if (took && hit.attacker != kNoObject && world.services != nullptr) {
        world.services->damageDone(hit.attacker, object);
    }
    return took;
}

bool LevelObjects::takeHumanHit(double object, const ObjectHit& hit) {
    if (glass.find(object) != nullptr) {
        return glass.humanHit(object, hit.attacker, world);
    }
    const Door* door = doors.find(object);
    if (door == nullptr) {
        return false;
    }
    // Hitting a break-and-enter door is a break-in at the attacker, whatever the door makes of the hit.
    if (door->objectType == object_type::kBreakAndEnterDoor && world.services != nullptr) {
        world.services->reportCrime(kCrimeBreakIn, hit.attackerAt, hit.attacker);
    }
    return doors.hit(object, hit, world);
}

bool LevelObjects::thrownHit(double object, const ObjectHit& hit) {
    const bool took =
        glass.find(object) != nullptr ? glass.thrownHit(object, hit.attacker, world) : doors.hit(object, hit, world);
    // A thrown object's hit is its thrower's damage (message 6 from the boxes he stands in).
    if (took && hit.attacker != kNoObject && world.services != nullptr) {
        world.services->damageDone(hit.attacker, object);
    }
    return took;
}

std::optional<double> LevelObjects::objectOfTriangle(std::uint32_t triangle) const {
    if (const GlassPane* pane = glass.findByTriangle(triangle)) {
        return pane->handle;
    }
    if (const Door* door = doors.findByTriangle(triangle)) {
        return door->handle;
    }
    return std::nullopt;
}

bool LevelObjects::ghostForCamera(double object) {
    const Door* door = doors.find(object);
    if (door == nullptr || !isDoorKind(door->objectType)) {
        return false;
    }
    markTriangles(world.collision, door->triangles, kCameraGhostBit);
    return true;
}

std::optional<anim::Vec3> LevelObjects::positionOf(double object) const {
    if (const GlassPane* pane = glass.find(object)) {
        return pane->centre;
    }
    if (const Door* door = doors.find(object)) {
        return door->position;
    }
    if (const Door* door = doors.findByLeaf(object)) {
        for (const DoorLeaf& leaf : door->leaves) {
            if (leaf.handle == object) {
                return leaf.position;
            }
        }
    }
    return std::nullopt;
}

void LevelObjects::clear() {
    glass.clearPanes();
    doors.clear();
}

HitKind thrownHitKind(int objectType, int animSet, bool molotov) {
    if (objectType == object_type::kMissionTv) {
        return HitKind::Airborne;
    }
    if (animSet == kLightThrownAnimSet && !molotov) {
        return HitKind::Plain;
    }
    return HitKind::Thrown;
}

HitKind humanHitKind(bool runAttackOrCharge, bool airborne) {
    if (airborne) {
        return HitKind::Airborne;
    }
    return runAttackOrCharge ? HitKind::Charge : HitKind::Plain;
}

} // namespace coney::world_objects
