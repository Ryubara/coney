// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/object_types.h"

#include <utility>

#include "core/name_hash.h"

namespace coney::world_objects {

const ObjectType& ObjectTypes::add(std::string_view name, std::string_view className, int hitpoints) {
    ObjectType type;
    type.name = std::string(name.substr(0, kMaxName));
    type.className = std::string(className.substr(0, kMaxClassName));
    type.hitpoints = hitpoints;
    type.modelHash = crc32(type.name);
    type.index = m_types.size();
    // The original's hash table finds the first of a name; keep that one.
    m_byName.try_emplace(type.name, type.index);
    m_types.push_back(std::move(type));
    return m_types.back();
}

const ObjectType* ObjectTypes::find(std::string_view name) const {
    const auto found = m_byName.find(std::string(name));
    return found == m_byName.end() ? nullptr : &m_types[found->second];
}

void ObjectTypes::clear() {
    m_types.clear();
    m_byName.clear();
}

} // namespace coney::world_objects
