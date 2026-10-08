// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "world_objects/object_services.h"
#include "world_objects/object_types.h"

// The breakable street props (newsstands, crate stacks, benches) and the other world objects a strike can land on:
// the two hit counters every world object keeps, what a strike does to them, and how a `dyn_masks` prop breaks and
// goes. Research: docs/research/objects.md#breakable-props, docs/research/combat.md#targets

namespace coney::world_objects {

/// The class of the breakable street props.
inline constexpr std::string_view kDynMasksClass = "dyn_masks";
/// The class of the trash cans, bags and other props a player can pick up and throw, which any strike breaks.
inline constexpr std::string_view kOverheadWeaponClass = "overhead_weapon";
/// The cash register's class, its drawer's type and the money the open drawer spills.
inline constexpr std::string_view kCashRegisterClass = "dyn_cashreg";
inline constexpr std::string_view kCashDrawerType = "dyn_cashreg_b";
inline constexpr std::string_view kMoneyType = "dyn_money";
/// The broken cash register's model.
inline constexpr std::uint32_t kCashRegisterBrokenModel = 0x59026f53U;

/// A collision body's flags from its type's word `+0x5e`: bit `0x1` gives `0x2`, `0x2` → `0x4`, `0x4` → `0x10`,
/// `0x8` → `0x8`, `0x10` → `0x40`, `0x20` → `0x20`, `0x40` → `0x2000`, `0x80` → `0x10000`, `0x100` → `0x20000`, on top
/// of `0x80000500`. The kinds that add bits of their own first are left out (none of them is a strike target here).
/// @orig 0x00391d48 Obj_CreatePhysicsBody (unknown)
[[nodiscard]] std::uint32_t bodyFlagsOf(std::uint16_t typeWord);

/// Whether a body with `bodyFlags` is something a strike picks: any of `0x8`, `0x10`, `0x20`.
/// @orig 0x00396710 Object_IsStrikeTarget (unknown)
[[nodiscard]] constexpr bool isStrikeTarget(std::uint32_t bodyFlags) { return (bodyFlags & 0x38U) != 0; }

/// Whether a sphere at `centre` of `radius` touches the collision body of an object of `type` standing at `position`
/// turned by `rotation` (game axes): a box of the type's size about its centre offset (`PHYS.OBB`), or a sphere of half
/// its size's x (`PHYS.SPHERE`), as `Obj_CreatePhysicsBody` makes them. **Coney's stand-ins**: the size is the box's
/// whole extents (the sphere's half x reads it so), and a cylinder or capsule body is tested as its box. No shape
/// (`PHYS.NONE`) or no size: no body.
/// Research: docs/research/objects.md#spawning, docs/research/combat.md#moving-strikes
[[nodiscard]] bool bodyTouches(const ObjectType& type, anim::Vec3 position, anim::Quat rotation, anim::Vec3 centre,
                               float radius);

/// Where a struck prop stands: its object's position and rotation (game axes).
struct PropPose {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// What one strike did to a prop.
struct PropStrike {
    bool intactBefore = false; ///< Neither counter was 0 before it: the strike was damage done (message 6).
    bool broke = false;        ///< The prop broke on it.
};

/// The world objects strikes have landed on: each one's two hit counters (`+0x10d`, `+0x10e`) and, for a `dyn_masks`
/// prop, its own hit points and hits, until it breaks and goes. An object's state is made from its type on its first
/// strike and kept by handle while the level lasts.
///
/// **Coney's stand-ins** where the research is open or Coney lacks the system: the noise, the statistic and the
/// burning near a fire are not built; the newsstands' paper debris is left out; the dust's colour, view test and
/// particle room are the services' (ObjectServices::dust()); a piece rests where it is spawned (it is not knocked
/// flying); the crate piece's random turn is drawn in steps of 1/1000 of pi; and a broken prop goes 20 ticks after
/// the break (the original removes it at its next update, up to 20 ticks later). None of the riot props has a damaged
/// model, so none is swapped.
///
/// An `overhead_weapon` prop (a trash can, bags) breaks on any strike (`OverheadWeapon_Break`): its pieces by model,
/// its dust and splinters, a trash set's bottle (a `dyn_beerbottle` 0.5 m above the path polygon under the hit point,
/// knocked loose through ObjectWorld::knock with a random spin), its material pair (at 0.65 when the attacker ran into
/// it, ObjectHit::runIn), and it goes the next tick. **Coney's stand-ins** there: the pieces rest where they are
/// spawned and never fade; the litter system's pieces, the brown splat, the splinters' colours, the camera test and the
/// path-polygon flag are left out; the path polygon's height is the mean of its vertices' heights; the cardboard set's
/// 8 debris pieces are 8 splinters; and a litter piece's offset is each axis drawn in ±0.43, ±0.43 and 0-0.83 m (the
/// research leaves the draw's use open).
///
/// A cash register (`dyn_cashreg`) takes 2 + 8 × kind from its hit points (its type's `+0x5a`, 16): a surviving hit
/// sounds its material against concrete and raises dust at the hit; the breaking one swaps its broken model, sounds its
/// material against itself, opens its drawer and stops it being a strike target, and it stays, still solid. One drawer
/// update later (60 ticks) the drawer spills a `dyn_money` 0.22 m above it holding $25-49. **Coney's stand-ins**
/// there: what makes the drawer is not traced, so the break makes it, open, at the register's pose (it does not slide);
/// the dust is ObjectServices::burst(); the game state flag that silences the hit sound, the use (triangle's robbery,
/// which takes the drawer) and the pick-up and drop of the register are left out.
///
/// Research: docs/research/objects.md#breakable-props, docs/research/objects.md#riot-prop-breaks,
/// docs/research/objects.md#trash-props, docs/research/script-types.md#dyn-cashreg
class Props {
  public:
    /// The ticks after a break until a broken prop goes: its update interval.
    static constexpr int kRemovalTicks = 20;
    /// The splinters a bench breaks into.
    static constexpr int kBenchSplinters = 25;
    /// The two dust bursts every hit on a `dyn_masks` prop raises at the hit point, metres.
    static constexpr float kDustRadius = 3.75F;
    static constexpr float kSecondDustRadius = 2.75F;
    /// The open drawer's update interval: the ticks from its opening to its money.
    static constexpr int kDrawerTicks = 60;
    /// The money: how far above the drawer it appears, and its dollars, kMoneyLeast plus a draw below kMoneySpread.
    static constexpr float kMoneyRise = 0.22F;
    static constexpr int kMoneyLeast = 25;
    static constexpr int kMoneySpread = 25;

