// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/spawners.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <ranges>

namespace coney::ai {

namespace {

// The states `GangStartSpawner` may set (`0x00168cd0`'s switch); 6 and 10 only the update sets.
bool settable(int mode) { return (mode >= 0 && mode <= 5) || mode == 7 || mode == 8 || mode == 9 || mode == 11; }

// Milliseconds in a second, for state 2's value.
constexpr std::uint64_t kMsPerSecond = 1000;

// Whether `point` is inside `view`'s cone and range.
bool seen(const SightCone& view, anim::Vec3 point) {
    const anim::Vec3 toPoint = anim::subtract(point, view.eye);
    const float distance = anim::length(toPoint);
    if (distance > view.range) {
        return false;
    }
    if (distance <= 0.0F) {
        return true;
    }
    return anim::dot(toPoint, view.forward) / distance >= std::cos(view.halfAngleRadians);
}

} // namespace

std::optional<anim::Vec3> outOfSightSpot(std::span<const anim::Vec3> spots, anim::Vec3 player, const SightCone& view,
                                         float metres, std::size_t turn) {
    // The unseen spots by how far their distance from the player is from `metres`; ties keep the spots' order.
    std::vector<std::pair<float, anim::Vec3>> unseen;
    for (const anim::Vec3& spot : spots) {
        if (!seen(view, spot)) {
            unseen.emplace_back(std::abs(anim::distance(spot, player) - metres), spot);
        }
    }
    if (unseen.empty()) {
        return std::nullopt;
    }
    std::ranges::stable_sort(unseen, {}, &std::pair<float, anim::Vec3>::first);
    return unseen[turn % std::min(unseen.size(), kOutOfSightSpots)].second;
}

void Spawners::add(const script::SpawnerCall& call) {
    std::vector<Spawner>& spawners = m_spawners[call.gang];
    if (spawners.size() >= kGangSpawners) {
        return;
    }
    Spawner spawner{.call = call, .state = call.state, .value = call.value};
    spawner.nextSpawnMs = m_nowMs;
    spawner.deadlineMs = m_nowMs + static_cast<std::uint64_t>(std::max(call.value, 0)) * kMsPerSecond;
    spawners.push_back(std::move(spawner));
}

void Spawners::start(int gang, std::string_view name, int mode, int value) {
    const auto found = m_spawners.find(gang);
    if (found == m_spawners.end()) {
        return;
    }
    const auto spawner = std::ranges::find_if(
        found->second, [name](const Spawner& candidate) { return candidate.inUse && candidate.call.name == name; });
    if (spawner == found->second.end()) {
        return;
    }
    // The value and the next spawn are written whether or not the mode is taken.
    if (value != -1) {
        spawner->value = value;
    }
    spawner->nextSpawnMs = m_nowMs;
    if (settable(mode)) {
        spawner->state = mode;
        spawner->deadlineMs = m_nowMs + static_cast<std::uint64_t>(std::max(spawner->value, 0)) * kMsPerSecond;
    }
}

void Spawners::update(std::uint64_t nowMs, SpawnerWorld& world) {
    m_nowMs = nowMs;
    for (auto& spawners : m_spawners | std::views::values) {
        for (Spawner& spawner : spawners) {
            if (!spawner.inUse) {
                continue;
            }
            // Its humans that went down or were deleted no longer count against its limit.
            std::erase_if(spawner.humans, [&world](double handle) { return !world.alive(handle); });
            if (!ready(spawner, nowMs, world) || nowMs < spawner.nextSpawnMs ||
                static_cast<int>(spawner.humans.size()) >= spawner.call.maxConcurrent) {
                continue;
            }
            spawnOne(spawner, nowMs, world);
        }
    }
}

const std::vector<Spawner>& Spawners::of(int gang) const {
    static const std::vector<Spawner> kNone;
    const auto found = m_spawners.find(gang);
    return found == m_spawners.end() ? kNone : found->second;
}

const Spawner* Spawners::find(int gang, std::string_view name) const {
    const std::vector<Spawner>& spawners = of(gang);
    const auto found =
        std::ranges::find_if(spawners, [name](const Spawner& spawner) { return spawner.call.name == name; });
    return found == spawners.end() ? nullptr : &*found;
}

bool Spawners::ready(const Spawner& spawner, std::uint64_t nowMs, const SpawnerWorld& world) {
    // Player 1's distance for states 3 and 5. Coney choice: measured in 3D (the page does not say).
    const auto playerDistance = [&world, &spawner]() -> std::optional<float> {
        const std::optional<anim::Vec3> player = world.playerPosition();
        if (!player) {
            return std::nullopt;
        }
        const anim::Vec3 at{spawner.call.position[0], spawner.call.position[1], spawner.call.position[2]};
        return anim::distance(*player, at);
    };
    switch (static_cast<SpawnerState>(spawner.state)) {
    case SpawnerState::On:
    case SpawnerState::DispatchingPolice:
    case SpawnerState::Reinforcements:
    case SpawnerState::OutOfSight:
    case SpawnerState::DispatchingGang:
        return true;
    case SpawnerState::Delayed:
        return nowMs >= spawner.deadlineMs;
    case SpawnerState::NearPlayer: {
        const std::optional<float> distance = playerDistance();
        return distance && *distance <= static_cast<float>(spawner.value);
    }
    case SpawnerState::FarFromPlayer: {
        const std::optional<float> distance = playerDistance();
        return distance && *distance > static_cast<float>(spawner.value);
    }
    // Coney stand-in: the dispatch states (4, 9) wait for crimes Coney does not route yet, and the top-up (11) reads
    // gang limits Coney does not keep, so none of them spawns.
    case SpawnerState::Stopped:
    case SpawnerState::PoliceDispatch:
    case SpawnerState::GangDispatch:
    case SpawnerState::KeepUpNumbers:
        return false;
    }
    return false;
}

int Spawners::pickType(Spawner& spawner) {
    // Coney stand-in: how the original picks from the list is not on the page; Coney takes the non-zero entries in
    // turn, which keeps a run deterministic.
    const auto& types = spawner.call.types;
    for (std::size_t tried = 0; tried < types.size(); ++tried) {
        const std::size_t at = (spawner.nextType + tried) % types.size();
        if (types[at] != 0) {
            spawner.nextType = at + 1;
            return types[at];
        }
    }
    return 0;
}

void Spawners::spawnOne(Spawner& spawner, std::uint64_t nowMs, SpawnerWorld& world) {
    // 6, 7, 8 and 10 place the human out of the camera's sight around the player (outOfSightSpot()'s stand-in, at
    // the value's distance; Coney stand-in: 7 at 0, its spot's distance not being on the page); the others at the
    // spawner, as do these when no spot is out of sight. Coney stand-in: state 7's walk to the gang's first live
    // member is not built, and no door is opened.
    std::array<float, 3> position = spawner.call.position;
    const auto state = static_cast<SpawnerState>(spawner.state);
    if (state == SpawnerState::DispatchingPolice || state == SpawnerState::Reinforcements ||
        state == SpawnerState::OutOfSight || state == SpawnerState::DispatchingGang) {
        const float metres = state == SpawnerState::Reinforcements ? 0.0F : static_cast<float>(spawner.value);
        if (const std::optional<anim::Vec3> spot = world.outOfSight(metres, m_placed++)) {
            position = {spot->x, spot->y, spot->z};
        }
    }
    SpawnRequest request{.gang = spawner.call.gang,
                         .name = std::format("{}{}", spawner.call.name, spawner.made),
                         .type = pickType(spawner),
                         .model = spawner.call.model,
                         .position = position,
                         .heading = spawner.call.heading};
    spawner.nextSpawnMs = nowMs + spawner.call.delayMs;
    const double handle = world.spawn(request);
    if (handle == 0.0) {
        return;
    }
    spawner.humans.push_back(handle);
    ++spawner.made;
    // A total of -1 makes no limit (inferred from the scripts' values).
    if (spawner.call.total >= 0 && spawner.made >= spawner.call.total) {
        spawner.inUse = false;
    }
    if (!spawner.call.callback.empty()) {
        world.spawned(spawner.call.callback, handle);
    }
}

} // namespace coney::ai
