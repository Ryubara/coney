// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "core/error.h"
#include "debug/world_split.h"
#include "platform/world_atomic.h"
#include "platform/world_edit_hook.h"
#include "raycast/collision_builder.h"
#include "world/debug_camera.h"
#include "world/world_streams.h"

// @coney-only the debug tools' world edits (docs/guides/running-viewers.md#world-edits), an experiment for Coney
// Studio (docs/research/world-split.md); the original has none.

namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

/// A sector atomic's plain geometry in the game's axes (z up, metres), where its frame places it: what the world edits'
/// box rules and collision builders read (debug/world_split.h).
struct GameMesh {
    std::vector<debug::Vec3> positions;
    std::vector<debug::MeshTriangle> triangles;
};

/// The plain geometry of `atomic` (after WorldAtomic::unpack()) in the game's axes, placed by its frame's translation;
/// empty when it has none.
[[nodiscard]] GameMesh gameMeshOf(const rw::Atomic* atomic);

/// A sector's box (RenderWare's axes, y up) in the game's axes.
[[nodiscard]] debug::GameBox gameBoxOf(const world::Box& box);

/// The world edits of a file (debug::WorldEdits) applied to a level's streamed worlds and collision, as the world
/// viewer and the play mode call them (WorldEditHook):
///
/// - `hide`: that world's sectors are not drawn.
/// - `spread`: every loaded sector's atomic is scaled about its box's centre and moved to its cell of a grid
///   (debug::spreadLayout()), the `s` world's sectors first, then a row left empty, then the `d` world's; every sector
///   is drawn, without the level's background, and the viewer reads every part before its first frame.
/// - `cut`: once every sector whose box meets the cut's box is loaded, the triangles the box takes are cut out of each
///   of those atomics (WorldAtomic::cut()) into pieces the editor owns, drawn after their world's sectors, moved by
///   the cut's keys (debug::offsetAt()); its collision triangles wholly in the box are switched off in the level's mesh
///   and added back as extra triangles, moved with it.
/// - `roof`: once the sectors under the box are loaded, the up-facing visible triangles whose centroid the box holds
///   become extra collision triangles.
///
/// A part the streamer frees and reads again comes back as the disc has it: its sectors are placed on the spread again
/// and cut again by every cut already made, whose pieces the editor keeps, so nothing shows twice.
///
/// The extra triangles replace any the mesh had (the debug menus' Spawner's), so the two do not mix.
class WorldEditor final : public WorldEditHook {
  public:
    /// Applies `edits`; `print` receives one line per edit made (counts only).
    WorldEditor(debug::WorldEdits edits, std::function<void(std::string_view)> print);

    void afterStep(std::uint64_t step, WorldSet& worlds, raycast::CollisionMesh* collision) override;
    [[nodiscard]] bool drawsBackground() const override { return !m_edits.spread.has_value(); }
    [[nodiscard]] bool loadsEverything() const override { return m_edits.spread.has_value(); }
    void drawAfterWorld(std::size_t world, SceneLighting& lighting) override;

    /// Whether world `world` (in load order: 0 the `s` world, 1 the `d` world) is drawn: not when `hide` names it.
    [[nodiscard]] bool drawsWorld(std::size_t world) const;
    /// Whether every loaded sector is drawn whatever the view sees: the spread moved them off their boxes.
    [[nodiscard]] bool drawsEverySector() const { return m_edits.spread.has_value(); }
    /// The world viewer's camera as the `camera` statement places it (RenderWare's axes), or nothing.
    [[nodiscard]] std::optional<world::DebugCamera> viewerCamera() const;
    /// The collision wireframe's reach when `debugdraw collision` asks for it.
    [[nodiscard]] std::optional<float> collisionWire() const { return m_edits.collisionWire; }
    /// One line of counts: sectors placed, pieces cut and their triangles, collision triangles moved and added.
    [[nodiscard]] std::string summary() const;

  private:
    // One piece cut out of a sector atomic, with where its frame was.
    struct Piece {
        std::size_t cut = 0;
        std::size_t world = 0;
        WorldAtomic atomic;
        world::Vec3 home; // the frame's translation as cut
    };

    // One sector a cut has been made in: its atomic and triangle count after the cuts, which a reread part's differ
    // from.
    struct CutSector {
        std::size_t world = 0;
        std::uint32_t sector = 0;
        const rw::Atomic* atomic = nullptr;
        std::int32_t triangles = 0;
    };

    // Places every loaded sector of the spread that is not in its cell.
    void placeSpread(WorldSet& worlds);
    // Cuts again every sector a cut was made in whose part has been read again since.
    void recutReread(WorldSet& worlds);
    // Remembers sector `sector` of world `world` as the cuts left it.
    void rememberCut(std::size_t world, std::uint32_t sector, const rw::Atomic& atomic);
    // Cuts what cut `index` takes from the sectors under its box, once they are all loaded.
    void makeCut(std::size_t index, WorldSet& worlds, raycast::CollisionMesh* collision);
    // Builds roof `index`'s collision once the sectors under it are loaded; true when it did.
    bool makeRoof(std::size_t index, WorldSet& worlds);
    // The mesh's extra triangles again: the roofs', then each cut's moved by its offset now.
    void rebuildCollision(raycast::CollisionMesh& collision);

    debug::WorldEdits m_edits;
    std::function<void(std::string_view)> m_print;
    WorldSet* m_set = nullptr; // the worlds of the last afterStep(), whose spread sectors drawAfterWorld() draws
    std::vector<debug::SpreadPlacement> m_spread; // by world then sector, both worlds' sectors in one list
    std::vector<std::size_t> m_spreadFirst;       // the index in m_spread of each world's sector 0
    std::vector<std::vector<bool>> m_placed;      // by world then sector, whether the spread has ever placed it
    std::size_t m_spreadPlaced = 0;
    std::vector<Piece> m_pieces;
    std::vector<CutSector> m_cutSectors; // every sector a cut was made in
    std::size_t m_recut = 0;             // sectors cut again after their part was read again
    std::vector<bool> m_cutDone;         // per cut
    std::vector<std::vector<raycast::BuildTriangle>> m_cutCollision;  // per cut, as the disc has it
    std::vector<debug::Vec3> m_offsets;                               // per cut, the offset as of the last step
    std::vector<std::vector<raycast::BuildTriangle>> m_roofCollision; // per roof, empty until built
    std::vector<bool> m_roofDone;
    std::size_t m_cutTriangles = 0; // visible triangles cut, every piece together
    bool m_collisionDirty = false;
};

/// Reads the world edit file at `path` (debug::parseWorldEdits()) and makes its editor. Fails with ErrorCode::NotFound
/// when the file cannot be read, and as parsing does.
[[nodiscard]] std::expected<std::shared_ptr<WorldEditor>, Error>
loadWorldEditor(const std::string& path, std::function<void(std::string_view)> print);

} // namespace coney::platform
