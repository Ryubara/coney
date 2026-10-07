// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "scripting/script_bindings.h"
#include "world_objects/object_tasks.h"

// The hats humans wear: the hat-fit sets `CfgHat` fills, where a worn hat sits on the head, and a knocked-off hat's
// fall. A worn hat is a world object (a spawn record) the wearer draws at his head; a knocked-off one falls and then
// lies where it landed like any other object.
// Research: docs/research/characters.md#hats, docs/research/script-types.md#hat-object

namespace coney::world_objects {

/// A class's hat name when he wears none (`CfgChar`'s argument 11).
inline constexpr std::string_view kNoHat = "none";
/// The pose bone a hat is worn on: 6, the head.
inline constexpr std::size_t kHatBone = 6;
/// The slots of one hat-fit set.
inline constexpr std::size_t kHatSlots = 48;
/// A knocked-off hat's spin, radians a second, and the speed it is thrown off at, metres a second.
inline constexpr float kHatSpin = 8.0F;
inline constexpr float kHatThrowSpeed = 2.0F;
/// The object kind (`+0x86`) of a hat that is never knocked off.
inline constexpr int kHatKindStaysOn = 27;

/// One fitting: where a hat sits in the head's frame.
struct HatFit {
    anim::Vec3 offset{}; ///< Metres, before the wearer's scale.
    anim::Quat rotation{};
};

/// The hat-fit sets (`0x00662b70`, 0xa90 bytes each): per set, the character type that owns it and up to kHatSlots
/// hats by name, each with its fitting.
///
/// Research: docs/research/characters.md#hats, docs/references/hats.md
class HatFits {
  public:
    /// The sets of the `CfgHat` calls in `recorded`, in their order.
    [[nodiscard]] static HatFits fromRecorded(const script::RecordedCalls& recorded);

    /// `CfgHat(set, owner, hat, offset, rotation)`: set `set`'s owner becomes `owner`, and `hat` goes into its first
    /// free slot with its fitting. **Coney's choice**: a hat for a full set is dropped (the original writes it before
    /// the record).
    /// @orig 0x00228d70 Cfg_AddHatFit (unknown)
    void add(int set, int owner, std::string_view hat, HatFit fit);

    /// The fitting of `hat` for a Warrior created as `type` of class `classType`: in the set `type` owns if one does,
    /// else in the one `classType` owns, the slot named `hat`. Nothing when no set is owned or the hat is in no slot
    /// of it. **Coney's reading**: of several sets one type owns, the lowest-numbered.
    /// @orig 0x003a3ba0 Human_PlaceHat (unknown)
    [[nodiscard]] std::optional<HatFit> find(int type, int classType, std::string_view hat) const;

  private:
    struct Set {
        int index = 0;
        int owner = 0;
        std::vector<std::pair<std::string, HatFit>> slots;
    };
    std::vector<Set> m_sets; // by index, rising
};

/// Where a hat of type `hatType` (its own held pose `own`) sits on a human of scale `scale` created as `type` of class
/// `classType`: on the head bone, at a Warrior's (`warrior`) fitting from `fits` or else the hat's own pose, the
/// offset × `scale`, the rotation as is. **Coney's stand-in**: a Warrior's hat in no slot of his set takes the hat's
/// own pose (the original's default transform `0x005116c0` is not on the page).
/// @orig 0x003e77c0 HatObject_Wear (unknown)
[[nodiscard]] HeldAttachment hatAttachment(const HatFits& fits, bool warrior, int type, int classType,
                                           std::string_view hatType, HatFit own, float scale);

/// Which way a knocked-off hat leaves the head (the knock-off's `+0x668` & 3): the direction it is thrown and the axis
/// it spins about, both unit vectors in the wearer's frame (+y ahead).
struct HatThrow {
    anim::Vec3 direction;
    anim::Vec3 spinAxis;
};
/// The throw for side `side` & 3: 0 ahead (+y) spinning about +x, 1 to +x about −y, 2 behind (−y) about −x, 3 to −x
/// about +y.
/// @orig 0x00258330 Human_KnockOffHat (unknown)
[[nodiscard]] HatThrow hatThrow(int side);

/// The ground height under a point, for a falling hat: nothing when there is no ground below.
using GroundBelow = std::function<std::optional<float>(anim::Vec3 at)>;

/// The hats knocked off and still falling.
///
/// **Coney's stand-in** for the hat's physics (state 2, a loose physics object, not traced): it flies on at its thrown
/// velocity under kGravity, spinning, until it meets the ground below it, where it stops as it is.
class FallingHats {
  public:
    /// The fall's gravity, m/s² (Coney's choice).
    static constexpr float kGravity = 9.81F;

    /// Hat `hat` leaves a head at `pose` (game axes), worn by a human facing `heading` (radians about z): thrown along
    /// the throw's direction at kHatThrowSpeed and spinning at kHatSpin about its axis, both turned by the heading.
    /// @orig 0x003e7958 HatObject_Drop (unknown)
    void knockOff(double hat, WorldPose pose, float heading, HatThrow how);

    /// One step of `seconds`: each hat moves, spins and falls; one that reaches the ground stops there and leaves the
    /// list. Returns every hat's new pose this step (those that landed among them).
    std::vector<std::pair<double, WorldPose>> step(float seconds, const GroundBelow& ground);

    /// Whether hat `hat` is falling.
    [[nodiscard]] bool falling(double hat) const;
    /// Forgets hat `hat` (its object was removed).
    void forget(double hat);

  private:
    struct Fall {
        double hat = 0;
        WorldPose pose;
        anim::Vec3 velocity;
        anim::Vec3 spinAxis; // unit, world
    };
    std::vector<Fall> m_falls;
};

} // namespace coney::world_objects
