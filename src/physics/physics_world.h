// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "animation/anim_math.h"
#include "core/engine_limits.h"
#include "physics/contact.h"
#include "physics/shapes.h"

// The physics world's bodies (docs/research/physics-bodies.md#unit-7) and its fixed tables
// (docs/research/physics-bodies.md#unit-8): the body record and its update, the body vector, the list of bodies kept
// sorted by their bound's min x with its widest-body bound, the box queries every shape, sweep, strike and ray test
// starts from, the overlap query, the ignore slots a body uses to pass through owners, and the shape pools. Bodies and
// shapes are named by ids into the world's own stores, owners by the game mode's ids: the original's pointers become
// those ids, so the world's state can be copied and named across machines.
// Research: docs/research/physics-bodies.md#unit-7, docs/research/physics-bodies.md#unit-8

namespace coney::physics {

/// A body's index in the physics world's body store; kNoBody for none (the original's null body pointer).
using BodyId = std::uint32_t;
inline constexpr BodyId kNoBody = std::numeric_limits<BodyId>::max();

/// An ignore slot's index; kNoIgnoreSlot for none.
using IgnoreSlotId = std::uint32_t;
inline constexpr IgnoreSlotId kNoIgnoreSlot = std::numeric_limits<IgnoreSlotId>::max();

/// Body flags (`+0x40`) this file reads.
inline constexpr std::uint32_t kBodyFlagSweepBodies = 0x1;     ///< With kBodyFlagAlwaysUpdate: the update sweep.
inline constexpr std::uint32_t kBodyFlagWriteVelocity = 0x400; ///< Write the slid velocity back.
inline constexpr std::uint32_t kBodyFlagAlwaysUpdate = 0x800;  ///< Pose and re-sort on every update.
inline constexpr std::uint32_t kBodyFlagDirty = 0x80000000;    ///< Pose and re-sort on the next update.

/// Overlap query options (`+0x10`) IPhysics_OverlapShape reads; no other bit is read.
inline constexpr std::uint32_t kOverlapMesh = 0x1;          ///< Test the level mesh.
inline constexpr std::uint32_t kOverlapBodies = 0x2;        ///< Test the bodies.
inline constexpr std::uint32_t kOverlapTypeMask = 0x10;     ///< Use the query's type mask (else every type).
inline constexpr std::uint32_t kOverlapSkipGroup = 0x80;    ///< Skip a body of the same non-zero group.
inline constexpr std::uint32_t kOverlapAskOwner = 0x100;    ///< Ask the querying owner about each candidate's owner.
inline constexpr std::uint32_t kOverlapEnabledOnly = 0x200; ///< Test only the candidate's enabled shapes.
inline constexpr std::uint32_t kOverlapGivenBox = 0x400;    ///< Query the given box, not the shape's bound.

/// The original's pools, each a fixed number of shapes built by the world's constructor
/// (docs/research/physics-bodies.md#u8-pools).
enum class ShapePool : std::uint8_t {
    ObjectSphere, ///< 416 spheres for world objects.
    ObjectBox,    ///< 416 boxes for world objects.
    CarBox,       ///< 36 boxes for cars.
    BigSphere,    ///< 30 spheres given a 2.0 m radius when taken.
    StrikeSphere, ///< 60 weapon strike spheres, free while switched off.
};

/// The original's count of each pool, in ShapePool order (the allocators' loop limits).
inline constexpr std::array<std::size_t, 5> kOrigPoolCounts{416, 416, 36, 30, 60};
/// The original's count of ignore slots (`IPhysics_FindFreeIgnoreSlot`'s loop limit `0x3c`).
inline constexpr std::size_t kOrigIgnoreSlots = 60;

/// The most shapes pool `pool` holds: its kOrigPoolCounts entry under the engine limits preset (`limits`
/// EngineLimits::Original), no limit otherwise.
/// @coney-only limit: the original's pools hold 416 object spheres, 416 object boxes, 36 car boxes, 30 big spheres and
/// 60 strike spheres and refuse one more; past them Coney builds one more shape each time every shape is taken
[[nodiscard]] std::size_t shapePoolCap(ShapePool pool, EngineLimits limits);

/// The most ignore slots the world holds: kOrigIgnoreSlots under the engine limits preset, no limit otherwise.
/// @coney-only limit: the original keeps 60 ignore slots and drops a 61st ignore; past 60 Coney adds a slot
[[nodiscard]] std::size_t ignoreSlotCap(EngineLimits limits);

/// One ignore slot (docs/research/physics-bodies.md#u8-slot): free while `owner` is null.
struct IgnoreSlot {
    BodyId body = kNoBody;             ///< `+0x00`: the body whose chain holds it.
    OwnerId owner = kNoOwner;          ///< `+0x04`: the owner passed through.
    IgnoreSlotId next = kNoIgnoreSlot; ///< `+0x08`: the next slot of that body's chain.
};

/// A physics body (docs/research/physics-bodies.md#u7-records): the sort node, then the body's fields. A new body
/// holds what `PhysicsBody_Construct` (`0x003418f8`) writes, and zero where it writes nothing.
struct WorldBody {
    Aabb box;                                 ///< `+0x00`/`+0x10`: the bound of its shapes.
    std::int32_t vectorIndex = -1;            ///< `+0x20`: its index in the body vector.
    BodyId prev = kNoBody;                    ///< `+0x24`: the previous body of the x-sorted list.
    BodyId next = kNoBody;                    ///< `+0x28`: the next body of the x-sorted list.
    ShapeId firstShape = kNoShape;            ///< `+0x30`: the first shape of its chain.
    OwnerId owner = kNoOwner;                 ///< `+0x34`.
    IgnoreSlotId ignoreChain = kNoIgnoreSlot; ///< `+0x38`: the first slot of its ignore chain.
    std::int16_t group = 0;                   ///< `+0x3c`: bodies of one non-zero group may skip each other.
    std::uint32_t flags = kBodyFlagDirty | kBodyFlagWriteVelocity; ///< `+0x40`: kBodyFlag* bits.
    std::uint32_t typeMask = 0;                                    ///< `+0x44`: the owner's type mask.
    anim::Vec3 pushOut;                                            ///< `+0x50`: the pending push-out.
    anim::Vec3 scale{1.0F, 1.0F, 1.0F};                            ///< `+0x60`.
    std::int32_t stuck = 0;    ///< `+0x70`: updates in a row its sweep could not move it.
    bool human = false;        ///< A human body (vtable `0x005446b0`): its own ignore rules.
    bool ignoresLevel = false; ///< A human body's `+0xd4`: it ignores the level.
};

/// The update sweep a body with kBodyFlagSweepBodies and kBodyFlagAlwaysUpdate asks for after its update
/// (docs/research/physics-bodies.md#u7-update): the candidate box and the first shape's move.
struct UpdateSweep {
    Aabb box;                 ///< The new bound grown forward by `move`.
    anim::Vec3 move;          ///< From the first shape's previous position to its new one.
    ShapeId shape = kNoShape; ///< The shape swept.
};

/// The filters of IPhysics_QueryBoxFiltered (docs/research/physics-bodies.md#u7-queryboxfiltered), in the order they
/// apply.
struct BodyFilter {
    OwnerId ignored = kNoOwner;           ///< A body of this owner is dropped (kNoOwner: ownerless bodies are).
    std::uint32_t typeMask = 0xffffffffU; ///< A body sharing no bit with it is dropped.
    std::uint32_t flagMask = 0;           ///< When not 0, a body sharing no flag with it is dropped.
    std::uint32_t excludedFlags = 0;      ///< A body with any of these flags is dropped.
    std::uint32_t excludedState = 0;      ///< A body whose owner's state has any of these bits is dropped.
    /// The candidate owner's state flags (owner vtable `+0x50`), asked of every candidate past the first four filters.
    std::function<std::uint64_t(OwnerId)> stateOf;
    /// When set, a candidate is dropped when this answers false for (the querying body's owner, its owner).
    std::function<bool(OwnerId, OwnerId)> keep;
};

/// An overlap query (docs/research/physics-kernels.md#overlapshape): the querying body and shape, the options and
/// their arguments.
struct OverlapQuery {
    BodyId body = kNoBody;      ///< `+0x00`: the querying body; it must be in the x-sorted list.
    ShapeId shape = kNoShape;   ///< `+0x04`: the querying shape.
    Aabb box;                   ///< `+0x08`: the box queried with kOverlapGivenBox.
    std::uint32_t options = 0;  ///< `+0x10`: kOverlap* bits.
    std::uint32_t typeMask = 0; ///< `+0x14`: read with kOverlapTypeMask.
    std::uint32_t max = 0;      ///< `+0x18`: the most contacts to return.
    float meshLow = 0.0F;       ///< `+0x28`: handed to the mesh overlap.
    float meshHigh = 0.0F;      ///< `+0x2c`: handed to the mesh overlap.
    /// kOverlapAskOwner: the querying owner's answer (owner vtable `+0x110`) about a candidate's owner; false skips it.
    std::function<bool(OwnerId, OwnerId)> accepts;
};

/// The world of bodies: the original IPhysics's body vector, x-sorted list, bound, ignore slots and shape pools.
class PhysicsWorld {
  public:
    /// A pair test of the overlap table: whether `own` overlaps `other`, writing what its research section names into
    /// the contact.
    using PairTest = std::function<bool(Contact&, const Shape&, const Shape&)>;
    /// A mesh overlap of the IPhysics `+0xf4` table, by the querying shape's type: `(low, high, out, room, box, body)`,
    /// appending at most `room` contacts to `out` and returning how many it appended.
    using MeshOverlap =
        std::function<std::size_t(float, float, std::vector<Contact>&, std::size_t, const Aabb&, BodyId)>;

