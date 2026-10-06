// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audio/sound_player.h"
#include "camera/cameras.h"
#include "core/error.h"
#include "core/interpolation.h"
#include "graphics/screen_fade.h"
#include "human/player.h"
#include "platform/character_mesh.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"
#include "platform/world_renderer.h"
#include "scenes/letterbox.h"
#include "scenes/scene_host.h"
#include "scenes/scene_player.h"

namespace coney::platform {

/// One line of a scene system's counts for the play mode's summary (`; scenes: ...`).
[[nodiscard]] std::string scenesSummary(const scenes::SceneStats& stats);

/// A character the stage draws a bound human as: its resources and texture dictionaries.
struct StageCharacter {
    std::unique_ptr<human::PlayerCharacter> character;
    std::vector<TextureDictionary> dictionaries;
};

/// The play mode's side of a playing scene (scenes::SceneHost): what the scene does to the screen and the camera, kept
/// for the play mode to draw, and the bound humans the play mode does not draw itself, drawn as **puppets** of their
/// characters in the poses the scene gives them. A human the play mode draws (the player) is not a puppet: the play
/// mode reads its frames (frameOf()) and gets it back at the end (takeReleases()).
///
/// With player 1's cameras (setCameras()) the scene camera is theirs: a scene's start pushes the camera shown and
/// makes the scene camera current, its keys set its view, and its end pops the camera back over the scene's blend
/// (camera::Cameras::beginScene()). The stage still keeps the scene camera's last two views for drawing, so a cut
/// between keys stays a cut.
///
/// Coney's glue; the behaviour it shows is docs/research/scenes.md's. **Coney's choices**: without the cameras (the
/// sandbox) the camera blend back is a cut; a puppet stays where its scene left it (Coney has no game human behind
/// it yet); captions, particles, rumble and the brains' freeze are logged and counted, not shown; the soundtrack plays
/// on the speech bus and stops when the cinematic ends, and other scene sounds play unplaced on the effects bus.
class SceneStage final : public scenes::SceneHost {
  public:
    /// Loads the character `model` names, for drawing.
    using CharacterLoader = std::function<std::expected<StageCharacter, Error>(std::string_view model)>;
    /// The model the bound human `human` in role `role` (named `roleName`) is drawn as; empty when the play mode draws
    /// it itself.
    using ModelOf = std::function<std::string(double human, std::size_t role, std::string_view roleName)>;

    /// A stage loading characters through `load`, drawing bound humans as `modelOf` says, printing to `print`.
    SceneStage(CharacterLoader load, ModelOf modelOf, std::function<void(std::string_view)> print);
    ~SceneStage() override;
    SceneStage(const SceneStage&) = delete;
    SceneStage& operator=(const SceneStage&) = delete;
    SceneStage(SceneStage&&) = delete;
    SceneStage& operator=(SceneStage&&) = delete;

    /// Starts a step at game time `nowMs`: the newest camera and puppet states become the previous ones.
    void beginStep(std::uint64_t nowMs);
    /// Plays the scene's sounds through `sounds` (null: counted only); it must outlive the stage's use of it.
    void setSounds(audio::SoundPlayer* sounds) { m_soundPlayer = sounds; }
    /// Hands the scene camera to player 1's `cameras` (null: the stage keeps it alone); they must outlive the stage's
    /// use of them.
    void setCameras(camera::Cameras* cameras) { m_cameras = cameras; }
    /// Tells the stage whether a cinematic is playing (the original's scene state): music ducks while one is, and the
    /// soundtrack stops when it ends.
    void setCinematic(bool playing);
    /// Sets the role names of the scene about to play (the human's model may follow its role), by role index.
    void setRoleNames(std::vector<std::string> names) { m_roleNames = std::move(names); }

    // ---- What the play mode reads ----

    /// Whether the scene camera is current.
    [[nodiscard]] bool cameraActive() const { return m_camera.has_value(); }
    /// The scene camera's view between the last two steps, `alpha` of the way, as the world renderer takes it, for a
    /// frame `size` pixels; nothing while no scene camera is current.
    [[nodiscard]] std::optional<WorldView> cameraView(float alpha, graphics::Extent size) const;
    /// Whether the scene holds `human` now (from its join until its release): the play mode does not move it.
    [[nodiscard]] bool holds(double human) const;
    /// The newest frame the scene gave `human`, blended `alpha` of the way from the one before; nothing when none.
    [[nodiscard]] std::optional<scenes::RoleFrame> frameOf(double human, float alpha) const;
    /// A human the scene has given back since the last call, and where it stands now.
    struct Release {
        double human = 0.0;
        anim::Vec3 feet{};
        float heading = 0.0F;
    };
    [[nodiscard]] std::vector<Release> takeReleases();

