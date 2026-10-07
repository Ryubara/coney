// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_object_services.h"

#include <array>
#include <cstddef>
#include <optional>

#include "effects/effect_culling.h"
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

void LevelObjectServices::setPlayers(GameState* state, CreatedHumans* humans) {
    m_state = state;
    m_humans = humans;
    m_crimes.setHumans(humans);
}

int LevelObjectServices::playerOf(double human) const {
    if (m_humans == nullptr) {
        return -1;
    }
    const HumanCreation* made = m_humans->find(human);
    return made != nullptr && (made->playerIndex == 1 || made->playerIndex == 2) ? made->playerIndex - 1 : -1;
}

void LevelObjectServices::reportCrime(int type, anim::Vec3 at, double offender) {
    if (m_state != nullptr) {
        m_state->player.crimes.report(m_crimes, type, CrimePosition{at.x, at.y, at.z}, offender, 0.0, true, 0,
                                      m_scripts.now());
    }
}

void LevelObjectServices::scoreEvent(double human, int category, int event) {
    const int player = playerOf(human);
    if (m_state != nullptr && player >= 0 && category >= 0 && event >= 0) {
        m_state->player.stats.add(player, static_cast<std::size_t>(category), static_cast<std::size_t>(event));
    }
}

void LevelObjectServices::countPaneBroken(double breaker) { scoreEvent(breaker, 4, 10); }

namespace {

// The most sprites a shatter makes: twice its capped count of 79 tries (docs/research/objects.md#shatter).
constexpr std::size_t kShatterMostShards = 158;
// The shatter's near test: a camera within 15 m of the pane, the pane in its view with a 10 m margin.
constexpr float kShardRange = 15.0F;
constexpr float kShardViewMargin = 10.0F;

} // namespace

bool LevelObjectServices::shardsWanted(anim::Vec3 centre) {
    if (m_particles == nullptr || !m_particles->hasRoom(kShatterMostShards)) {
        return false;
    }
    const std::optional<camera::CameraView> view = m_view ? m_view() : std::nullopt;
    return !view || effects::effectNearView(*view, centre, kShardRange, kShardViewMargin);
}

void LevelObjectServices::spawnShard(anim::Vec3 at, float size, std::uint32_t colour) {
    if (m_particles != nullptr) {
        m_particles->spawnShard(at, size, colour);
    }
}

void LevelObjectServices::freeCarStereos(anim::Vec3 at, float radius) {
    if (m_cars == nullptr) {
        return;
    }
    for (const world_objects::Car& car : m_cars->all()) {
        if (anim::distance(world_objects::Cars::stereoPosition(car), at) <= radius) {
            m_cars->freeStereo(car.handle);
        }
    }
}

void LevelObjectServices::dust(anim::Vec3 at, float /*radius*/) {
    if (m_particles != nullptr) {
        m_particles->spawn("sub_shack_puff", at);
    }
}

void LevelObjectServices::burst(anim::Vec3 at) {
    if (m_particles != nullptr) {
        m_particles->spawn("sub_shack_puff", at);
    }
}

} // namespace coney
