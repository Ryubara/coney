// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/level_document.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <limits>
#include <utility>

namespace coney::studio {

namespace {

// Half the width and the height of the box a human-sized point is picked by: Coney's choice, about a human's size.
constexpr float kPointHalfWidth = 0.3F;
constexpr float kPointHeight = 1.8F;
// A camera view's box: 0.6 m round its position.
constexpr float kViewHalfSize = 0.3F;

// `name` with its first letter in upper case (`box` gives `Box`).
std::string capitalised(std::string_view name) {
    std::string out(name);
    if (!out.empty()) {
        out.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(out.front())));
    }
    return out;
}

} // namespace

std::string_view entityKindName(EntityKind kind) {
    switch (kind) {
    case EntityKind::Primitive:
        return "Primitive";
    case EntityKind::Spawn:
        return "Spawn point";
    case EntityKind::Target:
        return "Target";
    case EntityKind::Fighter:
        return "AI fighter";
    case EntityKind::View:
        return "Camera view";
    }
    return "Entity";
}

LevelDocument LevelDocument::fromLayout(sandbox::SandboxLayout layout, std::filesystem::path path) {
    LevelDocument document;
    document.m_layout = std::move(layout);
    document.m_path = std::move(path);
    static std::uint64_t made = 0; // the UI thread alone makes documents
    document.m_serial = ++made;
    const sandbox::SandboxLayout& l = document.m_layout;
    // 1. The entities: the primitives numbered per shape (Box 1, Box 2, Ramp 1), then the named points by kind.
    const auto add = [&document](EntityKind kind, std::size_t index, std::string name, anim::Vec3 position) {
        const auto id = static_cast<EntityId>(document.m_entities.size());
        document.m_entities.push_back(
            Entity{.id = id, .kind = kind, .index = index, .name = std::move(name), .position = position});
    };
    std::array<int, 6> perShape{};
    for (std::size_t i = 0; i < l.primitives.size(); ++i) {
        const auto& primitive = l.primitives[i];
        const int number = ++perShape.at(static_cast<std::size_t>(primitive.shape));
        add(EntityKind::Primitive, i, std::format("{} {}", capitalised(sandbox::shapeName(primitive.shape)), number),
            primitive.base);
    }
    for (std::size_t i = 0; i < l.spawns.size(); ++i) {
        add(EntityKind::Spawn, i, l.spawns[i].name, l.spawns[i].position);
    }
    for (std::size_t i = 0; i < l.targets.size(); ++i) {
        add(EntityKind::Target, i, l.targets[i].name, l.targets[i].position);
    }
    for (std::size_t i = 0; i < l.fighters.size(); ++i) {
        add(EntityKind::Fighter, i, l.fighters[i].name, l.fighters[i].position);
    }
    for (std::size_t i = 0; i < l.views.size(); ++i) {
        add(EntityKind::View, i, l.views[i].name, l.views[i].position);
    }
    // 2. The triangles a click is tested against, and each primitive's box from them.
    document.m_pickMesh = sandbox::buildSandboxMesh(l, sandbox::kNoTessellation);
    constexpr float kHuge = std::numeric_limits<float>::max();
    document.m_primitiveBounds.assign(l.primitives.size(),
                                      Bounds{.min = {kHuge, kHuge, kHuge}, .max = {-kHuge, -kHuge, -kHuge}});
    for (const auto& vertex : document.m_pickMesh.vertices) {
        Bounds& box = document.m_primitiveBounds.at(vertex.primitive);
        box.min = {std::min(box.min.x, vertex.position.x), std::min(box.min.y, vertex.position.y),
                   std::min(box.min.z, vertex.position.z)};
        box.max = {std::max(box.max.x, vertex.position.x), std::max(box.max.y, vertex.position.y),
                   std::max(box.max.z, vertex.position.z)};
    }
    return document;
}

std::expected<LevelDocument, Error> LevelDocument::load(const std::filesystem::path& path) {
    auto layout = sandbox::loadSandboxLayout(path);
    if (!layout) {
        return std::unexpected(std::move(layout.error()));
    }
    return fromLayout(std::move(*layout), path);
}

std::string LevelDocument::title() const {
    if (!m_layout.title.empty()) {
        return m_layout.title;
    }
    return m_path.empty() ? std::string("Level") : m_path.stem().string();
}

const Entity* LevelDocument::entity(EntityId id) const { return id < m_entities.size() ? &m_entities[id] : nullptr; }

const sandbox::Primitive* LevelDocument::primitive(const Entity& entity) const {
    return entity.kind == EntityKind::Primitive ? &m_layout.primitives.at(entity.index) : nullptr;
}

std::optional<EntityId> LevelDocument::entityOfPrimitive(std::size_t primitive) const {
    // The primitives come first, in order, so a primitive's entity id is its index.
    if (primitive < m_layout.primitives.size()) {
        return static_cast<EntityId>(primitive);
    }
    return std::nullopt;
}

Bounds LevelDocument::bounds(const Entity& entity) const {
    const anim::Vec3 at = entity.position;
    switch (entity.kind) {
    case EntityKind::Primitive: {
        const Bounds& box = m_primitiveBounds.at(entity.index);
        // A primitive with no triangles (all of it below the ground) is picked by its base point alone.
        return box.min.x <= box.max.x ? box : Bounds{.min = at, .max = at};
    }
    case EntityKind::View:
        return Bounds{.min = {at.x - kViewHalfSize, at.y - kViewHalfSize, at.z - kViewHalfSize},
                      .max = {at.x + kViewHalfSize, at.y + kViewHalfSize, at.z + kViewHalfSize}};
    case EntityKind::Spawn:
    case EntityKind::Target:
    case EntityKind::Fighter:
        break;
    }
    return Bounds{.min = {at.x - kPointHalfWidth, at.y - kPointHalfWidth, at.z},
                  .max = {at.x + kPointHalfWidth, at.y + kPointHalfWidth, at.z + kPointHeight}};
}

} // namespace coney::studio
