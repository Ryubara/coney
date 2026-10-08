// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_crime_services.h"

#include <array>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "scripting/lua_value.h"

namespace coney {

bool LevelCrimeServices::isPlayer(double handle) {
    if (m_humans == nullptr) {
        return false;
    }
    const HumanCreation* human = m_humans->find(handle);
    return human != nullptr && human->playerIndex != 0;
}

std::optional<CrimeGang> LevelCrimeServices::gangOf(double handle) {
    const ai::Brain* brain = m_brains != nullptr ? m_brains->brain(handle) : nullptr;
    if (brain == nullptr || brain->gang() == nullptr) {
        return std::nullopt;
    }
    return CrimeGang{.id = brain->gang()->id(), .kind = brain->gang()->kind()};
}

int LevelCrimeServices::playerOneGang() {
    const ai::Brain* player = m_brains != nullptr ? m_brains->player() : nullptr;
    return player != nullptr && player->gang() != nullptr ? player->gang()->id() : -1;
}

void LevelCrimeServices::callCrimeCallback(const std::string& function, int gang, int type) {
    const std::array<script::Value, 2> args{script::Value(static_cast<double>(gang)),
                                            script::Value(static_cast<double>(type))};
    (void)m_scripts.call(function, args);
}

void LevelCrimeServices::markStoreRobbed(const CrimePosition& at, int offenderGang) {
    if (m_robStore) {
        m_robStore(at, offenderGang);
    }
}

void LevelCrimeServices::moveCrimeScene(const CrimePosition& at) {
    if (m_moveScene) {
        m_moveScene(at);
    }
}

} // namespace coney
