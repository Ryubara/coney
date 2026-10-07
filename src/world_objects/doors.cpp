// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/doors.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/name_hash.h"
#include "raycast/collision_mesh.h"
#include "world_objects/nav_links.h"

namespace coney::world_objects {

namespace {

// The open and close sounds of a family of door types.
struct SoundPair {
    std::uint32_t open;
    std::uint32_t close;
};
constexpr SoundPair kDefaultSounds{kDefaultOpenSound, kDefaultCloseSound};
constexpr SoundPair kGateSounds{0x43e4b743U, 0x2d88622bU};
constexpr SoundPair kChainSounds{0xca9bdea4U, 0x53928f1eU};
constexpr SoundPair kSteelSounds{0xc09823beU, 0x62b026b4U};
constexpr SoundPair kStripSounds{0xc09823beU, 0xd8831bf0U};
constexpr SoundPair kWoodFenceSounds{0xa75ac8f5U, 0x4954a9d9U};
constexpr SoundPair kSheetSounds{0x34062c42U, 0xb0443866U};

// One swinging door type the set-up names: its leaves and sounds.
struct TypeRow {
    std::string_view name;
    int leaves;
    SoundPair sounds;
};

// The types DoorSwing_SetUpType names (docs/research/objects.md#doors); any other gets no leaves.
constexpr std::array kTypeRows{
    TypeRow{"dyn_door_big_gate", 2, kGateSounds},
    TypeRow{"dyn_door_cemgate", 2, kGateSounds},
    TypeRow{"dyn_door_dblgate", 2, kGateSounds},
    TypeRow{"dyn_door_dblgate_d", 2, kGateSounds},
    TypeRow{"dyn_door_redgate", 2, kGateSounds},
    TypeRow{"dyn_door_chainlnk_a", 1, kChainSounds},
    TypeRow{"dyn_door_chainlnk_pick", 1, kChainSounds},
    TypeRow{"dyn_door_chainlnk_ag", 1, kChainSounds},
    TypeRow{"dyn_door_chainlnk_ap", 1, kChainSounds},
    TypeRow{"dyn_door_chainlnk_ar", 1, kChainSounds},
    TypeRow{"dyn_door_chainlnk_aw", 1, kChainSounds},
    TypeRow{"dyn_door_dblstlwin", 2, kSteelSounds},
    TypeRow{"dyn_door_steel_b", 2, kSteelSounds},
    TypeRow{"dyn_door_stlslot", 2, kSteelSounds},
    TypeRow{"dyn_door_stlwin", 2, kSteelSounds},
    TypeRow{"dyn_door_strip", 2, kStripSounds},
    TypeRow{"dyn_door_dblwoodfnce_xl", 2, kWoodFenceSounds},
    TypeRow{"dyn_door_woodfnce_xl", 1, kWoodFenceSounds},
    TypeRow{"dyn_door_red_fence", 1, kSheetSounds},
    TypeRow{"dyn_door_sheetmtl", 1, kSheetSounds},
    TypeRow{"dyn_door_corr", 1, kDefaultSounds},
    TypeRow{"dyn_door_ornate_single", 1, kDefaultSounds},
    TypeRow{"dyn_door_stall", 1, kDefaultSounds},
    TypeRow{"dyn_door_cabin_a", 2, kDefaultSounds},
    TypeRow{"dyn_door_cabin_b", 2, kDefaultSounds},
    TypeRow{"dyn_door_cabin_c", 2, kDefaultSounds},
    TypeRow{"dyn_door_dclub", 2, kDefaultSounds},
    TypeRow{"dyn_door_liz", 2, kDefaultSounds},
    TypeRow{"dyn_door_ornate", 2, kDefaultSounds},
    TypeRow{"dyn_door_shackdoor", 2, kDefaultSounds},
    TypeRow{"dyn_door_store", 2, kDefaultSounds},
    TypeRow{"dyn_door_storeb", 2, kDefaultSounds},
    TypeRow{"dyn_door_subcan", 2, kDefaultSounds},
    TypeRow{"dyn_door_temple", 2, kDefaultSounds},
    TypeRow{"dyn_door_templedoor", 2, kDefaultSounds},
    TypeRow{"dyn_door_templeshutter", 2, kDefaultSounds},
    TypeRow{"dyn_door_wood", 2, kDefaultSounds},
};

// The types that start pickable, and the ones that retag their links 0x40.
constexpr std::array<std::string_view, 3> kPickableTypes{"dyn_door_chainlnk_pick", "dyn_door_storeb",
                                                         "dyn_door_templedoor"};
constexpr std::array<std::string_view, 2> kMarkingTypes{"dyn_door_dclub", "dyn_door_liz"};

// The barrier classes, and the ones whose initialisers retag the links (all but dyn_door_chain_s).
constexpr std::array<std::string_view, 6> kBarrierClasses{"dyn_door_fence",   "dyn_door_bar_bani", "dyn_door_bnstr",
                                                          "dyn_door_chain_s", "dyn_door_fence_o",  "dyn_door_parapet"};
constexpr std::string_view kUnmarkedBarrierClass = "dyn_door_chain_s";
constexpr std::string_view kSwingingClass = "dyn_door_swinging";

// The prefixes of a door type and of its leaves.
constexpr std::string_view kDoorPrefix = "dyn_door_";
constexpr std::string_view kLeafPrefix = "dyn_dr_";

// The breakable types' names.
constexpr std::string_view kStore = "dyn_door_store";
constexpr std::string_view kLiz = "dyn_door_liz";
constexpr std::string_view kDclub = "dyn_door_dclub";
constexpr std::string_view kStall = "dyn_door_stall";
constexpr std::string_view kVargas = "dyn_door_vargas";
constexpr std::string_view kWallA = "dyn_door_wall_a";
constexpr std::array<std::string_view, 2> kBoardlessBarriers{"dyn_door_wall_a", "dyn_door_wall_b"};
// The boards a broken barrier throws.
constexpr std::array<std::string_view, 3> kBoards{"dyn_wooddmg_a", "dyn_wooddmg_b", "dyn_wooddmg_a"};

// dyn_door_liz's model stages (percent of its hitpoints) and the material each stage's sound pairs with CONCRETE (the
// fourth is the door's own, marked by 0); dyn_door_dclub's one stage.
constexpr std::array<int, 4> kLizStages{80, 60, 40, 20};
constexpr std::array<std::uint8_t, 4> kLizStageSounds{35, 36, 106, 0};
constexpr int kDclubStage = 50;
constexpr int kPercent = 100;

// Hitpoints a door type no CfgObj named gets (Coney's stand-in): the disc's value for most types.
constexpr int kStandInHitpoints = 100;
// A destroyed door's interval after message 0x15, and the interval at which it hits itself.
constexpr int kDestroyInterval = 2;
constexpr int kDestroyHitInterval = 3;
// A cabin's wreck piece by its letter.
constexpr std::array<std::string_view, 3> kCabinWreck{"dyn_cabin_aa", "dyn_cabin_bb", "dyn_cabin_cc"};
// Picks abandoned at a door before the third reports a break-in.
constexpr int kAbandonsReported = 3;
// The tint's alpha byte.
constexpr std::uint32_t kTintAlpha = 0xffU;
// Triangle flag bits the doors add (docs/research/collision.md#triangles).
constexpr std::uint16_t kTriangleDoorBit = 0x400;
constexpr std::uint16_t kTriangleBreakableBit = 0x40;

// Whether `list` holds `name`.
template <std::size_t N> bool listed(const std::array<std::string_view, N>& list, std::string_view name) {
    return std::ranges::find(list, name) != list.end();
}

// The default leaf model of `type`: dyn_dr_ and the name after dyn_door_.
std::string leafModelOf(std::string_view type) {
    if (type.starts_with(kDoorPrefix)) {
        return std::string(kLeafPrefix) + std::string(type.substr(kDoorPrefix.size()));
    }
    return std::string(type);
}

// The model a door draws when the Object List has none for its own (Coney's stand-in): dyn_dr_ and the name after
// dyn_door_, less a leading "dbl" (dyn_door_fence draws dyn_dr_fence, dyn_door_dblwoodfnce_xl's leaves
// dyn_dr_woodfnce_xl).
std::string fallbackModelOf(std::string_view type) {
    std::string model = leafModelOf(type);
    constexpr std::string_view kDouble = "dbl";
    if (model.starts_with(kLeafPrefix) && std::string_view(model).substr(kLeafPrefix.size()).starts_with(kDouble)) {
        model.erase(kLeafPrefix.size(), kDouble.size());
    }
    return model;
}

// One 60 Hz tick of a leaf: a new target starts a swing from the last one (snapping to it first), then the pose is
// the swing's slerp over the leaf's update interval, at a constant rate.
// @orig 0x003fb5a0 SubSwingingDoor_Update (unknown)
void stepLeaf(DoorLeaf& leaf) {
    if (leaf.turned) {
        leaf.from = leaf.to;
        leaf.to = leaf.target;
        leaf.swingTicks = 0;
        leaf.turned = false;
    } else if (leaf.swingTicks < kLeafSwingTicks) {
        ++leaf.swingTicks;
    }
    const float t = static_cast<float>(leaf.swingTicks) / static_cast<float>(kLeafSwingTicks);
    leaf.rotation = anim::slerp(leaf.from, leaf.to, t);
}

// The two loose pieces a breaking type leaves.
std::array<std::string_view, 2> wreckOf(std::string_view type) {
    if (type == kLiz) {
        return {"dyn_dre_liz_e", "dyn_dre_liz_f"};
    }
    if (type == kStall) {
        return {"dyn_dre_stall_a", "dyn_dre_stall_b"};
    }
    if (type == kDclub) {
        return {"dyn_dre_dclub_c", "dyn_dre_dclub_d"};
    }
    if (type.starts_with("dyn_door_cabin_") && type.size() == std::string_view("dyn_door_cabin_a").size()) {
        const auto letter = static_cast<std::size_t>(type.back() - 'a');
        if (letter < kCabinWreck.size()) {
            return {kCabinWreck.at(letter), kCabinWreck.at(letter)};
        }
    }
    return {};
}

// How a swinging type takes hits.
DoorBreaking breakingOf(std::string_view type) {
    if (type == kStore) {
        return DoorBreaking::Store;
    }
    if (type.starts_with("dyn_door_cabin_")) {
        return DoorBreaking::Cabin;
    }
    if (type == kLiz || type == kDclub || type == kStall) {
        return DoorBreaking::Splinters;
    }
    return DoorBreaking::None;
}

// Plays the material pair `a` × `b` at `at`.
void playPair(ObjectWorld& world, std::uint8_t a, std::uint8_t b, anim::Vec3 at) {
    if (world.services != nullptr) {
        world.services->playMaterialPair(a, b, at);
    }
}

// The dust and the splinters of a splintering door's or a barrier's hit.
void dustAndSplinters(ObjectWorld& world, anim::Vec3 at) {
    if (world.services == nullptr) {
        return;
    }
    for (const float radius : kDustRadii) {
        world.services->dust(at, radius);
    }
    world.services->splinters(at, kSplinters);
}

} // namespace

DoorTypeSetup swingingDoorSetup(std::string_view type) {
    DoorTypeSetup setup;
    if (const auto row = std::ranges::find(kTypeRows, type, &TypeRow::name); row != kTypeRows.end()) {
        setup.leaves = row->leaves;
        setup.openSound = row->sounds.open;
        setup.closeSound = row->sounds.close;
    }
    setup.pickable = listed(kPickableTypes, type);
    setup.marksLinks = listed(kMarkingTypes, type);
    setup.breaking = breakingOf(type);
    setup.cabin = setup.breaking == DoorBreaking::Cabin;
    setup.leafModel = type == "dyn_door_ornate_single" ? std::string("dyn_dr_ornate") : leafModelOf(type);
    setup.secondLeafModel = type == "dyn_door_stlslot" ? std::string("dyn_dr_stlslot") : setup.leafModel;
    setup.wreck = wreckOf(type);
    return setup;
}

DoorClass doorClassOf(std::string_view className) {
    if (className == kSwingingClass) {
        return DoorClass::Swinging;
    }
    return listed(kBarrierClasses, className) ? DoorClass::Barrier : DoorClass::Other;
}

const Door& Doors::spawn(double handle, const DoorSpawn& spawn, const ObjectTypeInfo* info, const HandleSource& handles,
                         ObjectWorld& world) {
    NavLinks links(world.paths);
    Door door;
    door.handle = handle;
    door.type = spawn.type;
    door.position = spawn.position;
    door.rotation = anim::normalise(spawn.rotation);
    door.triangles = spawn.triangles;
    door.number = spawn.number;
    door.doorClass = info != nullptr ? doorClassOf(info->className) : DoorClass::Swinging;
    door.hitpoints = info != nullptr ? info->hitpoints : kStandInHitpoints;
    door.maxHitpoints = door.hitpoints;
    door.material = info != nullptr ? info->material : 0;
    door.objectType = info != nullptr ? info->objectType : 0;
    door.leafWidth = info != nullptr ? info->leafWidth : 0.0F;

    if (door.doorClass == DoorClass::Swinging) {
        door.setup = swingingDoorSetup(door.type);
        // The leaves: the left at the door, the right 2w along its x axis, turned 180°.
        for (int leaf = 0; leaf < door.setup.leaves; ++leaf) {
            DoorLeaf made;
            made.handle = handles ? handles() : kNoObject;
            made.model = leaf == 0 ? door.setup.leafModel : door.setup.secondLeafModel;
            made.position =
                leaf == 0
                    ? door.position
                    : anim::add(door.position, rotate(door.rotation, anim::Vec3{-2.0F * door.leafWidth, 0.0F, 0.0F}));
            made.base = leaf == 0 ? door.rotation : turnAboutVertical(door.rotation, 180.0F);
            made.target = made.base;
            made.from = made.base;
            made.to = made.base;
            made.rotation = made.base;
            door.leaves.push_back(std::move(made));
        }
        // Its triangles: two-sided, 0x400, 0x40 with hitpoints, the type's material; a cabin's also 0x800.
        std::uint16_t flags = raycast::kTriangleTwoSided | kTriangleDoorBit;
        if (door.hitpoints != 0) {
            flags |= kTriangleBreakableBit;
        }
        if (door.setup.cabin) {
            flags |= raycast::kTriangleTestableDisabled;
            door.state = door_state::kCabinRest;
            door.interval = kCabinInterval;
            door.countdown = kCabinInterval;
        }
        markTriangles(world.collision, door.triangles, flags, door.material);
        if (door.setup.marksLinks) {
            links.setKindByNumber(door.number, link_kind::kBreakable);
        }
        if (door.setup.pickable) {
            pickableOn(door, world);
        }
    } else if (door.doorClass == DoorClass::Barrier) {
        markTriangles(world.collision, door.triangles,
                      raycast::kTriangleTwoSided | kTriangleBreakableBit | kTriangleDoorBit);
        if (info != nullptr && info->className != kUnmarkedBarrierClass) {
            links.setKindByNumber(door.number, link_kind::kBreakable);
        }
        door.interval = kBarrierInterval;
        door.countdown = kBarrierInterval;
    }
    m_doors.push_back(std::move(door));
    return m_doors.back();
}

void Doors::command(double handle, int command, ObjectWorld& world) {
    if (Door* door = findMutable(handle)) {
        runCommand(*door, command, world);
    }
}

void Doors::runCommand(Door& door, int command, ObjectWorld& world) {
    if (door.doorClass != DoorClass::Swinging || door.ended) {
        return;
    }
    NavLinks links(world.paths);
    switch (command) {
    case door_command::kOpen:
    case door_command::kOpenToo:
        if (!door.pickable) {
            resetLeaves(door);
            door.angle = kOpenAngle;
            swingTo(door);
            startOpening(door, world);
        }
        return;
    case door_command::kClose:
    case door_command::kCloseToo:
        // From closed only a reset; otherwise swing back with the close sound. Then as command 6.
        door.angle = 0.0F;
        if (door.state == door_state::kClosed) {
            resetLeaves(door);
        } else {
            swingTo(door);
            playSound(door, door.setup.closeSound, world);
        }
        door.state = door_state::kClosing;
        runCommand(door, door_command::kEnableCollision, world);
        return;
    case door_command::kEnableCollision:
        setTrianglesEnabled(world.collision, door.triangles, true);
        if (world.services != nullptr) {
            world.services->setBody(door.handle, true);
        }
        links.closeByNumber(door.number);
        return;
    case door_command::kDisableCollision:
        links.openByNumber(door.number);
        setTrianglesEnabled(world.collision, door.triangles, false);
        if (world.services != nullptr) {
            world.services->setBody(door.handle, false);
        }
        return;
    case door_command::kPickableOn:
        pickableOn(door, world);
        return;
    case door_command::kPickableOff:
    case door_command::kPickableOffToo:
        pickableOff(door, world);
        return;
    default:
        return;
    }
}

void Doors::setPickable(double handle, bool pickable, ObjectWorld& world) {
    if (!pickable && world.services != nullptr && find(handle) != nullptr) {
        world.services->stopHumansTargeting(handle);
    }
    command(handle, pickable ? door_command::kPickableOn : door_command::kPickableOffToo, world);
}

void Doors::pickableOn(Door& door, ObjectWorld& world) {
    // Only a door that is (nearly) closed takes the glint.
    if (door.angle >= kPickableMaxAngle) {
        return;
    }
    door.pickable = true;
    door.glint = true;
    door.glintAt = anim::add(door.position, rotate(door.rotation, anim::Vec3{-door.leafWidth, 0.0F, kGlintHeight}));
    if (world.services != nullptr) {
        world.services->setBody(door.handle, false);
    }
}

void Doors::pickableOff(Door& door, ObjectWorld& world) {
    door.pickable = false;
    door.glint = false;
    if (world.services != nullptr) {
        world.services->setBody(door.handle, true);
    }
}

void Doors::openToDegree(double handle, float degrees, ObjectWorld& world) {
    Door* door = findMutable(handle);
    if (door == nullptr || door->doorClass != DoorClass::Swinging || door->ended) {
        return;
    }
    if (door->pickable) {
        door->keptAngle = degrees;
        return;
    }
    door->angle = degrees;
    swingTo(*door);
    if (door->state == door_state::kClosed) {
        startOpening(*door, world);
    } else {
        playSound(*door, door->setup.openSound, world);
    }
}

void Doors::openBy(double handle, anim::Vec3 humanAt, ObjectWorld& world) {
    if (Door* door = findMutable(handle)) {
        openAway(*door, humanAt, world);
    }
}

void Doors::openAway(Door& door, anim::Vec3 humanAt, ObjectWorld& world) {
    if (door.doorClass != DoorClass::Swinging || door.ended || door.pickable || door.state != door_state::kClosed) {
        return;
    }
    resetLeaves(door);
    if (door.keptAngle != 0.0F) {
        door.angle = door.keptAngle;
    } else {
        // Away from the human: the sign of the door's turned axis against the human-to-door direction.
        const anim::Vec3 axis = rotate(door.rotation, anim::Vec3{0.0F, 1.0F, 0.0F});
        const float side = anim::dot(axis, anim::subtract(door.position, humanAt));
        door.angle = side >= 0.0F ? kOpenAngle : -kOpenAngle;
    }
    swingTo(door);
    door.pickable = false;
    startOpening(door, world);
}

bool Doors::isOpen(double handle) const {
    const Door* door = find(handle);
    return door != nullptr && door->doorClass == DoorClass::Swinging && door->state == door_state::kOpen;
}

void Doors::startOpening(Door& door, ObjectWorld& world) {
    door.state = door_state::kOpening;
    playSound(door, door.setup.openSound, world);
    // Opening reschedules the update for the next tick.
    door.countdown = 1;
}

void Doors::swingTo(Door& door) {
    // The left leaf turns by the angle from its closed pose, the right by the negated angle from its turned base.
    for (std::size_t leaf = 0; leaf < door.leaves.size(); ++leaf) {
        DoorLeaf& made = door.leaves[leaf];
        made.target = turnAboutVertical(made.base, leaf == 0 ? door.angle : -door.angle);
        made.turned = true;
    }
}

void Doors::resetLeaves(Door& door) {
    for (DoorLeaf& leaf : door.leaves) {
        leaf.target = leaf.base;
        leaf.from = leaf.base;
        leaf.to = leaf.base;
        leaf.rotation = leaf.base;
        leaf.turned = false;
    }
}

void Doors::playSound(const Door& door, std::uint32_t sound, ObjectWorld& world) {
    if (world.services != nullptr) {
        world.services->playSound(sound, door.position);
    }
}

bool Doors::hit(double handle, const ObjectHit& hit, ObjectWorld& world) {
    Door* door = findMutable(handle);
    if (door == nullptr || door->ended) {
        return false;
    }
    if (door->doorClass == DoorClass::Barrier) {
        if (!door->hittable || door->hidden || door->hitpoints <= 0) {
            return false;
        }
        hitBarrier(*door, hit, world);
        return true;
    }
    if (door->doorClass != DoorClass::Swinging) {
        return false;
    }
    switch (door->setup.breaking) {
    case DoorBreaking::Store:
        hitStore(*door, hit, world);
        return true;
    case DoorBreaking::Cabin:
        hitCabin(*door, hit, world);
        return true;
    case DoorBreaking::Splinters:
        hitSplinters(*door, hit, world);
        return true;
    case DoorBreaking::None:
        return false;
    }
    return false;
}

void Doors::hitStore(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    // Only the hit that takes its last hitpoint: the sounds, its triangles off, and it swings open away from them.
    if (door.hitpoints <= 0) {
        return;
    }
    door.hitpoints -= hitDamage(hit.kind);
    if (door.hitpoints > 0) {
        return;
    }
    playSound(door, kDoorHitSound, world);
    playPair(world, material::kGlass, material::kGlass, door.position);
    setTrianglesEnabled(world.collision, door.triangles, false);
    openAway(door, hit.attackerAt, world);
}

void Doors::hitCabin(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    playSound(door, kDoorHitSound, world);
    door.hitpoints -= hitDamage(hit.kind);
    if (door.hitpoints <= 0) {
        wreck(door, world);
        return;
    }
    // Below 7 both leaves are broken and one, at random, takes its next model with a burst.
    if (door.hitpoints < kCabinLeafBreak && !door.leavesBroken && !door.leaves.empty()) {
        door.leavesBroken = true;
        for (DoorLeaf& leaf : door.leaves) {
            leaf.broken = true;
        }
        const DoorLeaf& chosen =
            door.leaves.at(static_cast<std::size_t>(randomBelow(world.random, static_cast<int>(door.leaves.size()))));
        if (world.services != nullptr) {
            world.services->nextModel(chosen.handle);
            world.services->burst(chosen.position);
        }
    }
}

void Doors::hitSplinters(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    dustAndSplinters(world, hit.point);
    playPair(world, door.material, material::kConcrete, hit.point);
    door.hitpoints -= hitDamage(hit.kind);
    // dyn_door_stall breaks on its first hit.
    if (door.hitpoints <= 0 || door.type == kStall) {
        NavLinks(world.paths).openByNumber(door.number);
        door.tint &= ~kTintAlpha;
        dustAndSplinters(world, door.position);
        wreck(door, world);
        return;
    }
    // The model stages: dyn_door_liz at 80, 60, 40 and 20 %, dyn_door_dclub at 50 %.
    const auto reached = [&door](int percent) { return door.hitpoints * kPercent <= percent * door.maxHitpoints; };
    if (door.type == kLiz) {
        while (door.modelStage < static_cast<int>(kLizStages.size()) &&
               reached(kLizStages.at(static_cast<std::size_t>(door.modelStage)))) {
            const std::uint8_t sound = kLizStageSounds.at(static_cast<std::size_t>(door.modelStage));
            playPair(world, sound != 0 ? sound : door.material, material::kConcrete, door.position);
            if (world.services != nullptr) {
                world.services->nextModel(door.handle);
            }
            ++door.modelStage;
        }
    } else if (door.type == kDclub && door.modelStage == 0 && reached(kDclubStage)) {
        if (world.services != nullptr) {
            world.services->nextModel(door.handle);
        }
        door.modelStage = 1;
    }
}

void Doors::wreck(Door& door, ObjectWorld& world) {
    setTrianglesEnabled(world.collision, door.triangles, false);
    if (world.services != nullptr) {
        for (const std::string_view piece : door.setup.wreck) {
            if (!piece.empty()) {
                world.services->spawnObject(piece, door.position, door.rotation);
            }
        }
    }
    door.state = door_state::kBroken;
}

void Doors::hitBarrier(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    // Every hit: its damage, dust and splinters; one that leaves hitpoints does nothing more.
    door.hitpoints -= hitDamage(hit.kind);
    dustAndSplinters(world, hit.point);
    if (door.hitpoints > 0) {
        return;
    }
    // Broken: the triangles off and the links open, then by class: dyn_door_wall_a its damaged model and no body,
    // dyn_door_wall_b nothing more, the others three boards.
    setTrianglesEnabled(world.collision, door.triangles, false);
    NavLinks(world.paths).openByNumber(door.number);
    if (world.services != nullptr) {
        if (door.type == kWallA) {
            world.services->setModel(door.handle, kBarrierDamagedModel);
            world.services->setBody(door.handle, false);
        } else if (!listed(kBoardlessBarriers, door.type)) {
            for (const std::string_view board : kBoards) {
                world.services->spawnObject(board, door.position, door.rotation);
            }
        }
    }
    playPair(world, door.material, door.material, hit.point);
    // dyn_door_vargas destroys its second object and takes its broken model; the others are marked broken and hide,
    // to be removed at their next update.
    if (door.type == kVargas) {
        if (world.services != nullptr) {
            world.services->destroyObject(door.secondObject);
            world.services->setModel(door.handle, kVargasBrokenModel);
        }
        return;
    }
    door.broken = true;
    door.hidden = true;
}

void Doors::destroy(double handle) {
    Door* door = findMutable(handle);
    if (door == nullptr || door->doorClass != DoorClass::Swinging || door->ended) {
        return;
    }
    door->destroying = true;
    door->interval = kDestroyInterval;
    door->countdown = kDestroyInterval;
}

std::size_t Doors::destroyInRadius(anim::Vec3 centre, float radius) {
    std::size_t count = 0;
    for (const Door& door : m_doors) {
        if (anim::distance(door.position, centre) <= radius) {
            destroy(door.handle);
            ++count;
        }
    }
    return count;
}

void Doors::setHitpoints(double handle, int hitpoints) {
    if (Door* door = findMutable(handle)) {
        door->hitpoints = hitpoints;
    }
}

int Doors::hitpoints(double handle) const {
    const Door* door = find(handle);
    return door != nullptr ? door->hitpoints : 0;
}

void Doors::setHittable(double handle, bool hittable) {
    if (Door* door = findMutable(handle); door != nullptr && door->doorClass == DoorClass::Barrier) {
        door->hittable = hittable;
    }
}

void Doors::tick(ObjectWorld& world) {
    for (Door& door : m_doors) {
        for (DoorLeaf& leaf : door.leaves) {
            stepLeaf(leaf);
        }
        // A barrier's update only asks whether it is broken; a broken one is removed (WorldObject_Update).
        if (door.doorClass == DoorClass::Barrier && !door.removed && --door.countdown <= 0) {
            door.countdown = door.interval;
            if (door.broken) {
                door.removed = true;
                m_removed.push_back(door.handle);
            }
            continue;
        }
        if (door.ended || door.doorClass != DoorClass::Swinging) {
            continue;
        }
        if (--door.countdown > 0) {
            continue;
        }
        // Rescheduled first, so an update that starts a swing can ask for the next tick.
        door.countdown = door.interval;
        update(door, world);
    }
}

void Doors::update(Door& door, ObjectWorld& world) {
    // A destroyed door raises its interval each update and hits itself on reaching 3.
    if (door.destroying) {
        ++door.interval;
        if (door.interval == kDestroyHitInterval) {
            const ObjectHit self{.attacker = door.handle,
                                 .kind = HitKind::Plain,
                                 .point = door.position,
                                 .direction = {},
                                 .attackerAt = door.position};
            switch (door.setup.breaking) {
            case DoorBreaking::Store:
                hitStore(door, self, world);
                break;
            case DoorBreaking::Cabin:
                hitCabin(door, self, world);
                break;
            case DoorBreaking::Splinters:
                hitSplinters(door, self, world);
                break;
            case DoorBreaking::None:
                break;
            }
        }
    }
    switch (door.state) {
    case door_state::kClosing:
    case door_state::kClosing + 1:
        ++door.state;
        if (door.state == door_state::kClosed) {
            resetLeaves(door);
        }
        return;
    case door_state::kOpening:
        door.state = door_state::kSwung;
        return;
    case door_state::kSwung:
        runCommand(door, door_command::kDisableCollision, world);
        door.state = door_state::kOpen;
        return;
    case door_state::kBroken:
        door.ended = true;
        return;
    default:
        return;
    }
}

void Doors::lockPickSucceeded(double handle, anim::Vec3 humanAt, ObjectWorld& world) {
    Door* door = findMutable(handle);
    if (door == nullptr) {
        return;
    }
    runCommand(*door, door_command::kPickableOff, world);
    openAway(*door, humanAt, world);
}

bool Doors::lockPickAbandoned(double handle) {
    Door* door = findMutable(handle);
    if (door == nullptr || door->doorClass != DoorClass::Swinging) {
        return false;
    }
    ++door->abandonedPicks;
    return door->abandonedPicks == kAbandonsReported;
}

const Door* Doors::find(double handle) const {
    const auto found = std::ranges::find(m_doors, handle, &Door::handle);
    return found == m_doors.end() ? nullptr : &*found;
}

Door* Doors::findMutable(double handle) {
    const auto found = std::ranges::find(m_doors, handle, &Door::handle);
    return found == m_doors.end() ? nullptr : &*found;
}

const Door* Doors::findByTriangle(std::uint32_t triangle) const {
    const auto found = std::ranges::find_if(m_doors, [triangle](const Door& door) {
        return door.triangles[0] == triangle || door.triangles[1] == triangle;
    });
    return found == m_doors.end() ? nullptr : &*found;
}

const Door* Doors::findByLeaf(double leaf) const {
    const auto found = std::ranges::find_if(m_doors, [leaf](const Door& door) {
        return std::ranges::find(door.leaves, leaf, &DoorLeaf::handle) != door.leaves.end();
    });
    return found == m_doors.end() ? nullptr : &*found;
}

std::vector<DoorDraw> doorDraws(const Doors& doors) {
    std::vector<DoorDraw> draws;
    for (const Door& door : doors.doors()) {
        if (door.ended || door.hidden || door.removed) {
            continue;
        }
        if (door.doorClass == DoorClass::Swinging) {
            for (const DoorLeaf& leaf : door.leaves) {
                draws.push_back(DoorDraw{.handle = leaf.handle,
                                         .modelHash = crc32(leaf.model),
                                         .fallbackHash = crc32(fallbackModelOf(door.type)),
                                         .position = leaf.position,
                                         .rotation = leaf.rotation,
                                         .tint = door.tint});
            }
            continue;
        }
        // dyn_door_vargas swaps to its broken model at the end (a broken barrier of another class is hidden).
        std::uint32_t model = crc32(door.type);
        if (door.doorClass == DoorClass::Barrier && door.type == kVargas && door.hitpoints <= 0) {
            model = kVargasBrokenModel;
        }
        draws.push_back(DoorDraw{.handle = door.handle,
                                 .modelHash = model,
                                 .fallbackHash = crc32(fallbackModelOf(door.type)),
                                 .position = door.position,
                                 .rotation = door.rotation,
                                 .tint = door.tint});
    }
    return draws;
}

} // namespace coney::world_objects
