// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_object_services.h"

#include <array>
#include <optional>

#include "gamemodes/level_start.h"
#include "scripting/lua_value.h"

namespace coney {

void LevelObjectServices::playSound(std::uint32_t nameHash, anim::Vec3 at) {
    if (m_sounds != nullptr) {
        m_sounds->playSound(nameHash, at);
    }
}

void LevelObjectServices::playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at) {
    if (m_sounds != nullptr) {
        m_sounds->playMaterialPair(a, b, at);
    }
}

void LevelObjectServices::lockPickClick(double human) {
    if (m_sounds != nullptr) {
        m_sounds->lockPickClick(human);
    }
}

void LevelObjectServices::callScript(std::string_view function, double human, double door) {
    if (function.empty()) {
        return;
    }
    const std::array<script::Value, 2> args{script::Value(human), script::Value(door)};
    (void)m_scripts.call(function, args);
}

void LevelObjectServices::moveCrimeSceneFlag(anim::Vec3 at) {
    const std::optional<double> handle = m_flags.findByName(kCrimeSceneFlag);
    if (!handle) {
        return;
    }
    if (world_objects::WorldFlag* flag = m_flags.find(*handle)) {
        flag->position = {at.x, at.y, at.z};
    }
}

} // namespace coney
