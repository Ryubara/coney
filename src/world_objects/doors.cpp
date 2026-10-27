// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/doors.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
constexpr std::string_view kSlidingClass = "dyn_door_sliding";

// One sliding door type SlidingDoor_SpawnLeaves names (`0x003f4c80`): its leaves' types, whether its axis byte goes
// one further (`dyn_door_fence_s`, `dyn_door_pol`), whether it plays the roll-up's sound (else kSlidingDoorSound;
// the portcullis has its own) and whether its second leaf stands 2w along the door's -z instead of its -x (the lift's
// `dyn_door_ele_c`).
struct SlidingRow {
    std::string_view name;
    std::string_view leaf;
    std::string_view secondLeaf;
    bool axisOneMore;
    bool rollupSound;
    bool secondBelow = false;
};
constexpr std::string_view kPortcullis = "dyn_door_portcullis";
// The lift's two-leaf door, the one type whose leaves keep their set-up poses through the commands.
constexpr std::string_view kLiftPair = "dyn_door_ele_c";
constexpr std::array kSlidingRows{
    SlidingRow{"dyn_door_rollup", "dyn_dr_rollup", "", false, true},
    SlidingRow{kPortcullis, "dyn_dr_portcullis", "dyn_dr_portcullis", false, false},
    SlidingRow{"dyn_door_fence_r", "dyn_dr_fence_r", "dyn_dr_fence_r", false, false},
    SlidingRow{"dyn_door_fence_s", "dyn_dr_fence_s", "dyn_dr_fence_s", true, false},
    SlidingRow{"dyn_door_ele", "dyn_dr_ele", "", false, true},
    SlidingRow{kLiftPair, "dyn_dr_ele_c", "dyn_dr_ele_c", false, true, true},
    SlidingRow{"dyn_door_pol", "dyn_dr_pol", "", true, false},
    SlidingRow{"dyn_door_subway", "dyn_dr_subway", "dyn_dr_subway", false, false},
    SlidingRow{"dyn_door_spook", "dyn_dr_spook_a", "dyn_dr_spook_b", false, true},
};
// The portcullis' open and close sound.
constexpr std::uint32_t kPortcullisSound = 0x8a2b8511U;

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
// @coney-only a lookup in Coney's type lists
template <std::size_t N> bool listed(const std::array<std::string_view, N>& list, std::string_view name) {
    return std::ranges::find(list, name) != list.end();
}

// The default leaf model of `type`: dyn_dr_ and the name after dyn_door_.
// The leaf names DoorSwing_SetUpType spawns, by Coney's rule instead of one string per type
// (docs/research/objects.md#leaves).
// @orig 0x003f80f0 DoorSwing_SetUpType (unknown)
std::string leafModelOf(std::string_view type) {
    // Stand-in (#707): the rule names some leaves the original's strings do not: dyn_door_chainlnk_pick's leaf is
    // dyn_dr_chainlnk_a there (the rule's dyn_dr_chainlnk_pick is no model), and the doubles' leaves drop the "dbl"
    // (drawn right through fallbackModelOf).
    if (type.starts_with(kDoorPrefix)) {
        return std::string(kLeafPrefix) + std::string(type.substr(kDoorPrefix.size()));
    }
    return std::string(type);
}

// The model a door draws when the Object List has none for its own (Coney's stand-in): dyn_dr_ and the name after
// dyn_door_, less a leading "dbl" (dyn_door_fence draws dyn_dr_fence, dyn_door_dblwoodfnce_xl's leaves
// dyn_dr_woodfnce_xl).
// @coney-only a stand-in model for a type the Object List has no model for
std::string fallbackModelOf(std::string_view type) {
    std::string model = leafModelOf(type);
    constexpr std::string_view kDouble = "dbl";
    if (model.starts_with(kLeafPrefix) && std::string_view(model).substr(kLeafPrefix.size()).starts_with(kDouble)) {
        model.erase(kLeafPrefix.size(), kDouble.size());
    }
    return model;
}

