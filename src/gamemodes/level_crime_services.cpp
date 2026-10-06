// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_crime_services.h"

#include <array>

#include "scripting/lua_value.h"

namespace coney {

bool LevelCrimeServices::isPlayer(double handle) {
    if (m_humans == nullptr) {
        return false;
    }
    const HumanCreation* human = m_humans->find(handle);
    return human != nullptr && human->playerIndex != 0;
}

void LevelCrimeServices::callCrimeCallback(const std::string& function, int gang, int type) {
    const std::array<script::Value, 2> args{script::Value(static_cast<double>(gang)),
                                            script::Value(static_cast<double>(type))};
    (void)m_scripts.call(function, args);
}

void LevelCrimeServices::moveCrimeScene(const CrimePosition& at) {
    if (m_moveScene) {
        m_moveScene(at);
    }
}

} // namespace coney
