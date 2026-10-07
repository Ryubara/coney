// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "world_objects/object_services.h"

// The doors level scripts spawn with `SpawnDoor`: the swinging doors (`dyn_door_swinging`, one or two leaves, a state
// machine, lock picking, the breakable ones) and the breakable barriers (`dyn_door_fence` and its kin).
// Research: docs/research/objects.md#doors, docs/research/objects.md#door-states, docs/research/objects.md#door-break,
// docs/research/objects.md#barriers, docs/research/objects.md#lock-pick

namespace coney::world_objects {

/// What a door's script type makes of it.
enum class DoorClass : std::uint8_t {
    Swinging, ///< `dyn_door_swinging`: leaves, states, commands.
    Barrier,  ///< `dyn_door_fence`, `_bar_bani`, `_bnstr`, `_chain_s`, `_fence_o`, `_parapet`: broken by hits.
    Other,    ///< Any other class (`dyn_door_sliding`, ...): placed, but the page gives it no behaviour.
};

/// The swinging door's state byte (data `+0x28`).
namespace door_state {
inline constexpr int kClosing = 0;    ///< 0 and 1: closing; each update adds 1.
inline constexpr int kClosed = 2;     ///< Closed: the leaves are reset on reaching it.
inline constexpr int kOpening = 3;    ///< The leaves swinging; 4 on the next update.
inline constexpr int kSwung = 4;      ///< Collision off on the next update, then kOpen.
inline constexpr int kOpen = 5;       ///< Open: the only state `IsDoorOpen` answers true for.
inline constexpr int kBroken = 6;     ///< Broken: the next update ends the door's task.
inline constexpr int kCabinRest = 10; ///< The cabin doors at rest.
} // namespace door_state

/// The state commands (message `0x22`, `ObjectChangeState`).
namespace door_command {
inline constexpr int kPickableOff = 0;      ///< Also 11; the lock pick's success sends it.
inline constexpr int kOpen = 2;             ///< `OpenDoor`; also 8.
inline constexpr int kClose = 3;            ///< `CloseDoor`; also 7.
inline constexpr int kDisableCollision = 5; ///< `DisableDoorCollision`; what an opening door runs.
inline constexpr int kEnableCollision = 6;  ///< What a closing door runs.
inline constexpr int kOpenToo = 8;
inline constexpr int kCloseToo = 7;
inline constexpr int kPickableOn = 10;     ///< `SetDoorPickable(door, true)`.
inline constexpr int kPickableOffToo = 11; ///< `SetDoorPickable(door, false)`.
} // namespace door_command

/// How a swinging door type takes hits (docs/research/objects.md#door-break).
enum class DoorBreaking : std::uint8_t {
    None,      ///< Ignores hits.
    Store,     ///< `dyn_door_store`: the first hit swings it open, away from the attacker.
    Cabin,     ///< The cabin doors: a leaf breaks below 7 hitpoints, the door at 0.
    Splinters, ///< `dyn_door_liz`, `_dclub`, `_stall`: dust and splinters, model stages, wreck pieces.
};

/// The default open and close sounds (name hashes) of a swinging door.
inline constexpr std::uint32_t kDefaultOpenSound = 0xecc3ab3cU;
inline constexpr std::uint32_t kDefaultCloseSound = 0xb70e3ee2U;
/// The sound of a breakable door's hit.
inline constexpr std::uint32_t kDoorHitSound = 0xcf6586b2U;
/// A barrier's damaged model, and `dyn_door_vargas`' broken one (name hashes).
inline constexpr std::uint32_t kBarrierDamagedModel = 0x7f97c350U;
inline constexpr std::uint32_t kVargasBrokenModel = 0x3b4fefadU;

/// The angle `OpenDoor` and `DoorOpen` swing to, degrees.
inline constexpr float kOpenAngle = 170.0F;
/// A pickable glint only appears while the door's angle is under this, degrees.
inline constexpr float kPickableMaxAngle = 2.0F;
/// The glint's height above the door, metres.
inline constexpr float kGlintHeight = 1.3F;
/// Update intervals, in 60 Hz ticks: a swinging door, a cabin door, a barrier.
inline constexpr int kDoorInterval = 28;
inline constexpr int kCabinInterval = 180;
inline constexpr int kBarrierInterval = 60;
/// Below this a cabin door's leaves break.
inline constexpr int kCabinLeafBreak = 7;
/// Splinters a splintering door or a barrier throws per hit; dust radii.
inline constexpr int kSplinters = 20;
inline constexpr std::array<float, 2> kDustRadii{2.75F, 3.75F};

/// What `DoorSwing_SetUpType` gives a swinging door type, by name.
struct DoorTypeSetup {
    int leaves = 0; ///< 0, 1 or 2 `dyn_dr_*` leaves.
    std::uint32_t openSound = kDefaultOpenSound;
    std::uint32_t closeSound = kDefaultCloseSound;
    bool pickable = false;   ///< Starts pickable (`_chainlnk_pick`, `_storeb`, `_templedoor`).
    bool marksLinks = false; ///< Retags its number's links `0x40` (`_dclub`, `_liz`).
    bool cabin = false;      ///< State 10, triangle bit `0x800`, a 180-tick update.
    DoorBreaking breaking = DoorBreaking::None;
    std::string leafModel{};                 ///< The first leaf's object type.
    std::string secondLeafModel{};           ///< The second leaf's.
    std::array<std::string_view, 2> wreck{}; ///< The two loose pieces it leaves when it breaks.
};

/// A swinging door type's set-up. **Coney's stand-in** for the leaf models where the page names none: `dyn_dr_` and the
/// type name after `dyn_door_` (the Object List has `dyn_dr_<name>` for most types).
/// @orig 0x003f80f0 DoorSwing_SetUpType (unknown)
[[nodiscard]] DoorTypeSetup swingingDoorSetup(std::string_view type);

/// The class of the script type `className`.
[[nodiscard]] DoorClass doorClassOf(std::string_view className);

/// A leaf's update interval, 60 Hz ticks: the time one swing takes (docs/research/objects.md#leaves).
inline constexpr int kLeafSwingTicks = 28;

/// One leaf of a swinging door (a `sub_swinging_door` object).
struct DoorLeaf {
    double handle = kNoObject; ///< `GetLeftDoorHandle` / `GetRightDoorHandle`.
    std::string model{};       ///< Its object type (`dyn_dr_*`).
    anim::Vec3 position{};
    anim::Quat base{};     ///< The closed pose.
    anim::Quat target{};   ///< Where message `0x35` last turned it (data `+0x00`).
    anim::Quat from{};     ///< Where its swing starts (`+0x20`).
    anim::Quat to{};       ///< Where its swing ends (`+0x40`).
    anim::Quat rotation{}; ///< Where it is now: `from` slerped to `to` over kLeafSwingTicks.
    int swingTicks = 0;    ///< Ticks since the swing started (`+0x50`).
    bool turned = false;   ///< A new target waits for the leaf's next update (data `+0x14`).
    bool broken = false;   ///< A cabin door's leaf, flagged broken.
};

/// `SpawnDoor`'s arguments.
struct DoorSpawn {
    std::string type{};                       ///< The object type (`dyn_door_cabin_a`).
    anim::Vec3 position{};                    ///< `+0x10`.
    anim::Quat rotation{};                    ///< `+0x20`.
    std::array<std::uint32_t, 2> triangles{}; ///< The two static collision triangles that stand for it.
    std::uint16_t number = 0;                 ///< Its number in the level: the links it ties.
};

/// One door or barrier (Coney keeps the task's and data block's behaviour, not their layout).
struct Door {
    double handle = kNoObject;
    std::string type{};
    DoorClass doorClass = DoorClass::Other;
    DoorTypeSetup setup{}; ///< A swinging door's.
    anim::Vec3 position{};
    anim::Quat rotation{};
    std::array<std::uint32_t, 2> triangles{};
    std::uint16_t number = 0;
    std::uint8_t material = 0; ///< The type's material: its triangles' and its hits' sound.
    int objectType = 0;        ///< The type's `TYPE_*` (object_type).
    float leafWidth = 0.0F;    ///< `w`, the type's `CfgObj` argument 15 (property 5): one leaf's width.