// The unit direction axis byte `axis` names in a sliding leaf's frame: 0 up, 1 down, 2 along +x, 3 along -x.
// @orig 0x003f4950 SubSlidingDoor_PlaceLeaf (unknown)
anim::Vec3 slideDirection(std::uint8_t axis) {
    switch (axis) {
    case 0:
        return anim::Vec3{0.0F, 0.0F, 1.0F};
    case 1:
        return anim::Vec3{0.0F, 0.0F, -1.0F};
    case 2:
        return anim::Vec3{1.0F, 0.0F, 0.0F};
    case 3:
        return anim::Vec3{-1.0F, 0.0F, 0.0F};
    default:
        return anim::Vec3{};
    }
}

// One 60 Hz tick of a sliding door's leaf: on its update (every kSlideStepTicks while it slides) it moves `step`
// toward the open `length` or back to 0, as the original tests it (a step is still taken while the offset has not
// passed the end, so it may overshoot once before it settles there), and rests on reaching it.
// @orig 0x003f5a68 SubSlidingDoor_Update (unknown)
void stepSlidingLeaf(SlidingLeaf& leaf, float length, float step) {
    if (leaf.motion == slide_motion::kRest || --leaf.countdown > 0) {
        return;
    }
    leaf.countdown = kSlideStepTicks;
    bool done = false;
    if (leaf.motion == slide_motion::kOpening) {
        if (leaf.offset <= length) {
            leaf.offset += step;
        } else {
            leaf.offset = length;
            done = true;
        }
    } else if (leaf.motion == slide_motion::kClosing) {
        if (leaf.offset >= 0.0F) {
            leaf.offset -= step;
        } else {
            leaf.offset = 0.0F;
            done = true;
        }
    }
    if (done) {
        leaf.motion = slide_motion::kRest;
        leaf.countdown = kSlideRestTicks;
    }
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
// The pieces DoorSwing_Hit (the cabins) and DoorSwing_HitBreakable (liz, stall, dclub) spawn at the break
// (docs/research/objects.md#door-break).
// @orig 0x003f9ad0 DoorSwing_Hit (unknown)
// @orig 0x003fa438 DoorSwing_HitBreakable (unknown)
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
// The message handler's choice of hit (DoorSwing_HitBreakable for liz, stall and dclub, else DoorSwing_Hit) and
// DoorSwing_Hit's by type (the store, the cabins; docs/research/objects.md#door-break).
// @orig 0x003fb8d0 DynDoorSwinging_OnMessage (unknown)
// @orig 0x003f9ad0 DoorSwing_Hit (unknown)
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
// @coney-only the call into Coney's object services
void playPair(ObjectWorld& world, std::uint8_t a, std::uint8_t b, anim::Vec3 at) {
    if (world.services != nullptr) {
        world.services->playMaterialPair(a, b, at);
    }
}

// The dust and the splinters of a splintering door's or a barrier's hit.
// The dust (two radii) and the 20 splinters both hits make (docs/research/objects.md#door-break,
// docs/research/objects.md#barriers).
// @orig 0x003fa438 DoorSwing_HitBreakable (unknown)
// @orig 0x003b2180 DoorFence_OnHit (unknown)
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
    // Stand-in (#707): dyn_door_stlslot's first leaf is dyn_dr_steel_b in the original; Coney makes both
    // dyn_dr_stlslot.
    setup.secondLeafModel = type == "dyn_door_stlslot" ? std::string("dyn_dr_stlslot") : setup.leafModel;
    setup.wreck = wreckOf(type);
    return setup;
}

// The kind of door a CfgObj class name makes.
// @coney-only Coney's class lookup; the original makes the class's own object by its name
DoorClass doorClassOf(std::string_view className) {
    if (className == kSwingingClass) {
        return DoorClass::Swinging;
    }
    if (className == kSlidingClass) {
        return DoorClass::Sliding;
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
        markTriangles(world, door.triangles, flags, door.material);
        if (door.setup.marksLinks) {
            links.setKindByNumber(door.number, link_kind::kBreakable);
        }
        if (door.setup.pickable) {
            pickableOn(door, world);
        }
    } else if (door.doorClass == DoorClass::Barrier) {
        markTriangles(world, door.triangles, raycast::kTriangleTwoSided | kTriangleBreakableBit | kTriangleDoorBit);
        if (info != nullptr && info->className != kUnmarkedBarrierClass) {
            links.setKindByNumber(door.number, link_kind::kBreakable);
        }
        door.interval = kBarrierInterval;
        door.countdown = kBarrierInterval;
    } else if (door.doorClass == DoorClass::Sliding) {
        setUpSliding(door, info, handles, world);
    }
    m_doors.push_back(std::move(door));
    return m_doors.back();
}

