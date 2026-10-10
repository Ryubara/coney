// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/props.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>

#include "world/path_map.h"
#include "world_objects/pickups.h"

namespace coney::world_objects {

namespace {

// A counter at this value is unbreakable: a type value of 0 becomes it, and a hit leaves it alone.
constexpr std::uint8_t kUnbreakable = 0xff;
// The bits every body has.
constexpr std::uint32_t kBodyBase = 0x80000500U;
// The impact sound's second material: a plain strike's, and a charge's or dive's.
constexpr std::uint8_t kStrikeMaterial = 9;
constexpr std::uint8_t kChargeMaterial = 0x1a;
// The second material a dyn_masks prop's hit sounds against while it survives.
constexpr std::uint8_t kSurviveMaterial = 5;
// The bench's model (`dyn_parkbench_a`), whose break throws splinters and leaves a piece 0.395 m below its origin.
constexpr std::uint32_t kBenchModel = 0x7852cedbU;
constexpr std::string_view kBenchPiece = "dyn_parkbench_aa";
constexpr float kBenchPieceDrop = -0.395F;
// The crate stack's model (`dyn_crate_stack`), whose break leaves a piece 0.604 m below its origin.
constexpr std::uint32_t kCrateModel = 0x1a0686f7U;
constexpr std::string_view kCratePiece = "dyn_wooddmg_a";
constexpr float kCratePieceDrop = -0.604F;
// **Coney's stand-in**: the crate piece's random turn in 0-pi is drawn in this many steps.
constexpr int kAngleSteps = 1000;
// The overhead_weapon models with their own break effects (docs/research/objects.md#trash-props).
constexpr std::uint32_t kTrashCanModel = 0xfbf3e3aeU;
constexpr std::uint32_t kBagsModel = 0x62502b03U;
constexpr std::uint32_t kParkTrashModel = 0xb0c69542U;
constexpr std::uint32_t kPaperStackModel = 0xfbd21393U;
// The dented can and the park bin's piece, left at the prop's own pose.
constexpr std::string_view kTrashCanPiece = "dyn_trashcan_b";
constexpr std::string_view kParkTrashPiece = "dyn_parktrash_aa";
// The "trash" set: 10 splinters, a burst at the prop and four litter pieces.
constexpr int kTrashSplinters = 10;
constexpr std::array<std::string_view, 4> kTrashBits{"dyn_trashbit_a", "dyn_trashbit_b", "dyn_trashbit_d",
                                                     "dyn_trashbit_d"};
constexpr float kTrashBitSpread = 0.43F;
constexpr float kTrashBitRise = 0.83F;
// The "cardboard" set's 8 debris pieces (Coney's stand-in: splinters), and the default set's 20 + 24 splinters.
constexpr int kCardboardPieces = 8;
constexpr int kDefaultSplinters = 44;
// The trash set's bottle, placed this far above the path polygon under the hit point, spun up to pi rad/s about y
// and z.
constexpr std::string_view kTrashBottle = "dyn_beerbottle";
constexpr float kBottleRise = 0.5F;
// The break's volume when the attacker ran into it (human +0x368).
constexpr float kRunInVolume = 0.65F;
// An overhead_weapon's dust rises 0.5 m above the hit point.
constexpr float kOverheadDustRise = 0.5F;
// A draw in [-1, 1] in this many steps each side.
constexpr int kOffsetSteps = 1000;
// The one model whose break sounds its material against tin (`TINBOX`).
constexpr std::uint32_t kTinBreakModel = 0xe42da444U;
constexpr std::uint8_t kTinMaterial = 23;

// One bit of a type's body word and the body flag it gives.
struct BodyBit {
    std::uint16_t word;
    std::uint32_t flag;
};
constexpr std::array<BodyBit, 9> kBodyBits{{{0x1, 0x2},
                                            {0x2, 0x4},
                                            {0x4, 0x10},
                                            {0x8, 0x8},
                                            {0x10, 0x40},
                                            {0x20, 0x20},
                                            {0x40, 0x2000},
                                            {0x80, 0x10000},
                                            {0x100, 0x20000}}};

// A counter from a type's byte: 0 is unbreakable.
std::uint8_t counterOf(int value) {
    const auto byte = static_cast<std::uint8_t>(value);
    return byte == 0 ? kUnbreakable : byte;
}

// Plays the material pair `a` × `b` at `at`.
void playPair(ObjectWorld& world, std::uint8_t a, std::uint8_t b, anim::Vec3 at, float volume = 1.0F) {
    if (world.services != nullptr) {
        world.services->playMaterialPair(a, b, at, volume);
    }
}

// The height of the path polygon under (x, y): Coney's stand-in, the mean of its vertices' heights; nothing where no
// polygon lies.
std::optional<float> pathHeightAt(const world::PathMap* paths, float x, float y) {
    const std::optional<std::uint32_t> found = paths != nullptr ? paths->polygonAt(x, y) : std::nullopt;
    if (!found) {
        return std::nullopt;
    }
    const world::PathPolygon& polygon = paths->polygons()[*found];
    if (polygon.vertexCount == 0) {
        return std::nullopt;
    }
    float sum = 0.0F;
    for (std::uint32_t i = 0; i < polygon.vertexCount; ++i) {
        sum += paths->vertices()[polygon.firstVertex + i].z;
    }
    return sum / static_cast<float>(polygon.vertexCount);
}

// A draw in [-1, 1] from the game's random numbers.
float randomSigned(GameRandom* random) {
    return (static_cast<float>(randomBelow(random, (2 * kOffsetSteps) + 1)) / static_cast<float>(kOffsetSteps)) - 1.0F;
}

// The trash set's bottle: a dyn_beerbottle 0.5 m above the path polygon under the hit point (none where there is no
// polygon), in the prop's rotation, knocked with no velocity and a spin of (0, a, b), a and b in +-pi.
void dropBottle(anim::Vec3 point, anim::Quat rotation, ObjectWorld& world) {
    const std::optional<float> ground = pathHeightAt(world.paths, point.x, point.y);
    if (!ground || world.services == nullptr) {
        return;
    }
    const double bottle =
        world.services->spawnObject(kTrashBottle, anim::Vec3{point.x, point.y, *ground + kBottleRise}, rotation);
    if (bottle == kNoObject || !world.knock) {
        return;
    }
    const float a = randomSigned(world.random) * std::numbers::pi_v<float>;
    const float b = randomSigned(world.random) * std::numbers::pi_v<float>;
    world.knock(bottle, anim::Vec3{}, anim::Vec3{0.0F, a, b});
}

} // namespace

std::uint32_t bodyFlagsOf(std::uint16_t typeWord) {
    std::uint32_t flags = kBodyBase;
    for (const BodyBit& bit : kBodyBits) {
        if ((typeWord & bit.word) != 0) {
            flags |= bit.flag;
        }
    }
    return flags;
}

bool bodyTouches(const ObjectType& type, anim::Vec3 position, anim::Quat rotation, anim::Vec3 centre, float radius) {
    const auto [sx, sy, sz] = type.bodySize;
    if (type.bodyShape == 0 || sx <= 0.0F) {
        return false;
    }
    // The sphere's centre in the body's frame: off the object's position, turned back by its rotation, off the centre.
    const anim::Mat34 turn = anim::matrixFromQuat(rotation);
    const anim::Vec3 offset = anim::subtract(centre, position);
    const anim::Vec3 local{anim::dot(offset, turn.x) - type.bodyCentre[0],
                           anim::dot(offset, turn.y) - type.bodyCentre[1],
                           anim::dot(offset, turn.z) - type.bodyCentre[2]};
    if (type.bodyShape == kBodySphere) {
        const float reach = (sx / 2.0F) + radius;
        return anim::dot(local, local) <= reach * reach;
    }
    // A box: the nearest point of it, each axis clamped to its half extents.
    const float dx = local.x - std::clamp(local.x, -sx / 2.0F, sx / 2.0F);
    const float dy = local.y - std::clamp(local.y, -sy / 2.0F, sy / 2.0F);
    const float dz = local.z - std::clamp(local.z, -sz / 2.0F, sz / 2.0F);
    return (dx * dx) + (dy * dy) + (dz * dz) <= radius * radius;
}

void takeHit(std::uint8_t& counter, std::uint8_t& secondCounter, int kind, bool onePoint, bool flyingOrHeld) {
    // A flying or held object spends its second counter first (Coney's reading: while it has one to spend).
    if (flyingOrHeld && secondCounter != 0 && secondCounter != kUnbreakable) {
        --secondCounter;
        return;
    }
    if (counter == 0 || counter == kUnbreakable) {
        return;
    }
    const int damage = kind == -1 || onePoint ? 1 : hitDamage(static_cast<HitKind>(kind));
    counter = static_cast<std::uint8_t>(std::max(0, counter - damage));
}

Props::Prop& Props::stateOf(double handle, const ObjectType& type) {
    if (const auto found = m_props.find(handle); found != m_props.end()) {
        return found->second;
    }
    Prop prop;
    prop.counter = counterOf(type.value);
    prop.secondCounter = counterOf(type.secondHits);
    prop.modelHash = type.modelHash;
    // A dyn_masks prop keeps its type's hit points and hits (-1 for none); with hits but no hit points each hit
    // takes one point, and its first counter starts from the hit points.
    if (type.className == kDynMasksClass) {
        prop.masks = true;
        prop.hitpoints = type.hitpoints != 0 ? type.hitpoints : -1;
        prop.hits = type.value != 0 ? type.value : -1;
        prop.onePoint = prop.hits != -1 && prop.hitpoints == -1;
        prop.counter = static_cast<std::uint8_t>(prop.hitpoints);
    }
    prop.overhead = type.className == kOverheadWeaponClass;
    // DynCashreg_Init: a cash register's hit points are its type's `+0x5a`.
    if (type.className == kCashRegisterClass) {
        prop.cashRegister = true;
        prop.hitpoints = type.value;
    }
    return m_props.emplace(handle, prop).first->second;
}

bool Props::masksHit(Prop& prop, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                     ObjectWorld& world) {
    // Hit points take the hit's damage (mirrored to the first counter); without them, hits count down.
    int left = 0;
    if (prop.hitpoints != -1) {
        prop.hitpoints -= hitDamage(hit.kind);
        prop.counter = static_cast<std::uint8_t>(std::max(0, prop.hitpoints));
        left = prop.hitpoints;
    } else {
        --prop.hits;
        left = prop.hits;
    }
    const bool breaks = left < 1;
    // The sound at the hit point: the material against concrete while it stands; against itself (or tin, for one
    // model) when it breaks.
    std::uint8_t against = kSurviveMaterial;
    if (breaks) {
        against = prop.modelHash == kTinBreakModel ? kTinMaterial : type.material;
    }
    playPair(world, type.material, against, hit.point);
    ObjectServices* services = world.services;
    // Every hit raises two dust bursts at the hit point.
    if (services != nullptr) {
        services->dust(hit.point, kDustRadius);
        services->dust(hit.point, kSecondDustRadius);
    }
    if (!breaks) {
        return false;
    }
    // The break: the model's own piece, then the prop loses its body and waits for its removal.
    prop.broken = true;
    prop.removalIn = kRemovalTicks;
    if (services == nullptr) {
        return true;
    }
    const anim::Mat34 turn = anim::matrixFromQuat(pose.rotation);
    if (prop.modelHash == kCrateModel) {
        // One wooden piece below the prop's origin, turned about z by a random angle in 0-pi.
        const float angle = std::numbers::pi_v<float> * static_cast<float>(randomBelow(world.random, kAngleSteps)) /
                            static_cast<float>(kAngleSteps);
        const anim::Quat turned{0.0F, 0.0F, std::sin(angle), std::cos(angle)};
        static_cast<void>(
            services->spawnObject(kCratePiece, anim::add(pose.position, anim::scale(turn.z, kCratePieceDrop)), turned));
    } else if (prop.modelHash == kBenchModel) {
        // 25 splinters, then the bench's piece below its origin in its own pose.
        services->splinters(hit.point, kBenchSplinters);
        static_cast<void>(services->spawnObject(
            kBenchPiece, anim::add(pose.position, anim::scale(turn.z, kBenchPieceDrop)), pose.rotation));
    }
    return true;
}

void Props::overheadBreak(Prop& prop, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                          ObjectWorld& world) {
    // Broken whatever the hit: its counter 0, and it goes at its next update, the next tick.
    prop.broken = true;
    prop.counter = 0;
    prop.removalIn = 1;
    ObjectServices* services = world.services;
    if (services == nullptr) {
        return;
    }
    // Two dust bursts above the hit point, then the model's effects and pieces.
    const anim::Vec3 dustAt{hit.point.x, hit.point.y, hit.point.z + kOverheadDustRise};
    services->dust(dustAt, kDustRadius);
    services->dust(dustAt, kSecondDustRadius);
    const bool trash =
        prop.modelHash == kTrashCanModel || prop.modelHash == kBagsModel || prop.modelHash == kParkTrashModel;
    if (prop.modelHash == kTrashCanModel) {
        static_cast<void>(services->spawnObject(kTrashCanPiece, pose.position, pose.rotation));
    } else if (prop.modelHash == kParkTrashModel) {
        static_cast<void>(services->spawnObject(kParkTrashPiece, pose.position, pose.rotation));
    }
    if (trash) {
        services->splinters(hit.point, kTrashSplinters);
        services->burst(pose.position);
        dropBottle(hit.point, pose.rotation, world);
        // The litter pieces at random offsets turned by the prop's rotation.
        const anim::Mat34 turn = anim::transform(pose.rotation, pose.position);
        for (const std::string_view bit : kTrashBits) {
            const float x = randomSigned(world.random) * kTrashBitSpread;
            const float y = randomSigned(world.random) * kTrashBitSpread;
            const float z = (randomSigned(world.random) + 1.0F) / 2.0F * kTrashBitRise;
            static_cast<void>(
                services->spawnObject(bit, anim::transformPoint(turn, anim::Vec3{x, y, z}), anim::Quat{}));
        }
    } else if (prop.modelHash == kPaperStackModel) {
        services->splinters(hit.point, kCardboardPieces);
    } else {
        services->splinters(hit.point, kDefaultSplinters);
    }
    // At 0.65 of its volume when the attacker broke it by running into it.
    playPair(world, type.material, type.material, hit.point, hit.runIn ? kRunInVolume : 1.0F);
}

bool Props::cashRegisterHit(double handle, Prop& prop, const ObjectType& type, const ObjectHit& hit,
                            const PropPose& pose, ObjectWorld& world) {
    // 2 + 8 x the kind from its hit points.
    prop.hitpoints -= 2 + (8 * static_cast<int>(hit.kind));
    ObjectServices* services = world.services;
    if (prop.hitpoints > 0) {
        // It stands: its material against concrete at the register, and dust at the hit (it does not move).
        playPair(world, type.material, kSurviveMaterial, pose.position);
        if (services != nullptr) {
            services->burst(hit.point);
        }
        return false;
    }
    // Broken for good: its broken model and its material against itself; later hits are ignored.
    prop.broken = true;
    prop.modelHash = kCashRegisterBrokenModel;
    playPair(world, type.material, type.material, pose.position);
    if (services == nullptr) {
        return true;
    }
    services->setModel(handle, kCashRegisterBrokenModel);
    // Its drawer opens (message 0x12): it jumps out and spills the money at its next two updates.
    if (prop.drawer != kNoObject) {
        prop.drawerOpenUpdates = 0;
    }
    return true;
}

void Props::initCashRegister(double handle, const ObjectType& type, const PropPose& pose, ObjectWorld& world) {
    if (type.className != kCashRegisterClass) {
        return;
    }
    const bool made = m_props.contains(handle);
    Prop& prop = stateOf(handle, type);
    if (made || world.services == nullptr) {
        return;
    }
    // The drawer: its own child, at the offset turned by the register's rotation, in the register's pose.
    const anim::Mat34 turn = anim::transform(pose.rotation, pose.position);
    prop.drawerAt = anim::transformPoint(turn, kDrawerOffset);
    prop.drawerTurn = pose.rotation;
    prop.drawer = world.services->spawnObject(kCashDrawerType, prop.drawerAt, prop.drawerTurn);
    prop.drawerClock = kDrawerTicks;
}

bool Props::useCashRegister(double handle, const ObjectType& type, ObjectWorld& world) {
    if (type.className != kCashRegisterClass) {
        return false;
    }
    Prop& prop = stateOf(handle, type);
    // The drawer goes in every case (message 0x15).
    if (prop.drawer != kNoObject) {
        if (world.services != nullptr) {
            world.services->destroyObject(prop.drawer);
        }
        prop.drawer = kNoObject;
        prop.drawerOpenUpdates = -1;
    }
    if (prop.broken || prop.busy) {
        return false;
    }
    prop.busy = true;
    return true;
}

double Props::drawerOf(double handle) const {
    const auto found = m_props.find(handle);
    return found != m_props.end() ? found->second.drawer : kNoObject;
}

PropStrike Props::strike(double handle, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                         ObjectWorld& world) {
    Prop& prop = stateOf(handle, type);
    if (prop.broken) {
        return {};
    }
    // In Strike_Contact's order: whether it was intact, the counters, the impact sound, the boxes, then message 1.
    PropStrike result;
    result.intactBefore = prop.counter != 0 && prop.secondCounter != 0;
    takeHit(prop.counter, prop.secondCounter, static_cast<int>(hit.kind), prop.onePoint, false);
    playPair(world, type.material, hit.kind == HitKind::Charge ? kChargeMaterial : kStrikeMaterial, hit.point);
    if (result.intactBefore && hit.attacker != kNoObject && world.services != nullptr) {
        world.services->damageDone(hit.attacker, handle);
    }
    if (prop.masks) {
        result.broke = masksHit(prop, type, hit, pose, world);
    } else if (prop.overhead) {
        overheadBreak(prop, type, hit, pose, world);
        result.broke = true;
    } else if (prop.cashRegister) {
        result.broke = cashRegisterHit(handle, prop, type, hit, pose, world);
    }
    // A broken prop loses its body, but a cash register keeps it (only its strike-target bits go).
    if (result.broke && !prop.cashRegister && world.services != nullptr) {
        world.services->setBody(handle, false);
    }
    return result;
}

bool Props::broken(double handle) const {
    const auto found = m_props.find(handle);
    return found != m_props.end() && found->second.broken;
}

bool Props::bodyLost(double handle) const {
    const auto found = m_props.find(handle);
    return found != m_props.end() && found->second.broken && !found->second.cashRegister;
}

std::optional<std::uint8_t> Props::counter(double handle) const {
    const auto found = m_props.find(handle);
    if (found == m_props.end()) {
        return std::nullopt;
    }
    return found->second.counter;
}

void Props::tick(ObjectWorld& world) {
    for (auto& [handle, prop] : m_props) {
        if ((prop.broken || prop.spent) && prop.removalIn > 0) {
            --prop.removalIn;
            if (prop.removalIn == 0) {
                m_removed.push_back(handle);
            }
        }
        // A drawer's update (every 60 ticks): once open, the first moves it out, the second spills its money.
        if (prop.drawer == kNoObject || --prop.drawerClock > 0) {
            continue;
        }
        prop.drawerClock = kDrawerTicks;
        if (prop.drawerOpenUpdates < 0 || world.services == nullptr) {
            continue;
        }
        ++prop.drawerOpenUpdates;
        if (prop.drawerOpenUpdates == 1) {
            // DynCashregB_OnMessage's 0x12 set its position slot 0.35 m out along its local y; the update takes it.
            const anim::Mat34 turn = anim::transform(prop.drawerTurn, prop.drawerAt);
            prop.drawerAt = anim::transformPoint(turn, anim::Vec3{0.0F, kDrawerOut, 0.0F});
            world.services->moveObject(prop.drawer, prop.drawerAt);
        } else if (prop.drawerOpenUpdates == 2) {
            // A dyn_money 0.22 m above the drawer in its pose, holding 25 + Random_Int(25) dollars (its +0x124).
            const anim::Vec3 at{prop.drawerAt.x, prop.drawerAt.y, prop.drawerAt.z + kMoneyRise};
            const double money = world.services->spawnObject(kMoneyType, at, prop.drawerTurn);
            if (money != kNoObject) {
                world.services->setValue(
                    money, static_cast<std::uint32_t>(kMoneyLeast + randomBelow(world.random, kMoneySpread + 1)));
            }
        }
    }
}

std::vector<double> Props::takeRemoved() { return std::exchange(m_removed, {}); }

std::optional<double> Props::takeFromPile(double handle, const ObjectType& type, const PropPose& pose,
                                          ObjectWorld& world) {
    // The one pile that runs out (its model's hash).
    constexpr std::uint32_t kMolotovPileB = 0x69b9d1a0U;
    Prop& prop = stateOf(handle, type);
    if (prop.spent) {
        return kNoObject;
    }
    const PileTake take = pileTake(type.objectKind, [&world](int below) { return randomBelow(world.random, below); });
    if (take.itself) {
        return std::nullopt;
    }
    if (type.modelHash == kMolotovPileB && ++prop.takes >= kMolotovPileTakes) {
        prop.spent = true;
        prop.removalIn = 2 * kSpentPileTicks;
    }
    if (take.object.empty() || world.services == nullptr) {
        return kNoObject;
    }
    if (take.cue >= 0) {
        world.services->playCueAt(take.cue, pose.position);
    }
    return world.services->spawnObject(take.object, pose.position, pose.rotation);
}

bool Props::pileSpent(double handle) const {
    const auto found = m_props.find(handle);
    return found != m_props.end() && found->second.spent;
}

} // namespace coney::world_objects