    float angle = 0.0F;             ///< Data `+0x00`: the angle it swings to, degrees.
    float keptAngle = 0.0F;         ///< Data `+0x04`: a `DoorOpenDegree` left while it was pickable.
    int hitpoints = 0;              ///< Data `+0x08` (and the `+0x128` mirror `GetHitpoints` reads).
    int maxHitpoints = 0;           ///< The type's, at spawn.
    std::vector<DoorLeaf> leaves{}; ///< Left, then right.
    bool pickable = false;          ///< Data `+0x20`.
    bool glint = false;             ///< The pickable glint (`sub_triglint`) is up.
    anim::Vec3 glintAt{};
    int state = door_state::kClosed;  ///< Data `+0x28`.
    int abandonedPicks = 0;           ///< Record `+0x29`: lock picks abandoned at it.
    std::uint32_t tint = 0xffffffffU; ///< `+0xcc`: a broken splintering door clears its alpha byte.
    int modelStage = 0;               ///< Model stages taken (`dyn_door_liz`, `dyn_door_dclub`).
    bool leavesBroken = false;        ///< A cabin door's leaves, flagged broken.

    int interval = kDoorInterval;  ///< Its update interval, ticks.
    int countdown = kDoorInterval; ///< Ticks to its next update.
    bool destroying = false;       ///< Message `0x15` (flag `0x40`): it will hit itself.
    bool ended = false;            ///< Its task ended (a broken swinging door's next update).

