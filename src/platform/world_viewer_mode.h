// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "camera/camera_lens.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "graphics/render_device.h"
#include "platform/render_engine.h"
#include "platform/world_renderer.h"
#include "platform/world_set.h"
#include "world/debug_camera.h"
#include "world/level_object.h"
#include "world/sector_budget.h"

namespace coney::platform {

/// What a world viewer has done so far: counts only.
struct WorldViewerStats {
    std::uint32_t preloaded = 0; ///< Parts read by the preload.
    std::uint32_t loads = 0;     ///< Parts read while running.
    std::uint32_t unloads = 0;   ///< Parts freed to make room.
    std::uint32_t failures = 0;  ///< Parts that could not be read.
    std::uint32_t noRoom = 0;    ///< Frames where a wanted part did not fit and nothing could go.
    std::uint32_t maxDrawn = 0;  ///< Most atomics drawn in one frame.
    std::uint64_t frames = 0;    ///< Frames run.
};

/// Coney's streamed-world viewer, behind `coney --view-world NAME` (docs/guides/building.md#the-world-viewer): a
/// level's streamed worlds loaded in the original's order, streamed one decision a frame around a free-flying camera
/// driven by pad 1, and drawn in the original's world order with the draw distance and fade-in. The free-flying camera
/// is Coney's debug camera, but it looks through the player camera's lens (camera::kPlayerCameraLens: 65°, near 0.1,
/// far clip 115, which caps the draw distance). For a level with a level file (`<name>.lev`) the level object is
/// loaded too, and its background (sky, clouds, skyline) and light glows are drawn round the worlds. The first steps
/// of level loading; there are no objects or script yet.
///
/// Every frame: move the camera (world::DebugCamera); make one streaming decision from the last frame's visibility
/// (world::updateStreaming); move the draw distance (world::adjustDrawDistance); run the visibility pass; draw. Time is
/// game time, so with `--frames` and `--input-script` a run is the same every time (test mode).
///
/// Coney's own tool; the parts it is made of follow docs/research/world.md and docs/research/level-loading.md.
class WorldViewerMode final : public GameMode {
  public:
    /// The mode's id, outside the original's range.
    static constexpr std::uint32_t kId = 0x104;
    /// The background and fog colour before a level script sets one: white, as the device starts
    /// (docs/research/world.md#fog). The level scripts' SetFogColor is not run yet.
    static constexpr graphics::Rgba kFogColour{255, 255, 255, 255};
    /// The world's ambient light with no script call: the LightManager's constant offset, 40/255 = 0.157
    /// (docs/research/world.md#lighting).
    static constexpr float kAmbient = 40.0F / 255.0F;

    /// Loads the worlds `name` stands for (worldNamesFor()) from `wad`, charging `budget` first with the pools a
    /// running game holds before them: the `Global Data Pool` for `warriors.glr` and the `World Level Pool` for
    /// `<name>.lev`, when those files exist; then the level file itself, when there is one (loadLevel()). `print`
    /// receives one line per streaming event. Everything given must outlive the mode. Fails as worldNamesFor(),
    /// WorldSet::load() and loadLevel() do.
    [[nodiscard]] static std::expected<std::unique_ptr<WorldViewerMode>, Error>
    create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
           std::function<void(std::string_view)> print);

    [[nodiscard]] std::uint32_t id() const override { return kId; }

    /// Preloads the parts around the start position (WorldManager_Preload with the draw distance as radius).
    void enter() override;
    /// One line of counts: frames, parts read and freed, the most atomics drawn, the budget's peak.
    [[nodiscard]] std::string summary() const;
    /// One frame, as the class comment says.
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;

    [[nodiscard]] const WorldViewerStats& stats() const { return m_stats; }
    [[nodiscard]] const WorldSet& worlds() const { return *m_set; }
    /// The level object, or null for a name without a level file.
    [[nodiscard]] const world::LevelObject* level() const { return m_level.get(); }
    [[nodiscard]] const world::DebugCamera& camera() const { return m_camera; }
    [[nodiscard]] float drawDistance() const { return m_drawDistance; }

  private:
    WorldViewerMode(RenderEngine& engine, std::unique_ptr<WorldSet> set, std::unique_ptr<world::LevelObject> level,
                    world::SectorBudget& budget, world::Vec3 start, std::function<void(std::string_view)> print);

    // The camera as the frame draws it: pose, view window for the window's shape, clip distances.
    [[nodiscard]] WorldView view() const;

    RenderEngine& m_engine;
    std::unique_ptr<WorldSet> m_set;
    std::unique_ptr<world::LevelObject> m_level; // null without a level file
    world::SectorBudget& m_budget;
    world::DebugCamera m_camera;
    WorldRenderer m_renderer;
    std::function<void(std::string_view)> m_print;
    // InitLevel sets the draw distance to the far clip before the preload.
    float m_drawDistance = camera::kPlayerCameraLens.farClip;
    WorldViewerStats m_stats;
};

/// **Coney's choice** for where the viewer starts: above the middle of part 1 of the first world, at the top of its
/// sectors' boxes, looking along +z (no level start position is on the pages yet).
[[nodiscard]] world::Vec3 viewerStartPosition(const world::StreamedWorld& world);

} // namespace coney::platform
