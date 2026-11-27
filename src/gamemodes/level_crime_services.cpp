// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_crime_services.h"

#include <array>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "scripting/lua_value.h"

namespace coney {

int LevelCrimeServices::playerIndex(double handle) {
    const HumanCreation* human = m_humans != nullptr ? m_humans->find(handle) : nullptr;
    return human != nullptr && (human->playerIndex == 1 || human->playerIndex == 2) ? human->playerIndex - 1 : -1;
}

int LevelCrimeServices::brainType(double handle) {
    const ai::Brain* brain = m_brains != nullptr ? m_brains->brain(handle) : nullptr;
    return brain != nullptr ? static_cast<int>(brain->type()) : -1;
}

std::optional<double> LevelCrimeServices::gangLeader(double offender) {
    const ai::Brain* brain = m_brains != nullptr ? m_brains->brain(offender) : nullptr;
    const ai::Brain* leader = brain != nullptr && brain->gang() != nullptr ? brain->gang()->leader() : nullptr;
    return leader != nullptr ? std::optional<double>(leader->handle()) : std::nullopt;
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

std::optional<CrimeStore> LevelCrimeServices::nearestStore(const CrimePosition& at) {
    return m_stores.nearest ? m_stores.nearest(at) : std::nullopt;
}

void LevelCrimeServices::robStore(const CrimePosition& at) {
    if (m_stores.rob) {
        m_stores.rob(at);
    }
}

void LevelCrimeServices::claimStore(const CrimePosition& at, int gang) {
    if (m_stores.claim) {
        m_stores.claim(at, gang);
    }
}

void LevelCrimeServices::scoreCrime(double human, int event) {
    if (m_stores.scoreCrime) {
        m_stores.scoreCrime(human, event);
    }
}

void LevelCrimeServices::moveCrimeScene(const CrimePosition& at) {
    if (m_moveScene) {
        m_moveScene(at);
    }
}

} // namespace coney