void Doors::setUpSliding(Door& door, const ObjectTypeInfo* info, const HandleSource& handles, ObjectWorld& world) {
    // The slide: property 7's length along the leaf's x axis (axis byte 2), or with none property 8's up its z axis
    // (axis byte 0); the step twice property 6, taken every kSlideStepTicks while it slides.
    const float lengthX = info != nullptr ? info->property7 : 0.0F;
    door.slideLength = lengthX;
    door.slideAxis = 2;
    if (std::fabs(lengthX) == 0.0F) {
        door.slideLength = info != nullptr ? info->property8 : 0.0F;
        door.slideAxis = 0;
    }
    door.slideStep = info != nullptr ? info->property6 * 2.0F : 0.0F;
    // The leaves by type name: the first at the door's place, a second 2w away.
    const auto row = std::ranges::find(kSlidingRows, door.type, &SlidingRow::name);
    if (row != kSlidingRows.end()) {
        if (row->axisOneMore) {
            ++door.slideAxis;
        }
        std::uint32_t sound = row->rollupSound ? kRollupDoorSound : kSlidingDoorSound;
        if (row->name == kPortcullis) {
            sound = kPortcullisSound;
        }
        door.slideOpenSound = sound;
        door.slideCloseSound = sound;
        for (const std::string_view model : {row->leaf, row->secondLeaf}) {
            if (model.empty()) {
                continue;
            }
            const bool second = !door.slidingLeaves.empty();
            SlidingLeaf leaf;
            leaf.handle = handles ? handles() : kNoObject;
            leaf.model = std::string(model);
            // A second leaf stands 2w away from the first along the door's -x, or its -z for the lift's pair.
            const anim::Vec3 apart = row->secondBelow ? anim::Vec3{0.0F, 0.0F, -2.0F * door.leafWidth}
                                                      : anim::Vec3{-2.0F * door.leafWidth, 0.0F, 0.0F};
            leaf.closed = second ? anim::add(door.position, rotate(door.rotation, apart)) : door.position;
            door.slidingLeaves.push_back(std::move(leaf));
        }
    }
    // Its triangles two-sided with the door bit and the type's material, then closed (command 7).
    markTriangles(world, door.triangles, raycast::kTriangleTwoSided | kTriangleDoorBit, door.material);
    runSlidingCommand(door, door_command::kCloseToo, world);
    // Each leaf placed (SubSlidingDoor_PlaceLeaf): the first at the door's pose, a second turned half round about the
    // door's y for an up-and-down slide, else about the vertical; its slide direction is kept from then. **Coney's
    // stand-in**: placed at set-up, where the original places it on its first update; a command before that leaves
    // such a second leaf turned about y there and about z here (docs/research/armies.md#q-leaf-place).
    for (std::size_t i = 0; i < door.slidingLeaves.size(); ++i) {
        SlidingLeaf& leaf = door.slidingLeaves[i];
        if (i == 0) {
            leaf.rotation = door.rotation;
        } else if (door.slideAxis < 2) {
            leaf.rotation = turnAboutOwnAxis(door.rotation, anim::Vec3{0.0F, 1.0F, 0.0F}, 180.0F);
        } else {
            leaf.rotation = turnAboutVertical(door.rotation, 180.0F);
        }
        leaf.axis = rotate(leaf.rotation, slideDirection(door.slideAxis));
    }
}

