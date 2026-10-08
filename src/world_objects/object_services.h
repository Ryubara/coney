// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "animation/anim_math.h"

// What the glass panes, doors and barriers share: the material and object-type ids they name, the hit they take, the
// object type a `CfgObj` configured, and the world they change (the level's collision triangles, its navigation links,
// and everything else through ObjectServices). Research: docs/research/objects.md

namespace coney {
class GameRandom;
}
namespace coney::raycast {
class CollisionMesh;
}
namespace coney::world {
class PathMap;
}

namespace coney::world_objects {

/// The `MATERIAL_*` ids the panes and doors name (docs/references/enums.md).
namespace material {
inline constexpr std::uint8_t kGlass = 2;       ///< `GLASS`: a pane's triangles, the large shatter's sound.
inline constexpr std::uint8_t kConcrete = 5;    ///< `CONCRETE`: what a splintering door's material sounds against.
inline constexpr std::uint8_t kGlassSmall = 88; ///< `GLASS_SMALL`: the small shatter's sound.
} // namespace material

/// The `CfgObj` object types (its 19th argument) the panes and doors test (docs/references/enums.md).
namespace object_type {
inline constexpr int kBreakAndEnterDoor = 30; ///< `TYPE_BREAKANDENTER_DOOR`: hitting one reports a break-in.
inline constexpr int kGlass = 32;             ///< `TYPE_GLASS`: `BreakGlassInRadius` breaks these objects too.
inline constexpr int kMissionTv = 35;         ///< `TYPE_MISSIONTV`: a thrown one hits as kind 3.
} // namespace object_type

/// Crime type 1, the break-in (docs/references/crime-types.md).
inline constexpr int kCrimeBreakIn = 1;
/// Flag activity 9, `_xwWindowLook`: the flags a broken pane switches off (docs/research/flags.md#activities).
inline constexpr int kActivityWindowLook = 9;
/// The nil handle: no object.
inline constexpr double kNoObject = 0.0;

/// What a `CfgObj` call configured for an object type, as far as the panes and doors read it (docs/research/objects.md,
/// the argument order of `CfgObj` in docs/research/bindings/).
struct ObjectTypeInfo {
    std::string className{};   ///< Argument 2: the script type (`dyn_door_swinging`, `dyn_door_fence`, ...).
    int hitpoints = 0;         ///< Argument 3: a door's or barrier's hitpoints.
    anim::Vec3 size{};         ///< Argument 8: the object's box, metres.
    std::uint8_t material = 0; ///< Argument 13: the material its triangles take.
    float leafWidth = 0.0F;    ///< Argument 15 (`f15`, type `+0x68`, property 5): a door leaf's width, metres.
    int objectType = 0;        ///< Argument 19: `TYPE_*` (object_type).
};

/// Finds an object type's configuration by name; nothing when no `CfgObj` named it.
using ObjectTypeLookup = std::function<const ObjectTypeInfo*(std::string_view name)>;

/// The **kind** of a hit (message 1's fourth value): what decides a door's damage, `4 + 6 × kind`
/// (docs/research/objects.md#door-break).
enum class HitKind : std::uint8_t {
    Plain = 0,    ///< A human's plain hit; a thrown object of animation set 5 other than a molotov.
    Thrown = 1,   ///< A thrown object.
    Charge = 2,   ///< A human in the run attack, the charge or the dive (record `+0x08` & `0x1400000`).
    Airborne = 3, ///< A human in the air (object flag `0x4000000`); a thrown `TYPE_MISSIONTV`.
};

/// The damage a hit of `kind` does to a door or barrier: 4, 10, 16 or 22.
/// @orig 0x003f9ad0 DoorSwing_Hit (unknown)
[[nodiscard]] constexpr int hitDamage(HitKind kind) { return 4 + (6 * static_cast<int>(kind)); }

/// One hit (message 1): who dealt it, its kind, where it landed and which way it went.
struct ObjectHit {
    double attacker = kNoObject; ///< The human (or, for a thrown object, its thrower); kNoObject for none.
    HitKind kind = HitKind::Plain;
    anim::Vec3 point{};      ///< Where it landed.
    anim::Vec3 direction{};  ///< Which way it went.
    anim::Vec3 attackerAt{}; ///< Where the attacker stands: a store door hit open swings away from him.
    /// The attacker ran into it (human `+0x368`, a sprint into a `RUNTARGET` body): its break sounds quieter.
    bool runIn = false;
};

/// Everything a pane's or a door's change reaches beyond the collision triangles and the navigation links: sounds,
/// particles, crimes, flags, statistics, the objects' collision bodies, other objects and models. Each call has a
/// default that does nothing, so a test or an early host overrides only what it watches. Play mode implements it over
/// the game's systems as they arrive.
class ObjectServices {
  public:
    virtual ~ObjectServices() = default;
    ObjectServices() = default;
    ObjectServices(const ObjectServices&) = delete;
    ObjectServices& operator=(const ObjectServices&) = delete;
    ObjectServices(ObjectServices&&) = delete;
    ObjectServices& operator=(ObjectServices&&) = delete;

