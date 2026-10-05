// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/world_viewer_mode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <utility>
#include <vector>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "platform/level_file.h"
#include "world/view_frustum.h"
#include "world/world_streamer.h"

namespace coney::platform {

namespace {

// Game time in milliseconds from GameTimer ticks.
std::uint64_t millisecondsOf(std::uint64_t ticks) { return ticks / (GameTimer::kTicksPerSecond / 1000); }

// A world's name for messages: the index into the set's worlds.
std::string worldName(const WorldSet& set, std::size_t world) { return set.worlds()[world]->name(); }

} // namespace

world::Vec3 viewerStartPosition(const world::StreamedWorld& world) {
    if (world.partCount() == 0 || world.part(1).sectors.empty()) {
        return world::Vec3{};
    }
    // The union of part 1's sector boxes.
    world::Box bounds = world.sectors()[world.part(1).sectors.front()].box;
    for (const std::uint32_t k : world.part(1).sectors) {
        const world::Box& box = world.sectors()[k].box;
        bounds.min = {std::min(bounds.min.x, box.min.x), std::min(bounds.min.y, box.min.y),
                      std::min(bounds.min.z, box.min.z)};
        bounds.max = {std::max(bounds.max.x, box.max.x), std::max(bounds.max.y, box.max.y),
                      std::max(bounds.max.z, box.max.z)};
    }
    return world::Vec3{(bounds.min.x + bounds.max.x) * 0.5F, bounds.max.y, (bounds.min.z + bounds.max.z) * 0.5F};
}

std::expected<LevelScenery, Error> loadLevelScenery(RenderEngine& engine, const io::Wad& wad, std::string_view name,
                                                    world::SectorBudget& budget,
                                                    const std::function<void(std::string_view)>& print) {
    auto names = worldNamesFor(wad, name);
    if (!names) {
        return std::unexpected(std::move(names.error()));
    }
    // What a running game holds in the Sector Pool before a level's worlds: the global data, then the level file's
    // pool, which LoadLevel makes before the worlds (docs/research/level-loading.md#worldmanager-loadlevel).
    if (auto glr = wad.lookup("warriors.glr"); glr) {
        (void)budget.reserve(world::globalDataPoolBytes((*glr)->size));
    }
    if (auto lev = wad.lookup(std::string(name) + ".lev"); lev) {
        (void)budget.reserve(world::worldLevelPoolBytes((*lev)->size));
    }
    auto set = WorldSet::load(wad, *names, budget, engine.drawsPixels());
    if (!set) {
        return std::unexpected(std::move(set.error()));
    }
    std::string text = std::format("{}: {} world{}", name, names->size(), names->size() == 1 ? "" : "s");
    for (const world::StreamedWorld* world : (*set)->worlds()) {
        text += std::format(", {} ({} parts, {} streamed sectors)", world->name(), world->partCount(),
                            world->sectors().size());
    }
    print(text + std::format("; sector budget {} of {} bytes in use\n", budget.used(), budget.capacity()));
    // The level file, read after the worlds as LoadLevel does.
    std::unique_ptr<world::LevelObject> level;
    if (wad.lookup(std::string(name) + ".lev")) {
        auto loaded = loadLevel(wad, name, engine.drawsPixels());
        if (!loaded) {
            return std::unexpected(std::move(loaded.error()));
        }
        level = std::move(*loaded);
        print(std::format("{}.lev: {} collision triangles, {} occluders, {} path records, sky, clouds, skyline and "
                          "glows\n",
                          name, level->collision->triangles().size(), level->occluders.size(),
                          level->pathHeader.paths));
    }
    return LevelScenery{.set = std::move(*set), .level = std::move(level)};
}

std::expected<std::unique_ptr<WorldViewerMode>, Error>
WorldViewerMode::create(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
                        std::function<void(std::string_view)> print) {
    auto scenery = loadLevelScenery(engine, wad, name, budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    const world::Vec3 start = viewerStartPosition(*scenery->set->worlds().front());
    return std::unique_ptr<WorldViewerMode>(new WorldViewerMode(
        engine, std::move(scenery->set), std::move(scenery->level), budget, start, std::move(print)));
}

WorldViewerMode::WorldViewerMode(RenderEngine& engine, std::unique_ptr<WorldSet> set,
                                 std::unique_ptr<world::LevelObject> level, world::SectorBudget& budget,
                                 world::Vec3 start, std::function<void(std::string_view)> print)
    : m_engine(engine), m_set(std::move(set)), m_level(std::move(level)), m_budget(budget), m_camera(start),
      m_renderer(kAmbient), m_print(std::move(print)) {}

WorldView WorldViewerMode::view() const {
    // The player camera's view window on the 4:3 picture; a window of another shape keeps its height and widens or
    // narrows (a Coney choice: the original's picture is always the television's).
    const camera::ViewWindow window = camera::viewWindow(camera::kPlayerCameraLens);
    const graphics::Extent size = m_engine.frameSize();
    const float aspect =
        size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 4.0F / 3.0F;
    return WorldView{.pose = m_camera.pose(),
                     .halfWidth = window.halfHeight * aspect,
                     .halfHeight = window.halfHeight,
                     .nearClip = camera::kPlayerCameraLens.nearClip,
                     .drawDistance = m_drawDistance};
}

void WorldViewerMode::enter() {
    // The preload before the first frame, at game time 0, with the camera's draw distance as its radius.
    const std::array<world::Vec3, 1> cameras{m_camera.position()};
    const world::PreloadResult preload =
        world::preloadWorlds(m_set->worlds(), cameras, m_drawDistance, m_budget, *m_set, 0);
    m_stats.preloaded = preload.loaded;
    m_stats.unloads += preload.unloaded;
    m_stats.failures += preload.failed;
    m_print(std::format("preload: {} parts read, {} freed, {} failed; {} atomics resident, budget {} bytes in use\n",
                        preload.loaded, preload.unloaded, preload.failed, m_set->residentAtomics(), m_budget.used()));
}

std::string WorldViewerMode::summary() const {
    return std::format("world viewer: {} frames, {} parts preloaded, {} read, {} freed, {} failed, {} frames short of "
                       "room, at most {} atomics drawn, budget peak {} of {} bytes\n",
                       m_stats.frames, m_stats.preloaded, m_stats.loads, m_stats.unloads, m_stats.failures,
                       m_stats.noRoom, m_stats.maxDrawn, m_budget.peak(), m_budget.capacity());
}

ModeResult WorldViewerMode::update(GameModeStack& stack, const FrameTime& frame) {
    const auto seconds = static_cast<float>(frame.seconds);
    const std::uint64_t nowMs = millisecondsOf(frame.gameTicks);

    // The camera moves first, as the simulation does before the world manager's update.
    m_camera.update(stack.pads().port(0), seconds);
    const std::array<world::Vec3, 1> cameras{m_camera.position()};

    // One streaming decision, from the visibility of the last frame.
    const world::StreamStep step =
        world::updateStreaming(m_set->worlds(), cameras, m_drawDistance, m_budget, *m_set, nowMs);
    switch (step.result) {
    case world::StreamResult::Loaded:
        ++m_stats.loads;
        m_print(std::format("frame {}: read {} part {} ({:.1f} away)\n", frame.index, worldName(*m_set, step.world),
                            step.part, step.distance));
        break;
    case world::StreamResult::Unloaded:
        ++m_stats.unloads;
        m_print(std::format("frame {}: freed {} part {}\n", frame.index, worldName(*m_set, step.world), step.part));
        break;
    case world::StreamResult::Failed:
        ++m_stats.failures;
        m_print(std::format("frame {}: {} part {} failed: {}\n", frame.index, worldName(*m_set, step.world), step.part,
                            step.error));
        break;
    case world::StreamResult::NoRoom:
        ++m_stats.noRoom;
        break;
    case world::StreamResult::Idle:
        break;
    }

    // The draw distance follows the nearest missing scenery; Coney's fixed step is always 30 frames a second.
    const float pending = world::nearestPendingDistance(m_set->worlds(), cameras);
    m_drawDistance = world::adjustDrawDistance(m_drawDistance,
                                               world::DrawDistanceInputs{.pending = pending,
                                                                         .farClip = camera::kPlayerCameraLens.farClip,
                                                                         .seconds = seconds,
                                                                         .frameRate = 30.0F,
                                                                         .viewports = 1,
                                                                         .lowRateMode = false});

    // The visibility pass of this frame's one viewport, then the drawing.
    const WorldView current = view();
    const world::ViewFrustum frustum(current.pose, current.halfWidth, current.halfHeight, current.nearClip,
                                     current.drawDistance);
    for (world::StreamedWorld* world : m_set->worlds()) {
        world->findVisibleSectors(frustum, true);
    }
    m_renderer.render(m_engine, *m_set, m_level.get(), current, kFogColour, pending, nowMs);
    m_stats.maxDrawn = std::max(m_stats.maxDrawn, m_renderer.drawnAtomics());
    ++m_stats.frames;
    return ModeResult::Stay;
}

} // namespace coney::platform