void Doors::runSlidingCommand(Door& door, int command, ObjectWorld& world) {
    NavLinks links(world.paths);
    // Every command but on the lift's pair puts the leaves' poses back: the first the door's, a second the door's
    // turned half round about its z, whatever its placement turned it about. Their slide directions stay as placed.
    if (door.type != kLiftPair) {
        for (std::size_t i = 0; i < door.slidingLeaves.size(); ++i) {
            door.slidingLeaves[i].rotation = i == 0 ? door.rotation : turnAboutVertical(door.rotation, 180.0F);
        }
    }
    // The leaves take the door's command at once: a slide starts on their next step. **Coney's stand-in**: the
    // original only shortens each leaf's update interval to 2 ticks, so a leaf at rest on its 60-tick cycle starts
    // sliding 1 to 60 ticks later (SlidingDoor_StateCommand, Task_SetUpdateInterval `0x003a1630`; not yet seen at
    // runtime). The triangles and links change at once either way.
    const auto slide = [&door](int motion) {
        for (SlidingLeaf& leaf : door.slidingLeaves) {
            leaf.motion = motion;
            leaf.countdown = kSlideStepTicks;
        }
    };
    switch (command) {
    case door_command::kOpen:
    case door_command::kOpenToo:
        slide(slide_motion::kOpening);
        door.slideState = slide_state::kSlideOpen;
        playSound(door, door.slideOpenSound, world);
        [[fallthrough]];
    case door_command::kDisableCollision:
        setTrianglesEnabled(world, door.triangles, false);
        links.openByNumber(door.number);
        return;
    case door_command::kClose:
    case door_command::kCloseToo:
        slide(slide_motion::kClosing);
        door.slideState = slide_state::kSlideClosed;
        playSound(door, door.slideCloseSound, world);
        [[fallthrough]];
    case door_command::kEnableCollision:
        setTrianglesEnabled(world, door.triangles, true);
        links.closeByNumber(door.number);
        return;
    default:
        // 10 and 11 (the lock-pick glint) and the rest: **Coney's stand-in**, no glint on a sliding door.
        return;
    }
}

void Doors::command(double handle, int command, ObjectWorld& world) {
    if (Door* door = findMutable(handle)) {
        runCommand(*door, command, world);
    }
}

// The swinging door's state commands (docs/research/objects.md#door-states); a sliding door's go to
// runSlidingCommand().
// @orig 0x003f9770 DoorSwing_StateCommand (unknown)
void Doors::runCommand(Door& door, int command, ObjectWorld& world) {
    if (door.doorClass == DoorClass::Sliding && !door.removed) {
        runSlidingCommand(door, command, world);
        return;
    }
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
        setTrianglesEnabled(world, door.triangles, true);
        if (world.services != nullptr) {
            world.services->setBody(door.handle, true);
        }
        links.closeByNumber(door.number);
        return;
    case door_command::kDisableCollision:
        links.openByNumber(door.number);
        setTrianglesEnabled(world, door.triangles, false);
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

// Pickable on: the glint, and the door's collision body removed (docs/research/objects.md#lock-pick).
// @orig 0x003f9588 DoorSwing_SetPickable (unknown)
void Doors::pickableOn(Door& door, ObjectWorld& world) {
    // Only a door that is (nearly) closed takes the glint.
    if (door.angle >= kPickableMaxAngle) {
        return;
    }
    door.pickable = true;
    // Stand-in (#707): the original also clears the kept angle (what DoorOpenDegree left while pickable) here; Coney
    // keeps Door::keptAngle.
    door.glint = true;
    door.glintAt = anim::add(door.position, rotate(door.rotation, anim::Vec3{-door.leafWidth, 0.0F, kGlintHeight}));
    if (world.services != nullptr) {
        world.services->setBody(door.handle, false);
    }
}

// Pickable off: the glint gone and the collision body back (docs/research/objects.md#lock-pick).
// @orig 0x003f9588 DoorSwing_SetPickable (unknown)
void Doors::pickableOff(Door& door, ObjectWorld& world) {
    door.pickable = false;
    door.glint = false;
    if (world.services != nullptr) {
        world.services->setBody(door.handle, true);
    }
}

// Message 0x42 (`DoorOpenDegree`): swing to the angle, or keep it while pickable
// (docs/research/objects.md#door-states).
// @orig 0x003fb8d0 DynDoorSwinging_OnMessage (unknown)
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
        // A sliding door opened by a human takes command 2 (DynDoorSliding_OnMessage `0x0b`). Not ported from that
        // handler: message 8's detach from the parent, 0x15's own destroy and 0x3e's data (#707), 0x10 and 0x13's
        // pinning of the leaves for streaming (#710).
        if (door->doorClass == DoorClass::Sliding) {

            runSlidingCommand(*door, door_command::kOpen, world);
            return;
        }
        openAway(*door, humanAt, world);
    }
}

