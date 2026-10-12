// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"

// What the Entity inspector page (docs/guides/debug-menu.md#entity-inspector) reads of a play mode: the entities in
// the scene (humans and dynamic objects), and for one of them everything Coney knows, grouped as watch lines. The mode
// implements EntityInspect over its own humans and objects; the page and the selection rules here are the same in
// every front end. Read-only, and Coney's own debug tooling: the original has nothing like it.

namespace coney::debug {

/// An entity's id as the inspector names it, stable while the entity lasts; kNoEntity names none.
using EntityId = std::uint64_t;
inline constexpr EntityId kNoEntity = 0;

/// What an entity is.
enum class EntityKind : std::uint8_t {
    Player, ///< Player 1's human.
    Human,  ///< Any other human: an AI human (a fighter, the scripts' cast) or a sandbox target.
    Object, ///< A dynamic object: a world object, a door, a pickup.
};

/// One entity in the scene as the inspector lists it.
struct EntityRef {
    EntityId id = kNoEntity;
    EntityKind kind = EntityKind::Human;
    std::string label;   ///< A short name: "player 1 (rembrandt)", "AI 3 gang", "door 2".
    anim::Vec3 position; ///< The feet of a human, an object's origin; game axes.
};

/// One watched value: its name and its value as text.
struct WatchLine {
    std::string name;
    std::string value;
};

/// A named group of watch lines (Identity, Transform, Locomotion, ...).
struct WatchGroup {
    std::string name;
    std::vector<WatchLine> lines;
};

/// The entities of the mode the player plays in, for the Entity inspector. Called between steps and while drawing;
/// nothing here changes the game.
class EntityInspect {
  public:
    virtual ~EntityInspect() = default;

    /// The entities now: player 1 first, then the other humans, then the objects.
    [[nodiscard]] virtual std::vector<EntityRef> entities() const = 0;
    /// Everything known about entity `id`, grouped; empty when it is gone.
    [[nodiscard]] virtual std::vector<WatchGroup> describe(EntityId id) const = 0;
};

/// The entity with `id` in `entities`; null when it is not there.
[[nodiscard]] const EntityRef* findEntity(std::span<const EntityRef> entities, EntityId id);

/// The entity nearest `from` (by distance in 3D), not counting `skip`; kNoEntity when there is none.
[[nodiscard]] EntityId nearestEntity(std::span<const EntityRef> entities, anim::Vec3 from, EntityId skip = kNoEntity);

/// The entity `step` places after `current` in `entities` (negative: before it), wrapping round; the first (or, for a
/// negative step, the last) when `current` is not there; kNoEntity when the list is empty.
[[nodiscard]] EntityId stepEntity(std::span<const EntityRef> entities, EntityId current, int step);

/// How far above an entity's position the camera's look is aimed at it: a standing human's chest.
inline constexpr float kLookAtHeight = 1.0F;

/// The entity the camera at `eye` looking at `target` looks at most directly: the smallest angle between the view and
/// the line to its position raised by kLookAtHeight, within `maxDegrees`, not counting `skip`; kNoEntity when none is
/// that close to the view.
[[nodiscard]] EntityId lookedAtEntity(std::span<const EntityRef> entities, anim::Vec3 eye, anim::Vec3 target,
                                      float maxDegrees, EntityId skip = kNoEntity);

/// A point as the inspector prints it: "(1.00, 2.00, 0.50)".
[[nodiscard]] std::string vecText(anim::Vec3 p);

/// The groups as lines "Group / name: value", one each, for logs and the tests.
[[nodiscard]] std::vector<std::string> flatten(std::span<const WatchGroup> groups);

/// The value of `name` in group `group` of `groups`; nothing when it is not there.
[[nodiscard]] std::optional<std::string> watchValue(std::span<const WatchGroup> groups, std::string_view group,
                                                    std::string_view name);

} // namespace coney::debug