    /// Skins the puppets for the frame between the last two steps, `alpha` of the way, with `skin` (the play mode's
    /// skinning: character, pose, feet, heading into positions and normals).
    using Skin = std::function<void(const human::PlayerCharacter&, const anim::Pose&, anim::Vec3, float,
                                    std::vector<anim::Vec3>&, std::vector<anim::Vec3>&)>;
    void skinPuppets(float alpha, const Skin& skin);
    /// Draws the puppets (the caller has set the lights and render states).
    void drawPuppets() const;
    /// Draws the letterbox and the fade over the window, at game time `nowMs` (inside the frame).
    void drawOverlay(RenderEngine& engine, std::uint64_t nowMs) const;
    /// The fade level at `nowMs` (0 clear, 1 black) and the bars' height (a fraction of the screen's).
    [[nodiscard]] float fadeLevel() const { return m_fade.level(); }
    [[nodiscard]] float barHeight(std::uint64_t nowMs) const { return m_letterbox.barHeight(nowMs); }
    /// One line of counts for the summary.
    [[nodiscard]] std::string summary() const;

    // ---- scenes::SceneHost ----
    void humanJoin(double human, std::uint32_t scene, std::size_t role, const scenes::ScenePose& start,
                   int gait) override;
    [[nodiscard]] bool humanReady(double human) override;
    void humanEnterScene(double human, std::size_t role) override;
    void humanPose(double human, const scenes::RoleFrame& frame) override;
    void humanExitScene(double human) override;
    void humanRelease(double human, const std::optional<scenes::ScenePose>& endPose) override;
    void suspendBrains(bool suspended) override;
    void cameraBegin(const scenes::ScenePose& pose, const scenes::SceneLens& lens) override;
    void cameraPose(const scenes::ScenePose& pose, const scenes::SceneLens& lens) override;
    void cameraEnd(float blendSeconds) override;
    void screenEffect(scenes::ScreenEffect type, float seconds) override;
    void colouredFade(bool out, std::uint32_t rgb, float seconds) override;
    void caption(std::string_view scene, int command) override;
    void soundtrackPrepare(std::uint32_t hash) override;
    void soundtrackStart() override;
    void sound(std::uint32_t hash, std::optional<double> object) override;
    void particle(std::string_view name, const scenes::ScenePose& pose) override;
    void rumble(int strength) override;
    void log(std::string_view line) override { m_print(line); }

  private:
    // A bound human the stage draws.
    struct Puppet {
        StageCharacter loaded;
        std::unique_ptr<CharacterMesh> mesh;
        std::vector<anim::Vec3> positions;
        std::vector<anim::Vec3> normals;
    };
    // What the scene has said of one bound human.
    struct Bound {
        std::size_t role = 0;
        std::string model; // empty: the play mode draws it
        std::optional<Interpolated<scenes::RoleFrame>> frame;
        bool fresh = false; // a frame arrived this step
    };
    // The scene camera at the last two steps.
    struct CameraState {
        scenes::ScenePose pose;
        scenes::SceneLens lens;
    };

    CharacterLoader m_load;
    ModelOf m_modelOf;
    std::function<void(std::string_view)> m_print;
    std::vector<std::string> m_roleNames;
    std::map<double, Bound> m_bound;
    std::map<double, Puppet> m_puppets;
    std::vector<Release> m_releases;
    std::optional<Interpolated<CameraState>> m_camera;
    graphics::ScreenFade m_fade;
    scenes::Letterbox m_letterbox;
    std::uint64_t m_nowMs = 0;
    std::uint64_t m_captions = 0;
    std::uint64_t m_sounds = 0;
    audio::SoundPlayer* m_soundPlayer = nullptr;
    camera::Cameras* m_cameras = nullptr;
    std::optional<audio::SoundId> m_soundtrack; // prepared, waiting for its start event
    audio::VoiceHandle m_soundtrackVoice;
    std::optional<float> m_musicBeforeDuck; // the music bus's volume while ducked
    std::uint64_t m_particles = 0;
    std::uint64_t m_rumbles = 0;
    std::uint64_t m_soundtracks = 0;
};

} // namespace coney::platform