// Message 0x0b on a door that is closed and not pickable: open away from the human
// (docs/research/objects.md#door-states).
// @orig 0x003fb8d0 DynDoorSwinging_OnMessage (unknown)
// @orig 0x003f9910 DoorSwing_OpenBy (unknown)
void Doors::openAway(Door& door, anim::Vec3 humanAt, ObjectWorld& world) {
    if (door.doorClass != DoorClass::Swinging || door.ended || door.pickable || door.state != door_state::kClosed) {
        return;
    }
    resetLeaves(door);
    if (door.keptAngle != 0.0F) {
        // Stand-in (#707): the original runs command 5 (collision off, link open) before it takes the kept angle.
        door.angle = door.keptAngle;
    } else {
        // Away from the human: the sign of the door's turned axis against the human-to-door direction. Stand-in
        // (#707): the original dots the axis with (human - door), the opposite sign; not ported until the leaves'
        // turn is checked against it at run time.
        const anim::Vec3 axis = rotate(door.rotation, anim::Vec3{0.0F, 1.0F, 0.0F});
        const float side = anim::dot(axis, anim::subtract(door.position, humanAt));
        door.angle = side >= 0.0F ? kOpenAngle : -kOpenAngle;
    }
    swingTo(door);
    door.pickable = false;
    startOpening(door, world);
}

// Message 0x0c (`IsDoorOpen`): a swinging door in state 5, a sliding door slid open
// (docs/research/objects.md#door-states).
// @orig 0x003fb8d0 DynDoorSwinging_OnMessage (unknown)
// @orig 0x003f5ef0 DynDoorSliding_OnMessage (unknown)
bool Doors::isOpen(double handle) const {
    const Door* door = find(handle);
    if (door != nullptr && door->doorClass == DoorClass::Sliding) {
        return door->slideState == slide_state::kSlideOpen;
    }
    return door != nullptr && door->doorClass == DoorClass::Swinging && door->state == door_state::kOpen;
}

// The step the state command, DoorSwing_OpenBy and message 0x42 share: state 3 and the open sound; the next
// tick's update is DoorSwing_SwingTo's reschedule (docs/research/objects.md#door-states).
// @orig 0x003f9770 DoorSwing_StateCommand (unknown)
// @orig 0x003f8fd0 DoorSwing_SwingTo (unknown)
void Doors::startOpening(Door& door, ObjectWorld& world) {
    door.state = door_state::kOpening;
    playSound(door, door.setup.openSound, world);
    // Opening reschedules the update for the next tick. Stand-in (#707): the original's DoorSwing_SwingTo reschedules
    // it on every swing, closes and angle changes too; Coney only here, when a swing opens.
    door.countdown = 1;
}

// Each leaf's target: the door's rotation turned by the angle, the right leaf's by its negation
// (docs/research/objects.md#leaves).
// @orig 0x003f8fd0 DoorSwing_SwingTo (unknown)
void Doors::swingTo(Door& door) {
    // Not ported (#710): the leaves' spawn records pinned once, for streaming (Coney keeps every object resident).
    // The left leaf turns by the angle from its closed pose, the right by the negated angle from its turned base.
    for (std::size_t leaf = 0; leaf < door.leaves.size(); ++leaf) {
        DoorLeaf& made = door.leaves[leaf];
        made.target = turnAboutVertical(made.base, leaf == 0 ? door.angle : -door.angle);
        made.turned = true;
    }
}

// The leaves back in their closed poses at once (docs/research/objects.md#leaves).
// @orig 0x003f9368 DoorSwing_ResetLeaves (unknown)
void Doors::resetLeaves(Door& door) {
    // Stand-in (#707): the original's message 0x35 to the left leaf also makes its next update start from its
    // previous target; Coney's reset drops it, so OpenDoor on a door already open swings its leaves from closed.
    for (DoorLeaf& leaf : door.leaves) {

        leaf.target = leaf.base;
        leaf.from = leaf.base;
        leaf.to = leaf.base;
        leaf.rotation = leaf.base;
        leaf.turned = false;
    }
}

