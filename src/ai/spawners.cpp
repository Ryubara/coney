// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/spawners.h"

#include "ai/route_planner.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <numbers>
#include <queue>
#include <ranges>
#include <utility>
#include <vector>

namespace coney::ai {

namespace {

// The states `GangStartSpawner` may set (`0x00168cd0`'s switch); 6 and 10 only the update sets.
bool settable(int mode) { return (mode >= 0 && mode <= 5) || mode == 7 || mode == 8 || mode == 9 || mode == 11; }

// The sphere an off-screen spawner keeps out of sight: 1.6 m above its position, 0.3 m (Coney choice: the radius).
constexpr float kOffScreenHeight = 1.6F;
constexpr float kOffScreenRadius = 0.3F;

// Milliseconds in a second, for state 2's value.
constexpr std::uint64_t kMsPerSecond = 1000;

// `direction` turned about the vertical (z) axis by `radians`.
anim::Vec3 turned(anim::Vec3 direction, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return anim::Vec3{(direction.x * c) - (direction.y * s), (direction.x * s) + (direction.y * c), direction.z};
}

// The outward search's units: 1/16 m.
constexpr float kSearchUnitsPerMetre = 16.0F;

} // namespace

std::optional<std::uint32_t> searchOutward(const RoutePlanner& planner, std::uint32_t start, anim::Vec3 origin,
                                           anim::Vec3 goal, anim::Vec3 coneAxis, float halfAngle, float value) {
    const world::PathMap& map = planner.map();
    const auto nodes = map.nodes();
    if (start >= nodes.size()) {
        return std::nullopt;
    }
    const float cosHalf = std::cos(halfAngle);
    // Whether a node is out of sight: far, or beyond the value outside the cone.
    const auto found = [&](anim::Vec3 at) {
        const anim::Vec3 toNode = anim::subtract(at, origin);
        const float distance = anim::length(toNode);
        if (distance > kOutOfSightFar) {
            return true;
        }
        return distance > value && distance > 0.0F &&
               anim::dot(anim::scale(toNode, 1.0F / distance), coneAxis) < cosHalf;
    };
    // h: 16 x the 3D distance to the goal.
    const auto heuristic = [&](std::uint32_t node) {
        return static_cast<std::uint32_t>(anim::distance(nodes[node].position, goal) * kSearchUnitsPerMetre);
    };
    // The open list by f, then node; g per node; the closed marks.
    using Entry = std::pair<std::uint32_t, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    std::vector<std::uint32_t> g(nodes.size(), std::numeric_limits<std::uint32_t>::max());
    std::vector<bool> closed(nodes.size(), false);
    std::size_t closedCount = 0;
    g[start] = 0;
    open.emplace(heuristic(start), start);
    while (!open.empty()) {
        const std::uint32_t node = open.top().second;
        open.pop();
        // An older entry of a node opened again with a lower g.
        if (closed[node]) {
            continue;
        }
        closed[node] = true;
        if (found(nodes[node].position)) {
            return node;
        }
        if (++closedCount >= kSearchClosedMax) {
            return std::nullopt;
        }
        for (const world::PathEdge& edge : map.edgesOf(node)) {
            if (edge.avoid || edge.to >= nodes.size() || closed[edge.to]) {
                continue;
            }
            const std::uint32_t cost = g[node] + planner.edgeCost(node, edge, edge_flag::kDefaultMask);
            if (cost >= g[edge.to]) {
                continue;
            }
            if (open.size() >= kSearchOpenMax) {
                return std::nullopt;
            }
            g[edge.to] = cost;
            open.emplace(cost + heuristic(edge.to), edge.to);
        }
    }
    return std::nullopt;
}

std::optional<anim::Vec3> outOfSightNode(const RoutePlanner& planner, anim::Vec3 player, const PlacementCamera& camera,
                                         float value, const PlacementRandom& random) {
    const std::optional<std::uint32_t> start = planner.startNode(player);
    if (!start) {
        return std::nullopt;
    }
    const anim::Vec3 origin{camera.eye.x, camera.eye.y, player.z};
    const anim::Vec3 axis{camera.forward.x, camera.forward.y, 0.0F};
    const float half = std::min(camera.halfFovRadians + kConeMarginRadians, std::numbers::pi_v<float>);
    for (int attempt = 0; attempt < kPlacementTries; ++attempt) {
        // A draw before every search; the first search's is unused.
        const float theta = half + (random() * ((2.0F * std::numbers::pi_v<float>)-(2.0F * half)));
        const anim::Vec3 goal = attempt == 0 ? anim::add(origin, anim::scale(camera.forward, kFirstGoalAhead))
                                             : anim::add(origin, anim::scale(turned(axis, theta), 2.0F * value));
        if (const std::optional<std::uint32_t> node = searchOutward(planner, *start, origin, goal, axis, half, value)) {
            return planner.map().nodes()[*node].position;
        }
    }
    return std::nullopt;
}

void Spawners::add(const script::SpawnerCall& call) {
    std::vector<Spawner>& spawners = m_spawners[call.gang];
    if (spawners.size() >= kGangSpawners) {
        return;
    }
    Spawner spawner;
    spawner.call = call;
    spawner.state = call.state;
    spawner.value = call.value;
    // GangAddSpawner's limit is the field GangSetMaxConcurrent writes: a negative one makes waves too.
    spawner.maxConcurrent = call.maxConcurrent;
    // GangAddSpawner sets the off-screen switch for state 11 only.
    spawner.offScreen = call.state == static_cast<int>(SpawnerState::KeepUpNumbers);
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
            // Ready by its state, or (in any state) while a wave runs.
            if (!spawner.waveRunning && !ready(spawner, nowMs, world)) {
                continue;
            }
            // Waves: with none running, the next starts once every human of the last is gone.
            if (spawner.maxConcurrent < 0 && !spawner.waveRunning) {
                if (!spawner.humans.empty()) {
                    continue;
                }
                spawner.waveRunning = true;
            }
            // The gates: an off-screen spawner's spot unseen, room under the limit, the next spawn time reached.
            if (spawner.offScreen) {
                const anim::Vec3 spot{spawner.call.position[0], spawner.call.position[1],
                                      spawner.call.position[2] + kOffScreenHeight};
                if (world.seen(spot, kOffScreenRadius)) {
                    continue;
                }
            }
            if (!roomFor(spawner) || nowMs < spawner.nextSpawnMs) {
                continue;
            }
            spawnOne(spawner, nowMs, world);
            // A wave ends on the spawn that fills it.
            if (spawner.waveRunning && std::cmp_greater_equal(spawner.humans.size(), -spawner.maxConcurrent)) {
                spawner.waveRunning = false;
            }
        }
    }
}

