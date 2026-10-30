// SPDX-License-Identifier: GPL-3.0-or-later
// STUB for the test commit: the tests of sandbox_ground_test.cpp compile against it and fail.
#include "sandbox/sandbox_ground.h"

#include <utility>

namespace coney::sandbox {

std::expected<std::unique_ptr<SandboxGround>, Error> SandboxGround::create(SandboxWorld world,
                                                                           const std::optional<std::string>& spawn) {
    (void)spawn;
    return std::unique_ptr<SandboxGround>(new SandboxGround(std::move(world), human::PlayerStart{}, {}));
}

SandboxGround::SandboxGround(SandboxWorld world, const human::PlayerStart& start, std::string spawn)
    : m_world(std::move(world)), m_start(start), m_spawn(std::move(spawn)) {}

std::vector<human::Place> SandboxGround::places() const { return {}; }

std::vector<world::SceneTarget> SandboxGround::targets() const { return {}; }

std::vector<human::Place> SandboxGround::fighters() const { return {}; }

std::expected<void, Error> SandboxGround::setWorld(SandboxWorld world) {
    m_world = std::move(world);
    return {};
}

} // namespace coney::sandbox