// @coney-only the call into Coney's object services (Sound_PlayHashAt at the door)
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

// dyn_door_store's hit: broken by its last hitpoint, it swings open away from the attacker
// (docs/research/objects.md#door-break).
// @orig 0x003f9ad0 DoorSwing_Hit (unknown)
void Doors::hitStore(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    // Only the hit that takes its last hitpoint: the sounds, its triangles off, and it swings open away from them.
    if (door.hitpoints <= 0) {
        return;
    }
    door.hitpoints -= hitDamage(hit.kind);
    if (door.hitpoints > 0) {
        return;
    }
    // Stand-in (#707): the original plays the hit sound at the door on every hit; Coney only on the breaking one.
    playSound(door, kDoorHitSound, world);
    playPair(world, material::kGlass, material::kGlass, door.position);
    setTrianglesEnabled(world, door.triangles, false);
    openAway(door, hit.attackerAt, world);
}

// A cabin door's hit: the sound, the leaves' break below 7 and the wreck at 0 (docs/research/objects.md#door-break).
// @orig 0x003f9ad0 DoorSwing_Hit (unknown)
void Doors::hitCabin(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    playSound(door, kDoorHitSound, world);
    door.hitpoints -= hitDamage(hit.kind);
    if (door.hitpoints <= 0) {
        wreck(door, world);
        return;
    }
    // Below 7 both leaves are broken and one, at random, takes its next model with a burst. Stand-in (#707): the
    // original runs this step on every hit below 7, and before the break test; Coney once (leavesBroken), and not
    // on the breaking hit.
    if (door.hitpoints < kCabinLeafBreak && !door.leavesBroken && !door.leaves.empty()) {
        door.leavesBroken = true;
        for (DoorLeaf& leaf : door.leaves) {
            leaf.broken = true;
        }
        const DoorLeaf& chosen =
            door.leaves.at(static_cast<std::size_t>(randomBelow(world.random, static_cast<int>(door.leaves.size()))));
        if (world.services != nullptr) {
            world.services->nextModel(chosen.handle);
            // Stand-in (#707): the original's burst is at the leaf's hinge 1 m up.
            world.services->burst(chosen.position);
        }
    }
}