bool Spawners::roomFor(const Spawner& spawner) {
    // State 11 ignores the limit; a negative limit -n is a wave's n.
    if (spawner.state == static_cast<int>(SpawnerState::KeepUpNumbers)) {
        return true;
    }
    const int limit = spawner.maxConcurrent < 0 ? -spawner.maxConcurrent : spawner.maxConcurrent;
    return std::cmp_less(spawner.humans.size(), limit);
}

Spawner* Spawners::named(int gang, std::string_view name) {
    const auto found = m_spawners.find(gang);
    if (found == m_spawners.end()) {
        return nullptr;
    }
    const auto spawner =
        std::ranges::find_if(found->second, [name](const Spawner& candidate) { return candidate.call.name == name; });
    return spawner == found->second.end() ? nullptr : &*spawner;
}

void Spawners::setMaxConcurrent(int gang, std::string_view name, int count) {
    if (Spawner* spawner = named(gang, name); spawner != nullptr) {
        spawner->maxConcurrent = static_cast<std::int16_t>(count);
    }
}

void Spawners::setMustBeOffScreen(int gang, std::string_view name, bool on) {
    if (Spawner* spawner = named(gang, name); spawner != nullptr) {
        spawner->offScreen = on;
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
    // The index is not reset by GangAddSpawner, so a new spawner takes its second type first when it has one.
    const auto& types = spawner.call.types;
    ++spawner.typeIndex;
    if (spawner.typeIndex >= types.size() || types.at(spawner.typeIndex) == 0) {
        spawner.typeIndex = 0;
    }
    return types.at(spawner.typeIndex);
}

void Spawners::spawnOne(Spawner& spawner, std::uint64_t nowMs, SpawnerWorld& world) {
    // 6, 8 and 10 place the human out of the camera's sight (outOfSightNode()), and nothing spawns this update when
    // no spot is found; the others stand at the spawner. Coney stand-in: 7's placement (0x001679e8) is not on the
    // page, so it is placed as 8 with a value of 0, and its walk to the gang's first live member is not built; no
    // door is opened.
    std::array<float, 3> position = spawner.call.position;
    const auto state = static_cast<SpawnerState>(spawner.state);
    if (state == SpawnerState::DispatchingPolice || state == SpawnerState::Reinforcements ||
        state == SpawnerState::OutOfSight || state == SpawnerState::DispatchingGang) {
        const float value = state == SpawnerState::Reinforcements ? 0.0F : static_cast<float>(spawner.value);
        const std::optional<anim::Vec3> spot = world.outOfSight(value, spawner.call.gang);
        if (!spot) {
            return;
        }
        position = {spot->x, spot->y, spot->z};
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
        world.spawned(spawner.call.callback, handle, spawner.call.gang, spawner.call.name);
    }
}

} // namespace coney::ai
