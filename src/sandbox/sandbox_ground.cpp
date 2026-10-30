// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_ground.h"

#include <algorithm>
#include <format>
#include <utility>

#include "sandbox/sandbox_layout.h"

namespace coney::sandbox {

std::expected<std::unique_ptr<SandboxGround>, Error> SandboxGround::create(SandboxWorld world,
                                                                           const std::optional<std::string>& spawn) {
    if (world.collision() == nullptr) {
        return fail(ErrorCode::NotFound, "the sandbox layout has nothing solid to stand on");
    }
    // The spawn point, dropped onto the ground below it as a level's start is.
    const auto& spawns = world.layout().spawns;
    const auto found =
        spawn ? std::ranges::find_if(spawns, [&spawn](const auto& s) { return s.name == *spawn; }) : spawns.begin();
    if (found == spawns.end()) {
        std::string known;
        for (const SpawnPoint& s : spawns) {
            known += (known.empty() ? "" : ", ") + s.name;
        }
        return fail(ErrorCode::NotFound, std::format("the sandbox layout has no spawn point \"{}\" (there are: {})",
                                                     spawn.value_or(""), known));
    }
    raycast::Vec3 point{found->position.x, found->position.y, found->position.z + kSpawnDrop};
    if (!raycast::dropToGround(*world.collision(), kSpawnDrop * 2.0F, point)) {
        point.z = found->position.z;
    }
    const human::PlayerStart start{.position = anim::Vec3{point.x, point.y, point.z},
                                   .headingDegrees = found->headingDegrees};
    std::string name = found->name;
    return std::unique_ptr<SandboxGround>(new SandboxGround(std::move(world), start, std::move(name)));
}

SandboxGround::SandboxGround(SandboxWorld world, const human::PlayerStart& start, std::string spawn)
    : m_world(std::move(world)), m_start(start), m_spawn(std::move(spawn)) {}

std::vector<human::Place> SandboxGround::places() const {
    std::vector<human::Place> places;
    for (const SpawnPoint& spawn : m_world.layout().spawns) {
        places.push_back(human::Place{spawn.name, spawn.position, spawn.headingDegrees});
    }
    return places;
}

std::vector<world::SceneTarget> SandboxGround::targets() const {
    std::vector<world::SceneTarget> targets;
    for (const TargetPoint& point : m_world.layout().targets) {
        targets.push_back(
            world::SceneTarget{.feet = point.position, .headingDegrees = point.headingDegrees, .health = point.health});
    }
    return targets;
}

std::vector<human::Place> SandboxGround::fighters() const {
    std::vector<human::Place> fighters;
    for (const FighterPoint& point : m_world.layout().fighters) {
        fighters.push_back(human::Place{point.name, point.position, point.headingDegrees});
    }
    return fighters;
}

std::expected<void, Error> SandboxGround::setWorld(SandboxWorld world) {
    if (world.collision() == nullptr) {
        return fail(ErrorCode::InvalidArgument, "the sandbox would have nothing solid to stand on");
    }
    m_world = std::move(world);
    return {};
}

} // namespace coney::sandbox