    /// An empty world whose pools and ignore slots are built as the original's constructor builds them; `limits`
    /// says whether they may grow past the original's counts.
    /// @coney-only the world's stores and tables; porting IPhysics_Construct (0x0033c288) is Unit 15's (#379)
    explicit PhysicsWorld(EngineLimits limits = EngineLimits::Coney);

    /// What a contact's owner pointer is resolved from: the game mode's owner for an id (null when it has none).
    using OwnerLookup = std::function<BodyOwner*(OwnerId)>;
    /// Sets how owner ids are resolved for the contacts the world fills; unset, their owner pointers stay null.
    void setOwnerLookup(OwnerLookup lookup) { m_ownerLookup = std::move(lookup); }
    /// The owner `id` resolves to, or null (kNoOwner, or no lookup set).
    /// @coney-only the id-to-owner lookup that replaces the original's owner pointers
    [[nodiscard]] BodyOwner* ownerPointer(OwnerId id) const;

    /// Adds a shape record to the store and returns its id.
    /// @coney-only the world's shape store; the original's shapes live in its pools and bodies
    ShapeId addShapeRecord(const ShapeRecord& record);
    /// The shape `id`.
    [[nodiscard]] ShapeRecord& shape(ShapeId id) { return m_shapes.at(id); }
    /// The shape `id`.
    [[nodiscard]] const ShapeRecord& shape(ShapeId id) const { return m_shapes.at(id); }