    /// A strike from `hit.attacker` (of `hit.kind`, landing at `hit.point`) on the world object `handle` of `type`, at
    /// `pose`
    /// (`Strike_Contact` on a world object): takes the counters (`WorldObject_TakeHit`), plays the impact sound,
    /// tells the boxes the attacker stands in when the object was intact (ObjectServices::damageDone()), then gives a
    /// `dyn_masks` prop, an `overhead_weapon` or a cash register its hit (message 1). A broken prop takes nothing more.
    /// @orig 0x0021b290 Strike_Contact (unknown)
    PropStrike strike(double handle, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                      ObjectWorld& world);

    /// Whether the prop `handle` is broken (it no longer offers itself to a strike).
    [[nodiscard]] bool broken(double handle) const;
    /// Whether the prop `handle` broke and lost its body with it (every broken prop but a cash register, which stays
    /// solid).
    [[nodiscard]] bool bodyLost(double handle) const;
    /// The prop's counter `+0x10d`; nothing for an object no strike has landed on.
    [[nodiscard]] std::optional<std::uint8_t> counter(double handle) const;

    /// One 60 Hz tick: a broken prop's wait for its removal, and an open drawer's for its money, which it spills into
    /// `world` (ObjectServices::spawnObject(), ObjectServices::setValue()).
    /// @orig 0x003bffd0 DynCashregB_Update (unknown)
    void tick(ObjectWorld& world);
    /// The broken props whose time came since the last call: their objects go for good (message 2, spawn record bit
    /// `0x40000`).
    /// @orig 0x003bf548 DynMasks_Update (unknown)
    [[nodiscard]] std::vector<double> takeRemoved();

    /// Forgets every prop (the level is unloaded).
    void clear() {
        m_props.clear();
        m_removed.clear();
    }

  private:
    // One struck object.
    struct Prop {
        std::uint8_t counter = 0;       // +0x10d
        std::uint8_t secondCounter = 0; // +0x10e
        bool onePoint = false;          // +0x128: each hit takes one point
        bool masks = false;             // a dyn_masks prop, with the data below
        bool overhead = false;          // an overhead_weapon prop: any strike breaks it
        bool cashRegister = false;      // a dyn_cashreg: its hit points below, its drawer once open
        anim::Vec3 drawerAt{};          // the open drawer's place and turn
        anim::Quat drawerTurn{};
        int moneyIn = 0;     // ticks until the open drawer spills its money; 0 for none
        int hitpoints = -1;  // its data +0x0c: -1 for none
        int hits = -1;       // its data +0x10: -1 for none
        bool broken = false; // its data +0x00
        int removalIn = 0;   // ticks until it goes, once broken
        std::uint32_t modelHash = 0;
    };

    // The state of `handle`, made from `type` on its first strike.
    // @orig 0x003b7538 DynMasks_Init (unknown)
    Prop& stateOf(double handle, const ObjectType& type);
    // A dyn_masks prop's message 1: its own hit points or hits, the material sound, the dust and, when it breaks, its
    // splinters and piece.
    // @orig 0x003b7a88 DynMasks_OnHit (unknown)
    static bool masksHit(Prop& prop, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                         ObjectWorld& world);
    // An overhead_weapon prop's message 1: it breaks, whatever the hit.
    // @orig 0x003ffe90 OverheadWeapon_Break (unknown)
    static void overheadBreak(Prop& prop, const ObjectType& type, const ObjectHit& hit, const PropPose& pose,
                              ObjectWorld& world);
    // A cash register's message 1: its hit points, the sound and dust, and when they run out its broken model and its
    // drawer opened.
    // @orig 0x003c06b0 DynCashreg_OnMessage (unknown)
    static bool cashRegisterHit(double handle, Prop& prop, const ObjectType& type, const ObjectHit& hit,
                                const PropPose& pose, ObjectWorld& world);

    std::map<double, Prop> m_props;
    std::vector<double> m_removed;
};

/// `WorldObject_TakeHit` on the counters `counter` (`+0x10d`) and `secondCounter` (`+0x10e`) of an object hit with
/// kind `kind` (−1 a flying or held object's contact): `+0x10e` loses one first while `flyingOrHeld`; otherwise
/// `+0x10d` loses 1 when `kind` is −1 or `onePoint`, else 4 + 6 × kind, floored at 0. A counter at 0 or `0xff`
/// (unbreakable) is left alone.
/// @orig 0x00393450 WorldObject_TakeHit (unknown)
void takeHit(std::uint8_t& counter, std::uint8_t& secondCounter, int kind, bool onePoint, bool flyingOrHeld);

} // namespace coney::world_objects