    bool hittable = true;            ///< A barrier's message 10.
    bool hidden = false;             ///< A broken barrier hides itself.
    double secondObject = kNoObject; ///< `dyn_door_vargas`' second object (data `+0x08`).
};

/// The level's doors and barriers.
///
/// Research: docs/research/objects.md#doors
class Doors {
  public:
    /// Gives out handles for the leaves and glints, from the world objects' handle space.
    using HandleSource = std::function<double()>;

    /// `SpawnDoor`: makes the door with `handle` from `spawn` and its type's `CfgObj` (`info`; null for a type no
    /// `CfgObj` named: **Coney's stand-in** is a swinging door of 100 hitpoints). A swinging door gets its type's
    /// leaves (the second at `position + rotation × (−2w, 0, 0)`, turned 180°), its triangles two-sided with bit
    /// `0x400`, bit `0x40` when it has hitpoints and the type's material (a cabin door also `0x800`, state 10), its
    /// links retagged for `_dclub` and `_liz`, and the lock-pick glint for the pickable types. A barrier gets its
    /// triangles two-sided with `0x40` and `0x400` and, all but `dyn_door_chain_s`, its links retagged `0x40`.
    ///
    /// `w` is the type's `CfgObj` argument 15, one leaf's width (docs/research/objects.md#leaves).
    /// @orig 0x00397230 Door_Spawn (unknown)
    /// @orig 0x003fb5f8 DoorSwing_Init (unknown)
    /// @orig 0x003b2f40 DoorFence_Init (unknown)
    const Door& spawn(double handle, const DoorSpawn& spawn, const ObjectTypeInfo* info, const HandleSource& handles,
                      ObjectWorld& world);

    /// Message `0x22` (`ObjectChangeState`): the state commands of door_command. A barrier or another class ignores
    /// them, as does a door whose task has ended.
    /// @orig 0x003f9770 DoorSwing_StateCommand (unknown)
    void command(double handle, int command, ObjectWorld& world);
    /// `SetDoorPickable(door, pickable)`: stops the humans picking it first when switching it off, then command 10 or
    /// 11.
    /// @orig 0x00397078 Door_SetPickable (unknown)
    void setPickable(double handle, bool pickable, ObjectWorld& world);
    /// Message `0x42` (`DoorOpenDegree`): swings to `degrees` (state 2 → 3) with the open sound; a pickable door only
    /// keeps the angle for later.
    void openToDegree(double handle, float degrees, ObjectWorld& world);
    /// Message `0x0b` (`DoorOpen(door, human)`): a closed door that is not pickable swings ±170° away from the human at
    /// `humanAt` (or to the kept angle), with the open sound. **Coney's stand-in** for "away": the sign of the dot
    /// product of the door's turned y axis with the human-to-door direction.
    /// @orig 0x003f9910 DoorSwing_OpenBy (unknown)
    void openBy(double handle, anim::Vec3 humanAt, ObjectWorld& world);
    /// Message `0x0c` (`IsDoorOpen`): only state 5.
    [[nodiscard]] bool isOpen(double handle) const;

    /// Message 1: a hit of `hit.kind`, taking `4 + 6 × kind` hitpoints. A swinging door by its type's DoorBreaking, a
    /// barrier as `dyn_door_fence`'s hit. Returns whether the door took it.
    /// @orig 0x003f9ad0 DoorSwing_Hit (unknown)
    /// @orig 0x003fa438 DoorSwing_HitBreakable (unknown)
    /// @orig 0x003b2180 DoorFence_Hit (unknown)
    bool hit(double handle, const ObjectHit& hit, ObjectWorld& world);
    /// Message `0x15` (what `BreakObjectsInRadius` sends): a swinging door hits itself a few ticks later.
    void destroy(double handle);
    /// `BreakObjectsInRadius(centre, radius)`: message `0x15` to every door within `radius`. Returns how many.
    /// @orig 0x003961d0 World_BreakObjectsInRadius (unknown)
    std::size_t destroyInRadius(anim::Vec3 centre, float radius);
    /// Message `0x19`: sets the hitpoints.
    void setHitpoints(double handle, int hitpoints);
    /// `GetHitpoints`: the hitpoints mirror; 0 for no door.
    [[nodiscard]] int hitpoints(double handle) const;
    /// A barrier's message 10: whether it can be hit.
    void setHittable(double handle, bool hittable);