    /// Adds a body as `PhysicsBody_Construct` leaves it (no owner, no shape, not listed) and returns its id.
    /// @coney-only the world's body store; the original's bodies live in the pools of Unit 9 (#376)
    BodyId createBody();
    /// Adds a body for `owner`, whose type mask is `typeMask` (owner vtable `+0x20`, read only for an owner).
    /// @orig 0x00341970 PhysicsBody_ConstructForOwner (unknown)
    BodyId createBodyForOwner(OwnerId owner, std::uint32_t typeMask);
    /// The body `id`.
    [[nodiscard]] WorldBody& body(BodyId id) { return m_bodies.at(id); }
    /// The body `id`.
    [[nodiscard]] const WorldBody& body(BodyId id) const { return m_bodies.at(id); }

    /// Sets the body's owner; its type mask is set to `typeMask` only when `owner` is not kNoOwner.
    /// @orig 0x00341a68 PhysicsBody_SetOwner (unknown)
    void setOwner(BodyId id, OwnerId owner, std::uint32_t typeMask);
    /// Appends `shape` to the end of the body's chain (no check that it is already there).
    /// @orig 0x00341aa8 PhysicsBody_AddShape (unknown)
    void addShape(BodyId id, ShapeId shape);
    /// Sets `box` to the shape's bound.
    /// @orig 0x00341638 PhysicsBody_ComputeBounds (unknown)
    void computeBounds(Aabb& box, ShapeId shape) const;
    /// Grows `box` to hold the shape's bound too.
    /// @orig 0x003417c0 PhysicsBody_AddShapeBounds (unknown)
    void addShapeBounds(Aabb& box, ShapeId shape) const;