    /// A 3D sound by name hash at `at` (`0x003a6ee8`).
    virtual void playSound(std::uint32_t /*nameHash*/, anim::Vec3 /*at*/) {}
    /// The sound matrix's entry for materials `a` and `b` at `at` (`0x00117280`, docs/research/sound.md#play).
    virtual void playMaterialPair(std::uint8_t /*a*/, std::uint8_t /*b*/, anim::Vec3 /*at*/, float /*volume*/ = 1.0F) {}
    /// Whether a shatter at `centre` makes shards: within 15 m and 10 m of the two tests' points and inside the
    /// particle budget (`0x003a5280`, `0x003a51f8`, `0x003a5a50`), or forced by game state bit `0x20`.
    [[nodiscard]] virtual bool shardsWanted(anim::Vec3 /*centre*/) { return true; }
    /// One `glasstest` shard particle at `at` of `size` with the pane's colour word.
    virtual void spawnShard(anim::Vec3 /*at*/, float /*size*/, std::uint32_t /*colour*/) {}
    /// A crime of `type` reported at `at` with `offender` (docs/research/ai.md#crimes).
    virtual void reportCrime(int /*type*/, anim::Vec3 /*at*/, double /*offender*/) {}
    /// Moves the level's `CrimeScene` flag to `at` (an alarmed pane's break-in).
    virtual void moveCrimeSceneFlag(anim::Vec3 /*at*/) {}
    /// Disables every enabled flag of `activity` within `radius` of `at` (`0x00417480`).
    virtual void disableFlagsNear(float /*radius*/, anim::Vec3 /*at*/, int /*activity*/) {}
    /// A pane broken by `breaker`: crime statistic 10 when he is a player or a player's gang member.
    virtual void countPaneBroken(double /*breaker*/) {}
    /// Frees every `dyn_carstereo` within `radius` of `at` (a car window broken, `0x003a5870`).
    virtual void freeCarStereos(anim::Vec3 /*at*/, float /*radius*/) {}
    /// Breaks every `TYPE_GLASS` object within `radius` of `centre` (`BreakGlassInRadius`' other half).
    virtual void breakGlassObjects(anim::Vec3 /*centre*/, float /*radius*/) {}
    /// Adds (`present`) or removes an object's collision body (`0x003a52c8` / `0x003a5340`).
    virtual void setBody(double /*object*/, bool /*present*/) {}
    /// Plays interface cue `cue` of the sound matrix as a 3D sound at `at` (a weapon pile's take cue).
    virtual void playCueAt(int /*cue*/, anim::Vec3 /*at*/) {}
    /// Makes a loose object of `type` (a wreck piece, a board) at `at`; its handle, or kNoObject.
    virtual double spawnObject(std::string_view /*type*/, anim::Vec3 /*at*/, anim::Quat /*rotation*/) {
        return kNoObject;
    }
    /// Removes an object (message `0x15`).
    virtual void destroyObject(double /*object*/) {}
    /// Moves an object to `at` (a cash register's drawer jumping open).
    virtual void moveObject(double /*object*/, anim::Vec3 /*at*/) {}
    /// Gives an object its next model (message `0x19`, a damaged door's or leaf's next stage).
    virtual void nextModel(double /*object*/) {}
    /// Gives an object the model named by `modelHash`.
    virtual void setModel(double /*object*/, std::uint32_t /*modelHash*/) {}
    /// Sets an object's value (`+0x124`): a `dyn_money`'s dollars.
    /// @orig 0x003a4cb0 WorldObject_SetField124 (unknown)
    virtual void setValue(double /*object*/, std::uint32_t /*value*/) {}
    /// Dust of `radius` at `at` (`0x003c57d8`).
    virtual void dust(anim::Vec3 /*at*/, float /*radius*/) {}
    /// `count` splinters at `at` (`0x003c5b00`).
    virtual void splinters(anim::Vec3 /*at*/, int /*count*/) {}
    /// The small burst of a leaf taking its next model (`0x003c6038`).
    virtual void burst(anim::Vec3 /*at*/) {}
    /// Stops every human whose object target is `object` (`0x0022e400`).
    virtual void stopHumansTargeting(double /*object*/) {}
    /// Calls the Lua function `function` with a human's and a door's handles (the lock-pick callbacks).
    virtual void callScript(std::string_view /*function*/, double /*human*/, double /*door*/) {}
    /// Scores statistic `category`-`event` for `human` (a perfect lock pick: bonus event 1-3,
    /// docs/references/statistics.md).
    virtual void scoreEvent(double /*human*/, int /*category*/, int /*event*/) {}
    /// `human` damaged `object` (a landed strike, a thrown object's hit, a car hit): message 6 to every enabled volume
    /// box he stands in (docs/research/scripting.md#triggers).
    /// @orig 0x00413018 VolumeBoxes_SendDamageMessage (unknown)
    virtual void damageDone(double /*human*/, double /*object*/) {}
    /// A human's hit on `car` damaged `part` (1-25; -2 for the hit that left every part off), `broke` when it came off
    /// now: the car's message 0x19, (car, human, part, flag) (docs/research/cars.md).
    /// @orig 0x0038bea0 Car_OnHit (unknown)
    virtual void carHit(double /*car*/, double /*human*/, int /*part*/, bool /*broke*/) {}
    /// The lock-pick dial's click on a missed press.
    virtual void lockPickClick(double /*human*/) {}
};

/// The world the panes and doors change. The pointers may be null (a host without that part): what would change it
/// is skipped.
struct ObjectWorld {
    raycast::CollisionMesh* collision = nullptr; ///< The level's static collision mesh, whose triangles they own.
    world::PathMap* paths = nullptr;             ///< The level's navigation links (the path data's D records).
    ObjectServices* services = nullptr;          ///< Everything else; null does nothing.
    GameRandom* random = nullptr;                ///< The game's random numbers; null draws 0.
    /// Knocks an object loose (message `0x30`): it flies from where its record stands at `velocity` (m/s), spinning at
    /// `spin` (rad/s); empty: nothing moves it.
    std::function<void(double object, anim::Vec3 velocity, anim::Vec3 spin)> knock;
};

/// Switches the collision triangles `triangles` (indices into `mesh`) on or off; indices outside the mesh are skipped.
/// A null `mesh` does nothing.
void setTrianglesEnabled(raycast::CollisionMesh* mesh, std::span<const std::uint32_t> triangles, bool on);

/// Adds the flag bits `flags` to the triangles `triangles` and, when `material` is not negative, sets their material
/// byte. A null `mesh` does nothing.
/// @orig 0x003a4768 Triangle_MakeTwoSided (unknown)
void markTriangles(raycast::CollisionMesh* mesh, std::span<const std::uint32_t> triangles, std::uint16_t flags,
                   int material = -1);

/// A random whole number in [0, n - 1] from `random` (`0x003353b8`); 0 without one.
[[nodiscard]] int randomBelow(GameRandom* random, int n);

/// `rotation` turned by `degrees` about the vertical axis (applied after it, in the object's own frame): the half-angle
/// quaternion the doors' swing builds (docs/research/objects.md#door-states).
[[nodiscard]] anim::Quat turnAboutVertical(anim::Quat rotation, float degrees);

/// `v` rotated by `rotation`.
[[nodiscard]] anim::Vec3 rotate(anim::Quat rotation, anim::Vec3 v);

} // namespace coney::world_objects
