// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "debug/anim_catalog.h"
#include "debug/debug_lines.h"
#include "debug/entity_inspect.h"
#include "debug/play_controls.h"
#include "platform/play_level_mode.h"

namespace coney::platform {

/// The debug menus' way into a play mode (debug::PlayControls, docs/guides/debug-menu.md): the Player, Camera,
/// Spawner and AI fighters pages through the mode's own capabilities, and the parts that are debug tooling alone,
/// kept out of the mode so that the release build leaves them out (docs/guides/building.md#debug-tools): the Entity
/// inspector (play_level_inspect.cpp), the Animation player (play_level_anim.cpp) and the Debug draw page's lines.
/// Coney's own: the original has nothing like it, and nothing here changes how the game plays.
///
/// It acts on one mode at a time, set with setMode(); a mode the game replaces (a new level) is set again by whoever
/// asks for the controls, so they never outlive the mode they act on in use.
class PlayLevelControls final : public debug::PlayControls {
  public:
    /// Controls of `mode` (none: setMode() before use).
    explicit PlayLevelControls(PlayLevelMode* mode = nullptr) : m_mode(mode) {}
    ~PlayLevelControls() override = default;
    PlayLevelControls(const PlayLevelControls&) = delete;
    PlayLevelControls& operator=(const PlayLevelControls&) = delete;
    PlayLevelControls(PlayLevelControls&&) = delete;
    PlayLevelControls& operator=(PlayLevelControls&&) = delete;

    /// Acts on `mode` from now on.
    void setMode(PlayLevelMode* mode) { m_mode = mode; }
    /// The mode acted on; null when none is set.
    [[nodiscard]] PlayLevelMode* mode() const { return m_mode; }
    /// Acts on `mode` from now on and returns these controls; null, acting on nothing, when `mode` is null: the debug
    /// services' `play` hook, asked each time a page acts.
    [[nodiscard]] debug::PlayControls* controlsOf(PlayLevelMode* mode) {
        m_mode = mode;
        return mode != nullptr ? this : nullptr;
    }

    /// The Debug draw page's lines in `mode`'s scene (the collision wireframe, the player's and the camera's markers,
    /// the places, the humans' shapes and the Entity inspector's highlight, as `options` switches them), from
    /// `snapshot` as drawn, the models at `alpha` of a step.
    [[nodiscard]] static std::vector<SceneLine> sceneLines(const PlayLevelMode& mode,
                                                           const debug::DebugDrawOptions& options,
                                                           const human::PlayerSnapshot& snapshot, float alpha);
    /// The hook a mode draws sceneLines() with (PlayLevelMode::setSceneLines()), for `options`, which must outlive
    /// the mode.
    [[nodiscard]] static SceneLines sceneLinesFor(const debug::DebugDrawOptions& options);
    /// The distance from entity `id`'s newest-step feet (where the debug shapes are built) to where `mode` draws its
    /// model `alpha` of the way from the step before to the newest one; zero for an object or an unknown id. The debug
    /// shapes and the inspector's highlight move by it so they stay on the models at any frame rate.
    [[nodiscard]] static anim::Vec3 drawnOffsetOf(const PlayLevelMode& mode, debug::EntityId id, float alpha);