    /// A body's update: when it is dirty or always updated, poses every shape with `owner`, sets its bound and
    /// re-sorts it, then clears the dirty flag. Returns the update sweep for a body with kBodyFlagSweepBodies and
    /// kBodyFlagAlwaysUpdate (the sweep itself is Unit 11's, #377), else nothing.
    /// @orig 0x00341c10 PhysicsBody_Update (unknown)
    std::optional<UpdateSweep> updateBody(BodyId id, const ShapeOwner& owner);
    /// A body set to a new pose `m`: its first shape is set from the matrix, its bound is that shape's alone and it
    /// is re-sorted. Other shapes and the dirty flag are not touched.
    /// @orig 0x00341e40 PhysicsBody_Moved (unknown)
    void bodyMoved(BodyId id, const anim::Mat34& m);

    /// Puts a body into the x-sorted list, walking from `hint` (or the head) towards its place: after every body of
    /// equal min x it passes going forward, before every one it passes going backward. Grows the bound to its width.
    /// @orig 0x003404a8 BodyList_Insert (unknown)
    void insert(BodyId id, BodyId hint);
    /// Takes a body out of the x-sorted list and returns its old previous body (kNoBody for the head).
    /// @orig 0x003405d8 BodyList_Remove (unknown)
    BodyId remove(BodyId id);
    /// Takes a body out and puts it back from its old previous body.
    /// @orig 0x00340628 BodyList_Reinsert (unknown)
    void reinsert(BodyId id);
    /// Adds a body to the world: at the end of the body vector, then into the list from its head.
    /// @orig 0x00340668 IPhysics_AddBody (unknown)
    void addBody(BodyId id);
    /// Removes a body from the world: frees the ignore slots that name its owner, swap-removes it from the vector
    /// (the last body takes its index; its own index is kept), then takes it out of the list.
    /// @orig 0x00340700 IPhysics_RemoveBody (unknown)
    void removeBody(BodyId id);

    /// The bodies whose bound meets `box`, appended to `out`; returns how many. From `start` (kNoBody: from the head)
    /// the walk goes backward first, then forward from `start`'s next, so the backward hits come first in reverse list
    /// order; the backward walk has no upper x test. A body is dropped when its owner is `ignored`, it shares no bit
    /// with `typeMask`, or (`flagMask` not 0) it shares no flag with `flagMask`. Coney's list grows: the original's
    /// callers hold 512 and nothing checks it.
    /// @orig 0x0033feb0 IPhysics_QueryBox (unknown)
    [[nodiscard]] std::size_t queryBox(std::vector<BodyId>& out, const Aabb& box, BodyId start, OwnerId ignored,
                                       std::uint32_t typeMask, std::uint32_t flagMask) const;
    /// queryBox() with the filters of `filter`; `query` is the body whose owner `filter.keep` is asked about.
    /// @orig 0x00340120 IPhysics_QueryBoxFiltered (unknown)
    [[nodiscard]] std::size_t queryBoxFiltered(std::vector<BodyId>& out, BodyId query, const Aabb& box, BodyId start,
                                               const BodyFilter& filter) const;
    /// The contacts of the query's shape with the bodies (kOverlapBodies) and the level (kOverlapMesh), appended to
    /// `out` nearest first; returns how many, at most `q.max`.
    /// @orig 0x0033ebb8 IPhysics_OverlapShape (unknown)
    std::size_t overlapShape(std::vector<Contact>& out, const OverlapQuery& q);

    /// The step's bound refresh: the tick counter goes up, and on its 120th tick it restarts and the bound is
    /// recomputed from the listed bodies' current widths.
    /// @orig 0x00340918 IPhysics_Step (unknown)
    void refreshBound();

