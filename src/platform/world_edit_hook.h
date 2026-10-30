// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace coney::raycast {
class CollisionMesh;
} // namespace coney::raycast

namespace coney::platform {

class SceneLighting;
class WorldSet;

/// The hook through which the debug tools' world edits (docs/guides/running-viewers.md#world-edits) change a level's
/// streamed worlds and collision: sectors drawn apart, a world hidden, a building cut out of the sector atomics and
/// moved, collision added. The game offers it, the debug tools implement it (platform/world_edit.h), and the game links
/// nothing of theirs (docs/guides/building.md#debug-tools). With none set, the world viewer and the play mode load,
/// stream and draw exactly as without it. A hook changes what is drawn through the world set
/// (WorldSet::setDrawn()) and through the render's object and after-world passes, never inside the ported render
/// (WorldRenderer::render()).
/// @coney-only the debug tools' world edits, an experiment for Coney Studio; the original has none
class WorldEditHook {
  public:
    virtual ~WorldEditHook() = default;

    /// After each fixed step, counted from 0 at the first step of the world viewer or of play: `worlds` may have read
    /// or freed parts since the last call, and `collision` is the level's mesh (null in the world viewer without a
    /// level file). The hook may change the loaded atomics (cut, move, place) and the mesh's extra triangles.
    virtual void afterStep(std::uint64_t step, WorldSet& worlds, raycast::CollisionMesh* collision) = 0;
    /// Whether the level's background (sky, clouds, skyline) and light glows are drawn: not round sectors moved away
    /// from where they belong.
    [[nodiscard]] virtual bool drawsBackground() const = 0;
    /// Whether the world viewer reads every part before its first frame, so nothing streams later.
    [[nodiscard]] virtual bool loadsEverything() const = 0;
    /// Draws the hook's own atomics of world `world` (0 the `s` world, 1 the `d` world), in the world pass's camera and
    /// render states, lit by `lighting` as the sectors are: the `s` world's first among the objects drawn between the
    /// two worlds, the `d` world's first after them.
    virtual void drawAfterWorld(std::size_t world, SceneLighting& lighting) = 0;
};

} // namespace coney::platform
