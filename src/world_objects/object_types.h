// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "animation/anim_math.h"

namespace coney::world_objects {

/// One object type of the object database, as `CfgObj` makes it. Coney keeps the fields a spawn and a model need;
/// the binding's other arguments stay in the recorded calls until something uses them.
struct ObjectType {
    std::string name;            ///< `+0x28`: the name scripts spawn (`dyn_s_wwheel_a`), at most kMaxName characters.
    std::string className;       ///< `+0x43`: the class (`simple_object`), at most kMaxClassName characters.
    int hitpoints = 0;           ///< `+0x58`: a door's or barrier's hitpoints.
    std::uint32_t modelHash = 0; ///< `+0x8c`: CRC-32 of the name, the Object List key of its model.
    std::size_t index = 0;       ///< `+0x60`: the record's own index.
    int objectKind = 0;          ///< `+0x86`: the kind (`TYPE_BAT`, `TYPE_SPECIAL`...), `CfgObj`'s 19th argument.
    int value = 0;               ///< `+0x5a`, `CfgObj`'s 4th argument: a `TYPE_SPECIAL` pick-up's worth in dollars.
    int pickupAnim = 0;          ///< `+0x65`, `CfgObj`'s 14th argument: the pick-up animation (5 for jewellery).
    int animSet = 0; ///< `+0x87`, `CfgObj`'s 20th argument: the anim set the object applies in hand (3 for a bat).
    /// `+0x70`, `CfgObj`'s 17th argument: how far a held object is slid along its own y from the hand's offset to its
    /// grip (0.39 m for `dyn_bat_tuff`, docs/research/objects.md#held).
    float grip = 0.0F;
    /// `CfgObj`'s 21st and 22nd arguments: the offset (metres) and rotation the object takes when held or worn; a
    /// hat on a human who is not a Warrior sits there in his head's frame (docs/research/characters.md#hats).
    anim::Vec3 holdPosition{};
    anim::Quat holdRotation{};
    int secondHits = 0; ///< `+0x5b`, `CfgObj`'s 5th argument: the hits a flying or held object spends first.
    /// `+0x5e`, `CfgObj`'s 12th argument: the body's `PHYFLAG` layers, who meets it (world_objects::kPhyBlockHumans
    /// and the rest, docs/research/physics.md#layers); its collision body's flags come from it (bodyFlagsOf()).
    std::uint16_t bodyWord = 0;
    std::uint8_t material = 0; ///< `+0x64`, `CfgObj`'s 13th argument: its surface material (impact sounds).
    /// `CfgObj`'s 7th argument: the collision body's centre in the object's frame, metres.
    std::array<float, 3> bodyCentre{};
    /// `+0x78`, `CfgObj`'s 8th argument: the collision box's size (whole extents), metres.
    std::array<float, 3> bodySize{};
    /// `+0x84`, `CfgObj`'s 9th argument: the body's shape (`PHYS`: 0 none, 1 a box, 2 a sphere, 3 an upright
    /// cylinder, 4 a capsule).
    int bodyShape = 0;
    /// `+0x85`, `CfgObj`'s 10th argument (`AXIS`): the local axes a landed object may settle onto, bit 0 x, 1 y, 2 z;
    /// 0 never settles (docs/research/physics.md#settle).
    int axis = 0;
    /// `+0x88`, `CfgObj`'s 11th argument (the scripts' `mass`): the restitution a bounce uses (its attribute 8), 0.1
    /// for nearly every type (docs/research/physics.md#attributes).
    float restitution = 0.0F;
    /// `+0x62`, `CfgObj`'s 6th argument (16 bits): the weight, the body's physics attribute 1; it slows a throw
    /// (docs/research/objects.md#throws). 10 or 50 for most types, 20 for a bottle.
    int weight = 0;
};

/// `TYPE_KNIFE`, the kind (`+0x86`) a throw treats as weighing 1 whatever its weight.
inline constexpr int kObjectKindKnife = 11;

/// `PHYS.OBB` and `PHYS.SPHERE`: the body shapes `Obj_CreatePhysicsBody` makes (docs/research/objects.md#spawning).
inline constexpr int kBodyBox = 1;
inline constexpr int kBodySphere = 2;

/// `TYPE_SPECIAL`: a store's jewellery and other loose loot (docs/research/combat.md#breakables).
inline constexpr int kObjectKindSpecial = 12;

/// The object database (`0x00512c04`): the types `CfgObj` configures, found by name.
///
/// Research: docs/research/objects.md#object-types, docs/references/bindings/config.md#cfgobj
class ObjectTypes {
  public:
    /// The longest name kept (`+0x28`, 26 characters); a longer one is cut.
    static constexpr std::size_t kMaxName = 26;
    /// The longest class name kept (`+0x43`, 20 characters); a longer one is cut.
    static constexpr std::size_t kMaxClassName = 20;

    /// `CfgObj(name, className, hitpoints, ..., objectKind, ...)`: adds a type and returns it. The model hash is the
    /// CRC-32 of the name as given (the scripts' names are lower case). A second type of the same name is added too,
    /// but lookups keep finding the first.
    /// @orig 0x00390f18 Cfg_AddObjectType (unknown)
    const ObjectType& add(std::string_view name, std::string_view className, int hitpoints, int objectKind = 0);
    /// Adds `type` as configured (its name and class cut to their lengths; the hash and index set here).
    const ObjectType& add(ObjectType type);

    /// The type named `name`; null when none is.
    /// @orig 0x003913d8 ObjectDb_FindByName (unknown)
    [[nodiscard]] const ObjectType* find(std::string_view name) const;

    /// Every type, in the order added.
    [[nodiscard]] const std::vector<ObjectType>& all() const { return m_types; }
    /// Forgets every type: a fresh script state configures them again.
    void clear();

  private:
    std::vector<ObjectType> m_types;
    std::unordered_map<std::string, std::size_t> m_byName; // the first type of each name
};

} // namespace coney::world_objects