    /// The first body of the x-sorted list (the lowest min x), or kNoBody.
    [[nodiscard]] BodyId head() const { return m_head; }
    /// The bound: the widest listed body's `max.x − min.x` since the last refresh (`+0x14`).
    [[nodiscard]] float bound() const { return m_bound; }
    /// Overwrites the bound, as a stale bound would be.
    void setBound(float bound) { m_bound = bound; }
    /// The step's tick counter (`+0x12`).
    [[nodiscard]] std::uint16_t tickCounter() const { return m_ticks; }
    /// The body vector, in the order bodies were added (a removal moves the last body into the gap).
    [[nodiscard]] const std::vector<BodyId>& bodyVector() const { return m_vector; }
    /// The overlap table: the pair test for the own shape's type against the other's (ShapeType values; empty for no
    /// entry).
    [[nodiscard]] std::array<std::array<PairTest, 5>, 5>& overlapTable() { return m_overlaps; }
    /// The mesh overlap of shape type `type`'s queries (empty for none).
    [[nodiscard]] MeshOverlap& meshOverlap(ShapeType type) { return m_meshOverlaps.at(static_cast<std::size_t>(type)); }

    /// The lowest free ignore slot, or kNoIgnoreSlot when every slot is taken and no more may be added. It is not
    /// marked taken.
    /// @orig 0x00340890 IPhysics_FindFreeIgnoreSlot (unknown)
    IgnoreSlotId findFreeIgnoreSlot();
    /// The ignore slot `id`.
    [[nodiscard]] const IgnoreSlot& ignoreSlot(IgnoreSlotId id) const { return m_ignoreSlots.at(id); }
    /// How many ignore slots exist.
    [[nodiscard]] std::size_t ignoreSlotCount() const { return m_ignoreSlots.size(); }
    /// Whether the body passes through `owner` (body vtable `+0x30`); `grabPartner` is the body owner's grab partner
    /// (`Human_GetGrabOther`), which a human body always passes through.
    /// @coney-only the body vtable's +0x30 dispatch to the base or the human body's
    [[nodiscard]] bool isIgnoring(BodyId id, OwnerId owner, OwnerId grabPartner = kNoOwner) const;
    /// Makes the body pass through `owner` (body vtable `+0x40`).
    /// @coney-only the body vtable's +0x40 dispatch to the base or the human body's
    void ignore(BodyId id, OwnerId owner);
    /// Frees slot `slot` of the body's chain, or every slot of it for kNoIgnoreSlot (body vtable `+0x38`).
    /// @coney-only the body vtable's +0x38 dispatch to the base or the human body's
    void stopIgnoring(BodyId id, IgnoreSlotId slot);
    /// Frees every slot, in any body's chain, that names the owner of `removed`.
    /// @orig 0x0033cd70 IPhysics_ForgetIgnoredOwner (unknown)
    void forgetIgnoredOwner(BodyId removed);

    /// Takes the first free object sphere (flags kShapeAllocated clear), or kNoShape.
    [[nodiscard]] ShapeId allocObjectSphere() { return takeFromPool(ShapePool::ObjectSphere); }
    /// Takes the first free object box, or kNoShape.
    [[nodiscard]] ShapeId allocObjectBox() { return takeFromPool(ShapePool::ObjectBox); }
    /// Takes the first free car box, or kNoShape.
    [[nodiscard]] ShapeId allocCarBox() { return takeFromPool(ShapePool::CarBox); }
    /// Takes the first free big sphere, giving it a 2.0 m radius, or kNoShape.
    [[nodiscard]] ShapeId allocBigSphere() { return takeFromPool(ShapePool::BigSphere); }
    /// The first strike sphere that is switched off, or kNoShape; nothing is marked, so it stays free until enabled.
    [[nodiscard]] ShapeId findFreeStrikeSphere() { return takeFromPool(ShapePool::StrikeSphere); }
    /// Gives a pool shape back: clears only kShapeAllocated.
    /// @orig 0x0033d2c8 IPhysics_FreeShape (unknown)
    void freeShape(ShapeId id);
    /// How many shapes pool `pool` holds now.
    [[nodiscard]] std::size_t poolSize(ShapePool pool) const {
        return m_pools.at(static_cast<std::size_t>(pool)).size();
    }
    /// Shape `index` of pool `pool`.
    [[nodiscard]] ShapeId poolShape(ShapePool pool, std::size_t index) const {
        return m_pools.at(static_cast<std::size_t>(pool)).at(index);
    }