// dyn_door_liz's, dyn_door_dclub's and dyn_door_stall's hit: dust, splinters, model stages, the wreck
// (docs/research/objects.md#door-break).
// @orig 0x003fa438 DoorSwing_HitBreakable (unknown)
void Doors::hitSplinters(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    // Not ported (#707): object flag 0x800000 set on every hit, and the hit mode set from the hitpoints.
    // Stand-in (#707): a door in state 4 takes nothing from a hit in the original; Coney's always counts.
    // Stand-in (#707): every hit makes dust at the door (radii 2.75 and 3.75); the 20 splinters are dyn_door_dclub's
    // only, 40 twice in its colours. Coney adds 20 splinters for every type.
    dustAndSplinters(world, hit.point);
    // Stand-in (#707): the original plays (material, CONCRETE 5) at the door once, at the break; Coney at the hit
    // point on every hit.
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
    // The model stages: dyn_door_liz at 80, 60, 40 and 20 %, dyn_door_dclub at 50 %. Stand-in (#707): the original
    // takes liz's stages one a hit, checked from 20 % up, each below (not at) its share and needing the model of the
    // one before, and pairs the door's material with itself at 80 % (which also removes both leaves and shows the
    // door), then with 106, 36 and 35; dclub's stage also removes both leaves, takes model 0x4d67682e and bursts.
    // Coney loops from 80 % down, at or below each share, pairs 35, 36 and 106 with CONCRETE and keeps the leaves.
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

// The break both hits end in: triangles off, the two loose pieces, state 6 (docs/research/objects.md#door-break).
// @orig 0x003f9ad0 DoorSwing_Hit (unknown)
// @orig 0x003fa438 DoorSwing_HitBreakable (unknown)
void Doors::wreck(Door& door, ObjectWorld& world) {
    // Not ported (#707): at the break the original removes the door's collision body, gives both leaves their next
    // models (the cabins), makes its bursts, calls 0x00219320 on the attacker and, for liz, stall and dclub, plays
    // their pairs and removes the leaves.
    setTrianglesEnabled(world, door.triangles, false);
    if (world.services != nullptr) {
        for (const std::string_view piece : door.setup.wreck) {
            if (!piece.empty()) {
                // Stand-in (#707): the original throws each piece with random speeds and offsets; Coney places it
                // at the door's pose.
                world.services->spawnObject(piece, door.position, door.rotation);
            }
        }
    }
    door.state = door_state::kBroken;
}

// A barrier's hit: damage, dust and splinters, and at 0 the break by class (docs/research/objects.md#barriers).
// @orig 0x003b2180 DoorFence_OnHit (unknown)
void Doors::hitBarrier(Door& door, const ObjectHit& hit, ObjectWorld& world) {
    // Every hit: its damage, dust and splinters; one that leaves hitpoints does nothing more. Not ported (#707): the
    // hit mode set from the hitpoints.
    door.hitpoints -= hitDamage(hit.kind);
    dustAndSplinters(world, hit.point);
    if (door.hitpoints > 0) {
        return;
    }
    // Broken: the triangles off and the links open, then by class: dyn_door_wall_a its damaged model and no body,
    // dyn_door_wall_b nothing more, the others three boards. **Coney's reading**: the barrier's collision body, which
    // the break leaves until its removal and which stops a charge dead (docs/research/combat/index.md#moving-strikes),
    // is its triangles in Coney's level mesh, so they go off only at the removal, unless the body goes now (wall_a) or
    // the barrier is never removed (dyn_door_vargas). That timing is Coney's choice (#707): the original turns both
    // triangles off at the break for every barrier.
    if (door.type == kWallA || door.type == kVargas) {
        setTrianglesEnabled(world, door.triangles, false);
    }
    NavLinks(world.paths).openByNumber(door.number);
    if (world.services != nullptr) {
        if (door.type == kWallA) {
            world.services->setModel(door.handle, kBarrierDamagedModel);
            world.services->setBody(door.handle, false);
        } else if (!listed(kBoardlessBarriers, door.type)) {
            // Each board is made and sent its messages, then goes at its first update (tick()): a board that stayed
            // would stand in the gap as the broken fence for as long as the barrier waits for its removal.
            // Stand-in (#707): the original makes each board dyn_wooddmg_a or _b at random and throws it, unless
            // bit 2 of game state word 3; Coney places dyn_wooddmg_a, _b, _a at the barrier's pose. Not ported
            // (#707): the dust and bursts at the barrier (unless bit 2 of game state word 0), and dyn_door_wall_b's
            // and dyn_door_wall_a's bursts.
            for (const std::string_view board : kBoards) {
                const double handle = world.services->spawnObject(board, door.position, door.rotation);
                if (handle != kNoObject) {
                    door.boards.push_back(handle);
                }
            }
        }
    }
    playPair(world, door.material, door.material, hit.point);
    // dyn_door_vargas destroys its second object and takes its broken model; the others are marked broken and hide,
    // to be removed at their next update. Stand-in (#707): the original never marks dyn_door_wall_a broken
    // (0x003b2180): it stays, with its damaged model, and is never removed; Coney hides and removes it too.

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

// Message 0x15: flag 0x40 and update interval 2 (docs/research/objects.md#door-states).
// @orig 0x003fb8d0 DynDoorSwinging_OnMessage (unknown)
void Doors::destroy(double handle) {
    // Not ported, from the same message handler: 0x10's pinning of the leaves for streaming (#710), 0x20's removal of
    // the leaves (#1021) and 0x3e's answer of the angle (#707).

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

// Message 0x19: the hitpoints, a swinging door's or a barrier's (docs/research/objects.md#door-states,
// docs/research/objects.md#barriers).
// @orig 0x003fb8d0 DynDoorSwinging_OnMessage (unknown)
// @orig 0x003b3158 DoorFence_OnMessage (unknown)
void Doors::setHitpoints(double handle, int hitpoints) {
    if (Door* door = findMutable(handle)) {
        door->hitpoints = hitpoints;
    }
}

// The hitpoints Object_GetHitpoints reads (object +0x128), 0 for no door (docs/research/objects.md#doors).
// @orig 0x00385918 Object_GetHitpoints (unknown)
int Doors::hitpoints(double handle) const {
    // Stand-in (#1021): the original reads any world object's +0x128 (a prop's hitpoints too); Coney answers only for
    // doors and barriers, 0 for anything else.
    const Door* door = find(handle);
    return door != nullptr ? door->hitpoints : 0;
}

// A barrier's message 10: whether it can be hit (docs/research/objects.md#barriers).
// @orig 0x003b3158 DoorFence_OnMessage (unknown)
void Doors::setHittable(double handle, bool hittable) {
    // Not ported (#707): the hit mode the original's message handler sets from the hitpoints.
    if (Door* door = findMutable(handle); door != nullptr && door->doorClass == DoorClass::Barrier) {
        door->hittable = hittable;
    }
}

void Doors::tick(ObjectWorld& world) {
    for (Door& door : m_doors) {
        for (DoorLeaf& leaf : door.leaves) {
            stepLeaf(leaf);
        }
        for (SlidingLeaf& leaf : door.slidingLeaves) {
            stepSlidingLeaf(leaf, door.slideLength, door.slideStep);
        }
        // The boards a broken barrier threw end at their first update, the tick after the hit.
        if (!door.boards.empty()) {
            for (const double board : door.boards) {
                if (world.services != nullptr) {
                    world.services->destroyObject(board);
                }
            }
            door.boards.clear();
        }
        // A barrier's update only asks whether it is broken; a broken one is removed (WorldObject_Update).
        if (door.doorClass == DoorClass::Barrier && !door.removed && --door.countdown <= 0) {
            door.countdown = door.interval;
            if (door.broken) {
                door.removed = true;
                setTrianglesEnabled(world, door.triangles, false);
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

// A swinging door's update: a destroyed door's self-hit, then the state steps (docs/research/objects.md#door-states).
// @orig 0x003fbba0 DynDoorSwinging_Update (unknown)
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

// The count of abandoned picks at a swinging door, true on the third (docs/research/objects.md#lock-pick).
// @orig 0x003f8048 DoorSwing_CountLockPick (unknown)
bool Doors::lockPickAbandoned(double handle) {
    Door* door = findMutable(handle);
    if (door == nullptr || door->doorClass != DoorClass::Swinging) {
        return false;
    }
    ++door->abandonedPicks;
    return door->abandonedPicks == kAbandonsReported;
}

// @coney-only a lookup by handle
const Door* Doors::find(double handle) const {
    const auto found = std::ranges::find(m_doors, handle, &Door::handle);
    return found == m_doors.end() ? nullptr : &*found;
}

// @coney-only a lookup by handle
Door* Doors::findMutable(double handle) {
    const auto found = std::ranges::find(m_doors, handle, &Door::handle);
    return found == m_doors.end() ? nullptr : &*found;
}

// @coney-only a lookup by triangle
const Door* Doors::findByTriangle(std::uint32_t triangle) const {
    const auto found = std::ranges::find_if(m_doors, [triangle](const Door& door) {
        return door.triangles[0] == triangle || door.triangles[1] == triangle;
    });
    return found == m_doors.end() ? nullptr : &*found;
}

// @coney-only a lookup by leaf handle
const Door* Doors::findByLeaf(double leaf) const {
    const auto found = std::ranges::find_if(m_doors, [leaf](const Door& door) {
        return std::ranges::find(door.leaves, leaf, &DoorLeaf::handle) != door.leaves.end();
    });
    return found == m_doors.end() ? nullptr : &*found;
}

// What each door draws: its leaves, or its own model.
// @coney-only drawing glue
std::vector<DoorDraw> doorDraws(const Doors& doors) {
    std::vector<DoorDraw> draws;
    for (const Door& door : doors.doors()) {
        if (door.ended || door.hidden || door.removed) {
            continue;
        }
        if (door.doorClass == DoorClass::Sliding && !door.slidingLeaves.empty()) {
            for (const SlidingLeaf& leaf : door.slidingLeaves) {
                draws.push_back(DoorDraw{.handle = leaf.handle,
                                         .modelHash = crc32(leaf.model),
                                         .fallbackHash = crc32(fallbackModelOf(door.type)),
                                         .position = leaf.position(),
                                         .rotation = leaf.rotation,
                                         .tint = door.tint});
            }
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