    // debug::PlayControls.
    [[nodiscard]] std::string sceneName() const override { return m_mode->sceneName(); }
    [[nodiscard]] anim::Vec3 playerFeet() const override { return m_mode->playerFeet(); }
    [[nodiscard]] float playerHeadingDegrees() const override { return m_mode->playerHeadingDegrees(); }
    [[nodiscard]] float playerSpeed() const override { return m_mode->playerSpeed(); }
    [[nodiscard]] std::string playerState() const override { return m_mode->playerState(); }
    void teleport(const debug::Place& place) override { m_mode->teleport(place); }
    [[nodiscard]] std::vector<debug::Place> places() const override { return m_mode->places(); }
    [[nodiscard]] bool playerFrozen() const override { return m_mode->playerFrozen(); }
    void setPlayerFrozen(bool frozen) override { m_mode->setPlayerFrozen(frozen); }
    [[nodiscard]] bool canNoclip() const override { return true; }
    [[nodiscard]] bool noclip() const override { return m_mode->noclip(); }
    void setNoclip(bool on) override { m_mode->setNoclip(on); }
    [[nodiscard]] anim::Vec3 cameraEye() const override { return m_mode->cameraEye(); }
    [[nodiscard]] anim::Vec3 cameraTarget() const override { return m_mode->cameraTarget(); }
    void resetCamera() override { m_mode->resetCamera(); }
    [[nodiscard]] bool freeCamera() const override { return m_mode->freeCamera(); }
    void setFreeCamera(bool on) override { m_mode->setFreeCamera(on); }
    [[nodiscard]] bool canSpawn() const override { return m_mode->canSpawn(); }
    std::expected<void, Error> spawn(const sandbox::Primitive& primitive) override { return m_mode->spawn(primitive); }
    [[nodiscard]] std::size_t spawnedCount() const override { return m_mode->spawnedCount(); }
    std::expected<void, Error> clearSpawned() override { return m_mode->clearSpawned(); }
    [[nodiscard]] bool canSpawnFighter() const override { return true; }
    std::expected<void, Error> spawnFighter(anim::Vec3 feet, float headingDegrees) override {
        return m_mode->spawnFighter(feet, headingDegrees);
    }
    [[nodiscard]] std::vector<std::string> fighterModels() const override { return m_mode->fighterModels(); }
    std::expected<void, Error> spawnFighterAs(anim::Vec3 feet, float headingDegrees, std::string_view model) override {
        return m_mode->spawnFighterAs(feet, headingDegrees, model);
    }
    [[nodiscard]] std::size_t fighterCount() const override { return m_mode->fighterCount(); }
    void clearFighters() override { m_mode->clearFighters(); }
    [[nodiscard]] bool fightersEngage() const override { return m_mode->fightersEngage(); }
    void setFightersEngage(bool on) override { m_mode->setFightersEngage(on); }
    [[nodiscard]] std::string fightersState() const override { return m_mode->fightersState(); }
    [[nodiscard]] std::vector<debug::CharacterChoice> characterChoices() const override {
        return m_mode->characterChoices();
    }
    [[nodiscard]] int playerType() const override { return m_mode->playerType(); }
    [[nodiscard]] std::string characterState() const override { return m_mode->characterState(); }
    std::expected<void, Error> changeCharacter(int type) override { return m_mode->changeCharacter(type); }
    // The Animation player (play_level_anim.cpp): the player's set, the generic set, then every model a player can be
    // drawn as, each with its own clips (every character shares the player's skeleton).
    [[nodiscard]] std::vector<std::string> animSets() const override;
    std::expected<std::vector<debug::AnimClipChoice>, Error> animClips(std::size_t set) override;
    std::expected<void, Error> playAnim(std::size_t set, std::uint32_t id,
                                        const debug::AnimPlayOptions& options) override;
    void stopAnim() override;
    [[nodiscard]] debug::AnimPlayback animPlayback() const override;
    [[nodiscard]] hud::Hud* hud() override { return m_mode->hud(); }
    /// The Entity inspector's view of the mode: its humans and objects (play_level_inspect.cpp).
    [[nodiscard]] const debug::EntityInspect* inspect() const override { return &m_inspector; }
    [[nodiscard]] bool canFreezeEntity(debug::EntityId id) const override;
    [[nodiscard]] bool entityFrozen(debug::EntityId id) const override;
    void setEntityFrozen(debug::EntityId id, bool frozen) override;

  private:
    // The Entity inspector's view of the controls' mode: read-only (freezing is the controls').
    class Inspector final : public debug::EntityInspect {
      public:
        explicit Inspector(const PlayLevelControls& controls) : m_controls(controls) {}
        [[nodiscard]] std::vector<debug::EntityRef> entities() const override;
        [[nodiscard]] std::vector<debug::WatchGroup> describe(debug::EntityId id) const override;

      private:
        const PlayLevelControls& m_controls;
    };

    // The entities of `mode` (Inspector::entities()) and what is known of one (Inspector::describe()).
    [[nodiscard]] static std::vector<debug::EntityRef> entitiesOf(const PlayLevelMode& mode);
    [[nodiscard]] static std::vector<debug::WatchGroup> describeIn(const PlayLevelMode& mode, debug::EntityId id);
    // Adds the collision shapes, the classified wireframe and the inspector's highlight to `lines`
    // (play_level_inspect.cpp).
    static void addInspectorLines(const PlayLevelMode& mode, const debug::DebugDrawOptions& options,
                                  debug::DebugLines& lines, const human::PlayerSnapshot& snapshot, float alpha);
    // The Animation player's anim set `set` (animSets()), a model's loaded on first use and kept by the mode; fails for
    // a set that does not exist and as loading the model does.
    [[nodiscard]] std::expected<const characters::AnimSet*, Error> animSetAt(std::size_t set);

    PlayLevelMode* m_mode;
    Inspector m_inspector{*this};
};

} // namespace coney::platform
