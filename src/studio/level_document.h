// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "sandbox/sandbox_layout.h"
#include "sandbox/sandbox_mesh.h"

// The level open in Coney Studio, as the Hierarchy, the Inspector and the Level view's picking see it: one entity per
// thing the level places. The prototype's levels are sandbox layouts (docs/guides/sandbox.md#the-layout-format), which
// are flat lists, so the entities are flat too: the layout's primitives in their order, then its spawn points,
// targets, AI fighters and camera views. Parenting, groups and prefabs wait on Coney's own level format
// (docs/guides/studio/editor-experience.md#level-format-needs). Coney's own; the original has no editor.

namespace coney::studio {

/// What an entity is: one per kind of statement a layout places.
enum class EntityKind : std::uint8_t {
    Primitive, ///< A box, ramp, stairs, cylinder, sphere or capsule (a `repeat` gives one entity per copy).
    Spawn,     ///< A `spawn`: where the player can start.
    Target,    ///< A `target`: a passive human to fight.
    Fighter,   ///< A `fighter`: an AI human that fights back.
    View,      ///< A `view`: a camera viewpoint.
};

/// The name the Inspector shows for `kind` (`Primitive`, `Spawn point`, ...).
[[nodiscard]] std::string_view entityKindName(EntityKind kind);

/// An entity's id: stable while the level is open, never a pointer (docs/guides/network-architecture.md#checklist).
using EntityId = std::uint32_t;

/// One entity of the level.
struct Entity {
    EntityId id = 0;
    EntityKind kind = EntityKind::Primitive;
    std::size_t index = 0; ///< Into the layout's vector of its kind.
    std::string name;      ///< The statement's name, or for a primitive its shape and number (`Box 3`).
    anim::Vec3 position;   ///< Its base point or feet, game axes (metres, z up).
};

/// An axis-aligned box in game axes.
struct Bounds {
    anim::Vec3 min;
    anim::Vec3 max;
};

/// A level opened from a sandbox layout.
class LevelDocument {
  public:
    /// The document of `layout`, read from the file `path` (kept for Run in Coney and the title; may be empty).
    [[nodiscard]] static LevelDocument fromLayout(sandbox::SandboxLayout layout, std::filesystem::path path = {});
    /// Reads and parses the layout at `path`, then fromLayout(). Fails as sandbox::loadSandboxLayout() does.
    [[nodiscard]] static std::expected<LevelDocument, Error> load(const std::filesystem::path& path);

    /// The layout as parsed.
    [[nodiscard]] const sandbox::SandboxLayout& layout() const { return m_layout; }
    /// The layout file it was read from; empty for one made from text.
    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }
    /// A number no other document made in this process has, from 1: what a renderer caches its build of a level by.
    [[nodiscard]] std::uint64_t serial() const { return m_serial; }
    /// The level's name for the Hierarchy's top row: the layout's title, else its file name.
    [[nodiscard]] std::string title() const;
    /// Every entity, in the Hierarchy's order; an entity's id is its place in this list.
    [[nodiscard]] const std::vector<Entity>& entities() const { return m_entities; }
    /// The entity `id`, or null.
    [[nodiscard]] const Entity* entity(EntityId id) const;
    /// The primitive behind `entity`, or null when it is not one.
    [[nodiscard]] const sandbox::Primitive* primitive(const Entity& entity) const;
    /// The untessellated triangles of every primitive, each tagged with its primitive: what a click is tested against.
    [[nodiscard]] const sandbox::SandboxMesh& pickMesh() const { return m_pickMesh; }
    /// The entity of the layout's primitive `primitive`, or nothing.
    [[nodiscard]] std::optional<EntityId> entityOfPrimitive(std::size_t primitive) const;
    /// The box round `entity`: a primitive's triangles, or for a point a human-sized box (0.6 m wide, 1.8 m high)
    /// round its feet; a camera view's is 0.6 m round its position.
    [[nodiscard]] Bounds bounds(const Entity& entity) const;

  private:
    // Only fromLayout() makes one.
    LevelDocument() = default;

    sandbox::SandboxLayout m_layout;
    std::filesystem::path m_path;
    std::uint64_t m_serial = 0;
    std::vector<Entity> m_entities;
    sandbox::SandboxMesh m_pickMesh;
    std::vector<Bounds> m_primitiveBounds; // by primitive index
};

} // namespace coney::studio
