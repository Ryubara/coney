// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/level_object_services.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "effects/effect_culling.h"
#include "gamemodes/level_start.h"
#include "scripting/lua_value.h"

namespace coney {

void LevelObjectServices::playSound(std::uint32_t nameHash, anim::Vec3 at) {
    if (m_sounds != nullptr) {
        m_sounds->playSound(nameHash, at);
    }
}

void LevelObjectServices::playMaterialPair(std::uint8_t a, std::uint8_t b, anim::Vec3 at, float volume) {
    if (m_sounds != nullptr) {
        m_sounds->playMaterialPair(a, b, at, volume);
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

void LevelObjectServices::robStore(anim::Vec3 at, int gang) {
    constexpr int kStoreActivity = 14;
    constexpr float kStoreReach = 10.0F;
    constexpr float kStrobeReach = 6.0F;
    // The nearest store flag within reach of the break-in.
    const world_objects::WorldFlag* store = nullptr;
    float best = kStoreReach * kStoreReach;
    for (const world_objects::WorldFlag& flag : m_flags.all()) {
        if (flag.kind != kStoreActivity) {
            continue;
        }
        const anim::Vec3 d{flag.position[0] - at.x, flag.position[1] - at.y, flag.position[2] - at.z};
        if (const float distance = anim::dot(d, d); distance <= best) {
            best = distance;
            store = &flag;
        }
    }
    if (store == nullptr) {
        return;
    }
    // Robbed (group bit 16), by the offender's gang in bits 18-22 (31: none).
    if (world_objects::WorldFlag* robbed = m_flags.find(store->handle)) {
        constexpr std::uint32_t kRobbedBit = 1U << 16;
        constexpr std::uint32_t kGangShift = 18;
        constexpr std::uint32_t kGangMask = 0x1fU << kGangShift;
        const std::uint32_t gangBits = (static_cast<std::uint32_t>(gang < 0 ? 31 : gang) << kGangShift) & kGangMask;
        robbed->kind2 =
            static_cast<int>((static_cast<std::uint32_t>(robbed->kind2) & ~kGangMask) | kRobbedBit | gangBits);
    }
    if (m_particles == nullptr) {
        return;
    }
    // Its alarm: the strobe emitter nearest the flag.
    const anim::Vec3 flagAt{store->position[0], store->position[1], store->position[2]};
    if (effects::ParticleSystem* strobe = m_particles->nearestNamed("strobe", flagAt, kStrobeReach)) {
        effects::ParticleSystems::setEmitting(*strobe, true);
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

void LevelObjectServices::setModel(double object, std::uint32_t modelHash) {
    if (world_objects::SpawnRecord* record = m_records != nullptr ? m_records->find(object) : nullptr) {
        record->model = modelHash;
    }
}

void LevelObjectServices::setValue(double object, std::uint32_t value) {
    if (world_objects::SpawnRecord* record = m_records != nullptr ? m_records->find(object) : nullptr) {
        record->money = value;
    }
}

double LevelObjectServices::spawnObject(std::string_view type, anim::Vec3 at, anim::Quat rotation) {
    const double handle = m_scripts.nextObjectHandle();
    if (m_records == nullptr || handle == world_objects::kNoObject) {
        return world_objects::kNoObject;
    }
    world_objects::SpawnRecord record;
    record.handle = handle;
    record.typeName = std::string(type);
    record.position = {at.x, at.y, at.z};
    record.rotation = {rotation.x, rotation.y, rotation.z, rotation.w};
    return m_records->add(std::move(record)) != nullptr ? handle : world_objects::kNoObject;
}

} // namespace coney
