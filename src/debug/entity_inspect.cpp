// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/entity_inspect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>

namespace coney::debug {

const EntityRef* findEntity(std::span<const EntityRef> entities, EntityId id) {
    const auto found = std::ranges::find(entities, id, &EntityRef::id);
    return found != entities.end() ? &*found : nullptr;
}

EntityId nearestEntity(std::span<const EntityRef> entities, anim::Vec3 from, EntityId skip) {
    EntityId best = kNoEntity;
    float bestDistance = 0.0F;
    for (const EntityRef& entity : entities) {
        if (entity.id == skip) {
            continue;
        }
        const float distance = anim::distance(entity.position, from);
        if (best == kNoEntity || distance < bestDistance) {
            best = entity.id;
            bestDistance = distance;
        }
    }
    return best;
}

EntityId stepEntity(std::span<const EntityRef> entities, EntityId current, int step) {
    if (entities.empty()) {
        return kNoEntity;
    }
    const auto count = static_cast<std::ptrdiff_t>(entities.size());
    const auto found = std::ranges::find(entities, current, &EntityRef::id);
    if (found == entities.end()) {
        return step < 0 ? entities.back().id : entities.front().id;
    }
    // Wrapped both ways: the remainder is taken into 0 .. count - 1 before indexing.
    const std::ptrdiff_t index = ((found - entities.begin()) + step) % count;
    return entities[static_cast<std::size_t>(index < 0 ? index + count : index)].id;
}

EntityId lookedAtEntity(std::span<const EntityRef> entities, anim::Vec3 eye, anim::Vec3 target, float maxDegrees,
                        EntityId skip) {
    const anim::Vec3 view = anim::subtract(target, eye);
    if (anim::length(view) <= 1e-6F) {
        return kNoEntity;
    }
    const anim::Vec3 forward = anim::normalise(view);
    const float limit = std::cos(maxDegrees * std::numbers::pi_v<float> / 180.0F);
    EntityId best = kNoEntity;
    float bestCosine = limit;
    for (const EntityRef& entity : entities) {
        if (entity.id == skip) {
            continue;
        }
        const anim::Vec3 to = anim::subtract(anim::add(entity.position, anim::Vec3{0.0F, 0.0F, kLookAtHeight}), eye);
        if (anim::length(to) <= 1e-6F) {
            continue;
        }
        // The cosine of the angle off the view: larger is more direct.
        const float cosine = anim::dot(forward, anim::normalise(to));
        if (cosine >= bestCosine) {
            best = entity.id;
            bestCosine = cosine;
        }
    }
    return best;
}

std::string vecText(anim::Vec3 p) { return std::format("({:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z); }

std::vector<std::string> flatten(std::span<const WatchGroup> groups) {
    std::vector<std::string> lines;
    for (const WatchGroup& group : groups) {
        for (const WatchLine& line : group.lines) {
            lines.push_back(std::format("{} / {}: {}", group.name, line.name, line.value));
        }
    }
    return lines;
}

std::optional<std::string> watchValue(std::span<const WatchGroup> groups, std::string_view group,
                                      std::string_view name) {
    for (const WatchGroup& candidate : groups) {
        if (candidate.name != group) {
            continue;
        }
        for (const WatchLine& line : candidate.lines) {
            if (line.name == name) {
                return line.value;
            }
        }
    }
    return std::nullopt;
}

} // namespace coney::debug
