// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/spawners.h"

#include "ai/route_planner.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <numbers>
#include <queue>
#include <ranges>

namespace coney::ai {

namespace {

// The states `GangStartSpawner` may set (`0x00168cd0`'s switch); 6 and 10 only the update sets.
bool settable(int mode) { return (mode >= 0 && mode <= 5) || mode == 7 || mode == 8 || mode == 9 || mode == 11; }

// Milliseconds in a second, for state 2's value.
constexpr std::uint64_t kMsPerSecond = 1000;

// The angle between `forward` (length 1) and the direction from the camera to `point`, radians.
float angleFrom(const PlacementCamera& camera, anim::Vec3 point) {
    const anim::Vec3 toPoint = anim::subtract(point, camera.eye);
    const float distance = anim::length(toPoint);
    if (distance <= 0.0F) {
        return 0.0F;
    }
    return std::acos(std::clamp(anim::dot(toPoint, camera.forward) / distance, -1.0F, 1.0F));
}

// Whether a human on `point` is out of the camera's sight: beyond kOutOfSightFar, or beyond `value` and outside the
// cone. Coney choice: the distance and the angle are taken in 3D (the page does not say).
bool unseen(const PlacementCamera& camera, float value, anim::Vec3 point) {
    const float distance = anim::distance(point, camera.eye);
    return distance > kOutOfSightFar ||
           (distance > value && angleFrom(camera, point) > camera.halfFovRadians + kConeMarginRadians);
}

// The node the search starts from: Coney stand-in for the player's route node (not on the page), the nearest node of
// the polygon under `player`, or the map's nearest node when that polygon has none.
std::optional<std::uint32_t> startNode(const world::PathMap& map, anim::Vec3 player) {
    const auto nodes = map.nodes();
    std::uint32_t first = 0;
    auto count = static_cast<std::uint32_t>(nodes.size());
    if (const std::optional<std::uint32_t> polygon = map.polygonAt(player.x, player.y)) {
        const world::PathPolygon& under = map.polygons()[*polygon];
        if (under.hasNodes && under.nodeCount > 0) {
            first = under.firstNode;
            count = under.nodeCount;
        }
    }
    std::optional<std::uint32_t> nearest;
    float best = 0.0F;
    for (std::uint32_t node = first; node < first + count && node < nodes.size(); ++node) {
        const float distance = anim::distance(nodes[node].position, player);
        if (!nearest || distance < best) {
            nearest = node;
            best = distance;
        }
    }
    return nearest;
}

// One best-first search from `start` toward `goal`: the first node taken off the open list that is unseen.
// Coney choice: the open list is ordered by the straight distance to the goal, and the search fails once it would
// hold more than kMaxSearchNodes (the route planner's limit), the page giving no limit.
std::optional<std::uint32_t> searchOutward(const world::PathMap& map, std::uint32_t start, anim::Vec3 goal,
                                           const PlacementCamera& camera, float value) {
    const auto nodes = map.nodes();
    using Entry = std::pair<float, std::uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
    std::vector<bool> reached(nodes.size(), false);
    open.emplace(anim::distance(nodes[start].position, goal), start);
    reached[start] = true;
    while (!open.empty()) {
        const std::uint32_t node = open.top().second;
        open.pop();
        if (unseen(camera, value, nodes[node].position)) {
            return node;
        }
        for (const world::PathEdge& edge : map.edgesOf(node)) {
            // An edge with the avoid bit is not taken (ai.md#path-planning).
            if (edge.avoid || edge.to >= nodes.size() || reached[edge.to]) {
                continue;
            }
            reached[edge.to] = true;
            open.emplace(anim::distance(nodes[edge.to].position, goal), edge.to);
        }
        if (open.size() > kMaxSearchNodes) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// `direction` turned about the vertical (z) axis by `radians`.
anim::Vec3 turned(anim::Vec3 direction, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return anim::Vec3{(direction.x * c) - (direction.y * s), (direction.x * s) + (direction.y * c), direction.z};
}

} // namespace

std::optional<anim::Vec3> outOfSightNode(const world::PathMap& map, anim::Vec3 player, const PlacementCamera& camera,
                                         float value, const PlacementRandom& random) {
    const std::optional<std::uint32_t> start = startNode(map, player);
    if (!start) {
        return std::nullopt;
    }
    // The first goal straight ahead; then a direction outside the cone, the goal at twice the value. Coney choice:
    // the angle is drawn evenly over the turns outside the cone, about the vertical axis.
    const float cone = std::min(camera.halfFovRadians + kConeMarginRadians, std::numbers::pi_v<float>);
    for (int attempt = 0; attempt < kPlacementTries; ++attempt) {
        anim::Vec3 goal = anim::add(camera.eye, anim::scale(camera.forward, kFirstGoalAhead));
        if (attempt > 0) {
            const float angle = cone + (random() * 2.0F * (std::numbers::pi_v<float> - cone));
            goal = anim::add(camera.eye, anim::scale(turned(camera.forward, angle), 2.0F * value));
        }
        if (const std::optional<std::uint32_t> node = searchOutward(map, *start, goal, camera, value)) {
            return map.nodes()[*node].position;
        }
    }
    return std::nullopt;
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