    /// One 60 Hz tick: each door whose countdown runs out updates (a swinging door's states, a destroyed door's hit),
    /// and each leaf takes its target rotation.
    /// @orig 0x003fbba0 DoorSwing_Update (unknown)
    /// @orig 0x003fb5a0 SubSwingingDoor_Update (unknown)
    void tick(ObjectWorld& world);

    /// A lock pick at the door by `human` succeeded: command 0 (no longer pickable), then the door swings open away
    /// from him. **Coney's stand-in** for `Door_OpenAnimated` (the human's state 26, not traced): openBy() at once.
    /// @orig 0x0022d908 LockPick_End (unknown)
    void lockPickSucceeded(double handle, anim::Vec3 humanAt, ObjectWorld& world);
    /// A lock pick at the door was abandoned: a swinging door counts it; returns true on the third, which reports a
    /// break-in.
    bool lockPickAbandoned(double handle);

    /// The door with `handle`; null when there is none.
    [[nodiscard]] const Door* find(double handle) const;
    /// The door one of whose triangles is `triangle`; null when none.
    [[nodiscard]] const Door* findByTriangle(std::uint32_t triangle) const;
    /// The door with leaf `leaf`; null when none.
    [[nodiscard]] const Door* findByLeaf(double leaf) const;
    /// Every door, oldest first.
    [[nodiscard]] std::span<const Door> doors() const { return m_doors; }
    /// Forgets every door (the level's unload).
    void clear() { m_doors.clear(); }

  private:
    // The door with `handle`, for changing it; null when there is none.
    Door* findMutable(double handle);
    // Turns the leaves to `door.angle` (message 0x35 to each).
    static void swingTo(Door& door);
    // Puts the leaves back in the closed pose.
    static void resetLeaves(Door& door);
    // Plays a door sound at the door.
    static void playSound(const Door& door, std::uint32_t sound, ObjectWorld& world);
    // The start of a swing open: the state, the open sound and the next tick's update.
    static void startOpening(Door& door, ObjectWorld& world);
    // A state command on `door` (command()'s work).
    static void runCommand(Door& door, int command, ObjectWorld& world);
    // Message 0x0b on `door` (openBy()'s work).
    static void openAway(Door& door, anim::Vec3 humanAt, ObjectWorld& world);
    // Pickable on or off (commands 10 and 0 / 11).
    static void pickableOn(Door& door, ObjectWorld& world);
    static void pickableOff(Door& door, ObjectWorld& world);
    // A swinging door's update.
    static void update(Door& door, ObjectWorld& world);
    // The breakable swinging doors' and the barriers' hits.
    static void hitStore(Door& door, const ObjectHit& hit, ObjectWorld& world);
    static void hitCabin(Door& door, const ObjectHit& hit, ObjectWorld& world);
    static void hitSplinters(Door& door, const ObjectHit& hit, ObjectWorld& world);
    static void hitBarrier(Door& door, const ObjectHit& hit, ObjectWorld& world);
    // The ends of a splintering door and a cabin door: triangles off, wreck pieces, state 6.
    static void wreck(Door& door, ObjectWorld& world);

    std::vector<Door> m_doors;
};

/// One model a door puts in the world this frame.
struct DoorDraw {
    double handle = kNoObject; ///< The leaf's or the door's handle.
    std::uint32_t modelHash = 0;
    /// **Coney's stand-in** when the Object List has no model for modelHash: `dyn_dr_` and the type name after
    /// `dyn_door_`, less a leading `dbl` (the barrier `dyn_door_fence` draws `dyn_dr_fence`).
    std::uint32_t fallbackHash = 0;
    anim::Vec3 position{};
    anim::Quat rotation{};
    std::uint32_t tint = 0xffffffffU; ///< `0xRRGGBBAA`.
};

/// What the level's doors draw: each leaf of a swinging door at its pose (its model `dyn_dr_*`), and a barrier or a
/// door of another class as its type's model (a hit barrier its damaged model, `dyn_door_vargas` broken its broken
/// one); nothing for a hidden barrier or a door whose task has ended. **Coney's stand-in** until the draw is traced: a
/// swinging door's frame type draws no model of its own, and a model stage a splintering door or a leaf takes is not
/// shown.
[[nodiscard]] std::vector<DoorDraw> doorDraws(const Doors& doors);

} // namespace coney::world_objects
