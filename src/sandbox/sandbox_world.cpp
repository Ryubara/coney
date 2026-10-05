// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_world.h"

#include <format>
#include <utility>
#include <vector>

#include "raycast/collision_builder.h"

namespace coney::sandbox {

std::expected<SandboxWorld, Error> SandboxWorld::build(SandboxLayout layout) {
    SandboxWorld world;
    // The collision mesh, then the drawn geometry with its light.
    const std::vector<raycast::BuildTriangle> solid = sandboxCollisionTriangles(layout);
    if (solid.size() > raycast::kMaxCollisionTriangles) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("the layout's solid primitives make {} collision triangles; at most {} fit",
                                solid.size(), raycast::kMaxCollisionTriangles));
    }
    if (!solid.empty()) {
        auto collision = raycast::buildCollisionMesh(solid, kSandboxCellSize);
        if (!collision) {
            return std::unexpected(std::move(collision.error()));
        }
        world.m_collision = std::move(*collision);
    }
    world.m_mesh = buildSandboxMesh(layout, layout.tessellation);
    world.m_bake = bakeLighting(world.m_mesh, layout);
    world.m_layout = std::move(layout);
    return world;
}

std::expected<SandboxWorld, Error> SandboxWorld::load(const std::filesystem::path& folder,
                                                      std::string_view nameOrPath) {
    auto path = findSandboxLayout(folder, nameOrPath);
    if (!path) {
        return std::unexpected(std::move(path.error()));
    }
    auto layout = loadSandboxLayout(*path);
    if (!layout) {
        return std::unexpected(std::move(layout.error()));
    }
    auto world = build(std::move(*layout));
    if (!world) {
        return fail(world.error().code, std::format("{}: {}", path->string(), world.error().message));
    }
    world->m_folder = path->parent_path();
    return world;
}

} // namespace coney::sandbox
