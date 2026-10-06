// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace coney::world_objects {

/// One object type of the object database, as `CfgObj` makes it. Coney keeps the fields a spawn and a model need;
/// the binding's other arguments stay in the recorded calls until something uses them.
struct ObjectType {
    std::string name;            ///< `+0x28`: the name scripts spawn (`dyn_s_wwheel_a`), at most kMaxName characters.
    std::string className;       ///< `+0x43`: the class (`simple_object`), at most kMaxClassName characters.
    int hitpoints = 0;           ///< `+0x58`: a door's or barrier's hitpoints.
    std::uint32_t modelHash = 0; ///< `+0x8c`: CRC-32 of the name, the Object List key of its model.
    std::size_t index = 0;       ///< `+0x60`: the record's own index.
    int objectKind = 0;          ///< `+0x86`: the kind (`TYPE_BAT`, `TYPE_HAT`...), `CfgObj`'s 20th argument.
};

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