  private:
    // The x-sorted walks of both box queries, keeping the bodies `keeps` accepts after the x tests.
    // @orig 0x0033feb0 IPhysics_QueryBox (unknown)
    // @orig 0x00340120 IPhysics_QueryBoxFiltered (unknown)
    template <typename Keep>
    std::size_t walkBox(std::vector<BodyId>& out, const Aabb& box, BodyId start, const Keep& keeps) const;
    // Builds one pool shape as the constructor does and adds it to `pool`.
    // @coney-only one pool shape as IPhysics_Construct builds it (Unit 15, #379)
    ShapeId buildPoolShape(ShapePool pool);
    // The allocators' scan from index 0 for the first free shape of `pool`.
    // @orig 0x0033d1a0 IPhysics_AllocObjectSphere (unknown)
    // @orig 0x0033d1e8 IPhysics_AllocObjectBox (unknown)
    // @orig 0x0033d230 IPhysics_AllocCarBox (unknown)
    // @orig 0x0033d278 IPhysics_AllocBigSphere (unknown)
    // @orig 0x003408d8 IPhysics_FindFreeStrikeSphere (unknown)
    ShapeId takeFromPool(ShapePool pool);
    // The base body: whether its chain holds a slot naming `owner` (false for null).
    // @orig 0x00341ae8 PhysicsBody_IsIgnoring (unknown)
    [[nodiscard]] bool bodyIsIgnoring(BodyId id, OwnerId owner) const;
    // The base body: takes a free slot for `owner` and pushes it at the front of the chain; no duplicate check.
    // @orig 0x00341b20 PhysicsBody_Ignore (unknown)
    void bodyIgnore(BodyId id, OwnerId owner);
    // The base body: unlinks and zeroes `slot` (found or not), or zeroes the whole chain for kNoIgnoreSlot.
    // @orig 0x00341b80 PhysicsBody_StopIgnoring (unknown)
    void bodyStopIgnoring(BodyId id, IgnoreSlotId slot);
    // The human body: a null owner asks about the level; its owner's grab partner is always passed through.
    // @orig 0x003421d0 PhysicsHumanBody_IsIgnoring (unknown)
    [[nodiscard]] bool humanBodyIsIgnoring(BodyId id, OwnerId owner, OwnerId grabPartner) const;
    // The human body: a null owner makes it ignore the level.
    // @orig 0x00342230 PhysicsHumanBody_Ignore (unknown)
    void humanBodyIgnore(BodyId id, OwnerId owner);
    // The human body: kNoIgnoreSlot also stops it ignoring the level.
    // @orig 0x00342260 PhysicsHumanBody_StopIgnoring (unknown)
    void humanBodyStopIgnoring(BodyId id, IgnoreSlotId slot);

    EngineLimits m_limits;
    OwnerLookup m_ownerLookup;
    std::deque<ShapeRecord> m_shapes;
    std::deque<WorldBody> m_bodies;
    std::vector<BodyId> m_vector;          // `+0x4f450`: the body vector
    BodyId m_head = kNoBody;               // `+0x08`: the x-sorted list's head
    std::uint16_t m_ticks = 0;             // `+0x12`: the step's tick counter
    float m_bound = 0.0F;                  // `+0x14`: the widest body's width
    std::vector<IgnoreSlot> m_ignoreSlots; // `+0xf4e0`
    std::array<std::vector<ShapeId>, 5> m_pools;
    std::array<std::array<PairTest, 5>, 5> m_overlaps; // `+0x18`: indexed by ShapeType values
    std::array<MeshOverlap, 5> m_meshOverlaps;         // `+0xf4`: indexed by ShapeType values
};

} // namespace coney::physics
