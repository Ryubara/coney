// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/scene_stage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

#include <rw.h>

#include "camera/camera_lens.h"
#include "camera/camera_view.h"
#include "core/interpolation.h"
#include "platform/play_scenery.h"

namespace coney::platform {

namespace {

// `v` turned by `q`.
anim::Vec3 rotate(anim::Quat q, anim::Vec3 v) { return anim::transformDirection(anim::matrixFromQuat(q), v); }

// The scene camera at `pose` through `lens` as player 1's cameras take it: the pose's frame is the camera's (+y the
// view direction, +z up), and it looks at the point one metre along it.
camera::CameraView sceneCameraView(const scenes::ScenePose& pose, const scenes::SceneLens& lens) {
    return camera::CameraView{.position = pose.position,
                              .orientation = pose.rotation,
                              .lookAt = anim::add(pose.position, rotate(pose.rotation, {0.0F, 1.0F, 0.0F})),
                              .fieldOfView = lens.fieldOfView,
                              .nearClip = lens.nearClip,
                              .farClip = lens.farClip};
}

// A role frame `alpha` of the way from `a` to `b`: the feet lerped, the heading the short way round, the poses blended.
scenes::RoleFrame blend(const scenes::RoleFrame& a, const scenes::RoleFrame& b, float alpha) {
    scenes::RoleFrame out = b;
    out.feet = anim::lerp(a.feet, b.feet, alpha);
    out.heading = lerpAngle(a.heading, b.heading, alpha);
    out.pose = anim::blendPoses(a.pose, b.pose, alpha);
    return out;
}

// The texture a character's dictionaries hold, which every material uses; null when none.
rw::Texture* textureOf(const std::vector<TextureDictionary>& dictionaries) {
    if (dictionaries.empty()) {
        return nullptr;
    }
    const std::vector<rw::Texture*> textures = dictionaries.front().textures();
    return textures.empty() ? nullptr : textures.front();
}

} // namespace

SceneStage::SceneStage(CharacterLoader load, ModelOf modelOf, std::function<void(std::string_view)> print)
    : m_load(std::move(load)), m_modelOf(std::move(modelOf)), m_print(std::move(print)) {}

SceneStage::~SceneStage() {
    // The meshes before the dictionaries whose texture they hold.
    for (auto& [human, puppet] : m_puppets) {
        puppet.mesh.reset();
    }
}

void SceneStage::beginStep(std::uint64_t nowMs) {
    m_nowMs = nowMs;
    m_fade.update(nowMs);
    if (m_camera) {
        m_camera->commit();
    }
    for (auto& [human, bound] : m_bound) {
        if (bound.frame) {
            bound.frame->commit();
        }
        bound.fresh = false;
    }
}

std::optional<WorldView> SceneStage::cameraView(float alpha, graphics::Extent size) const {
    if (!m_camera) {
        return std::nullopt;
    }
    const CameraState& a = m_camera->previous();
    const CameraState& b = m_camera->current();
    const anim::Vec3 position = anim::lerp(a.pose.position, b.pose.position, alpha);
    const anim::Quat rotation = anim::slerp(a.pose.rotation, b.pose.rotation, alpha);
    // The camera looks along its rotation's +y with +z up (Coney's reading, docs/research/scenes.md), in RenderWare's
    // axes for the renderer; right is forward × up, as the play camera's.
    const anim::Vec3 forward = anim::normalise(directionToRenderWare(rotate(rotation, {0.0F, 1.0F, 0.0F})));
    const anim::Vec3 up = anim::normalise(directionToRenderWare(rotate(rotation, {0.0F, 0.0F, 1.0F})));
    const anim::Vec3 right = anim::normalise(anim::cross(forward, up));
    const scenes::SceneLens& lens = b.lens;
    const camera::ViewWindow window = camera::viewWindow(
        camera::CameraLens{.fieldOfView = lens.fieldOfView, .nearClip = lens.nearClip, .farClip = lens.farClip});
    const float aspect =
        size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 4.0F / 3.0F;
    const world::Vec3 eye = toRenderWare(position);
    return WorldView{.pose = world::CameraPose{.position = eye,
                                               .forward = world::Vec3{forward.x, forward.y, forward.z},
                                               .up = world::Vec3{up.x, up.y, up.z},
                                               .right = world::Vec3{right.x, right.y, right.z}},
                     .halfWidth = window.halfHeight * aspect,
                     .halfHeight = window.halfHeight,
                     .nearClip = lens.nearClip,
                     .drawDistance = lens.farClip};
}

bool SceneStage::holds(double human) const { return m_bound.contains(human); }

std::optional<scenes::RoleFrame> SceneStage::frameOf(double human, float alpha) const {
    const auto found = m_bound.find(human);
    if (found == m_bound.end()) {
        return std::nullopt;
    }
    const std::optional<Interpolated<scenes::RoleFrame>>& frame = found->second.frame;
    if (!frame) {
        return std::nullopt;
    }
    return blend(frame->previous(), frame->current(), alpha);
}

void SceneStage::skinPuppets(float alpha, const Skin& skin) {
    for (auto& [human, puppet] : m_puppets) {
        const auto bound = m_bound.find(human);
        if (bound == m_bound.end() || puppet.mesh == nullptr) {
            continue;
        }
        const std::optional<Interpolated<scenes::RoleFrame>>& frames = bound->second.frame;
        if (!frames) {
            continue;
        }
        const scenes::RoleFrame frame = blend(frames->previous(), frames->current(), alpha);
        skin(*puppet.loaded.character, frame.pose, frame.feet, frame.heading, puppet.positions, puppet.normals);
        puppet.mesh->update(puppet.positions, puppet.normals);
    }
}

void SceneStage::drawPuppets() const {
    for (const auto& [human, puppet] : m_puppets) {
        const auto bound = m_bound.find(human);
        if (bound != m_bound.end() && bound->second.frame && puppet.mesh != nullptr) {
            puppet.mesh->atomic()->render();
        }
    }
}

void SceneStage::drawOverlay(RenderEngine& engine, std::uint64_t nowMs) const {
    // The bars over the window's whole width, then the fade over everything.
    const graphics::Extent size = engine.frameSize();
    const float bar = m_letterbox.barHeight(nowMs) * static_cast<float>(size.height);
    std::vector<graphics::LogicalQuad> quads;
    if (bar > 0.0F) {
        const graphics::Rgba black{0, 0, 0, 255};
        const auto width = static_cast<float>(size.width);
        quads.push_back(graphics::LogicalQuad{0.0F, 0.0F, width, bar, graphics::UvRect{}, black});
        quads.push_back(
            graphics::LogicalQuad{0.0F, static_cast<float>(size.height) - bar, width, bar, graphics::UvRect{}, black});
    }
    if (const float level = m_fade.level(); level > 0.0F) {
        const auto alpha = static_cast<std::uint8_t>(std::clamp(std::lround(level * 255.0F), 0L, 255L));
        quads.push_back(graphics::LogicalQuad{0.0F, 0.0F, static_cast<float>(size.width),
                                              static_cast<float>(size.height), graphics::UvRect{},
                                              graphics::Rgba{0, 0, 0, alpha}});
    }
    if (!quads.empty()) {
        engine.drawWindowRects(quads);
    }
}

std::string scenesSummary(const scenes::SceneStats& stats) {
    return std::format("; scenes: preloads {}, started {}, ended {} (skipped {}, aborted {}), events {}, Lua calls {}",
                       stats.preloads, stats.started, stats.ended, stats.skipped, stats.aborted, stats.events,
                       stats.callbacks);
}

std::string SceneStage::summary() const {
    return std::format("; scene stage: {} bound, {} puppets, camera {}, captions {}, soundtracks {}, sounds {}, "
                       "particles {}, rumbles {}",
                       m_bound.size(), m_puppets.size(), m_camera ? "on" : "off", m_captions, m_soundtracks, m_sounds,
                       m_particles, m_rumbles);
}

void SceneStage::humanJoin(double human, std::uint32_t /*scene*/, std::size_t role, const scenes::ScenePose& /*start*/,
                           int /*gait*/) {
    // The model it is drawn as: its own, or none when the play mode draws it.
    const std::string_view roleName = role < m_roleNames.size() ? std::string_view(m_roleNames[role]) : "";
    m_bound[human] = Bound{.role = role, .model = m_modelOf(human, role, roleName), .frame = {}, .fresh = false};
}

bool SceneStage::humanReady(double human) {
    const auto bound = m_bound.find(human);
    if (bound == m_bound.end() || bound->second.model.empty() || m_puppets.contains(human)) {
        return true;
    }
    // The puppet's character, loaded now (Coney's reads are synchronous); a model that fails is drawn as nothing.
    auto loaded = m_load(bound->second.model);
    if (!loaded) {
        m_print(std::format("scene: human {} as {} could not be loaded ({}); not drawn\n", human, bound->second.model,
                            loaded.error().message));
        bound->second.model.clear();
        return true;
    }
    Puppet puppet;
    const characters::CharacterModel& model = loaded->character->assets().model;
    puppet.mesh = std::make_unique<CharacterMesh>(model, textureOf(loaded->dictionaries));
    puppet.positions.resize(model.vertices.size());
    puppet.normals.resize(model.vertices.size());
    puppet.loaded = std::move(*loaded);
    m_puppets.emplace(human, std::move(puppet));
    return true;
}

void SceneStage::humanEnterScene(double /*human*/, std::size_t /*role*/) {}

void SceneStage::humanPose(double human, const scenes::RoleFrame& frame) {
    Bound& bound = m_bound[human];
    if (!bound.frame) {
        bound.frame.emplace(frame);
    } else {
        bound.frame->current() = frame;
    }
    bound.fresh = true;
}

void SceneStage::humanExitScene(double /*human*/) {}

void SceneStage::objectPose(double object, const scenes::ScenePose& pose) {
    if (m_onObjectPose) {
        m_onObjectPose(object, pose.position);
    }
}

void SceneStage::humanRelease(double human, const std::optional<scenes::ScenePose>& endPose) {
    const auto found = m_bound.find(human);
    if (found == m_bound.end()) {
        return;
    }
    // Where it stands: its end pose after a skip, else where its clip left it.
    Release release{.human = human};
    std::optional<Interpolated<scenes::RoleFrame>>& frame = found->second.frame;
    if (endPose) {
        release.feet = endPose->position;
        release.heading = scenes::headingOf(endPose->rotation);
    } else if (frame) {
        release.feet = frame->current().feet;
        release.heading = frame->current().heading;
    }
    if (m_onRelease) {
        m_onRelease(release);
    }
    if (found->second.model.empty()) {
        m_bound.erase(found); // the play mode's own human goes back to it
        return;
    }
    // A puppet stays where it was put, still.
    if (frame) {
        scenes::RoleFrame still = frame->current();
        still.feet = release.feet;
        still.heading = release.heading;
        frame->reset(still);
    }
}

void SceneStage::suspendBrains(bool suspended) {
    m_print(std::format("scene: brains {}\n", suspended ? "suspended" : "resumed"));
}

void SceneStage::cameraBegin(const scenes::ScenePose& pose, const scenes::SceneLens& lens) {
    m_camera.emplace(CameraState{.pose = pose, .lens = lens});
    if (m_cameras != nullptr) {
        m_cameras->beginScene(sceneCameraView(pose, lens));
    }
}

void SceneStage::cameraPose(const scenes::ScenePose& pose, const scenes::SceneLens& lens) {
    if (m_cameras != nullptr) {
        m_cameras->setSceneView(sceneCameraView(pose, lens));
    }
    if (!m_camera) {
        m_camera.emplace(CameraState{.pose = pose, .lens = lens});
        return;
    }
    // A cut between two keys is a jump: blending across it would sweep the camera through the set.
    const CameraState& last = m_camera->current();
    m_camera->current() = CameraState{.pose = pose, .lens = lens};
    if (anim::distance(last.pose.position, pose.position) > 1.0F) {
        m_camera->reset(m_camera->current());
    }
}

void SceneStage::cameraEnd(float blendSeconds) {
    // The cameras blend back to the camera the scene pushed; the play mode draws through them again.
    m_camera.reset();
    if (m_cameras != nullptr) {
        m_cameras->endScene(blendSeconds);
    }
}

void SceneStage::screenEffect(scenes::ScreenEffect type, float seconds) {
    switch (type) {
    case scenes::ScreenEffect::FadeIn:
    case scenes::ScreenEffect::FadeOut:
        m_fade.queue(static_cast<int>(type), seconds, m_nowMs);
        return;
    case scenes::ScreenEffect::LetterboxIn:
    case scenes::ScreenEffect::LetterboxOut:
        m_letterbox.start(type == scenes::ScreenEffect::LetterboxIn, seconds, m_nowMs);
        return;
    case scenes::ScreenEffect::EndBlurPulse:
        return;
    }
}

void SceneStage::colouredFade(bool out, std::uint32_t /*rgb*/, float seconds) {
    // **Coney's choice**: drawn black (the fade is the screen fade's).
    m_fade.queue(out ? graphics::ScreenFade::kFadeOut : graphics::ScreenFade::kFadeIn, seconds, m_nowMs);
}

void SceneStage::caption(std::string_view /*scene*/, int command) { m_captions += command == 0 ? 1 : 0; }

// The game's sound engine, or null when there is none (no audio, no disc).
audio::SoundEngine* SceneStage::soundEngine() const {
    return m_soundPlayer != nullptr ? m_soundPlayer->engine() : nullptr;
}

void SceneStage::soundtrackPrepare(std::uint32_t hash) {
    // The engine stops the previous scene's soundtrack and buffers this one silent on a stereo stream pair
    // (docs/research/sound.md#scene-sound).
    ++m_soundtracks;
    if (audio::SoundEngine* engine = soundEngine(); engine != nullptr) {
        engine->preloadSceneSound(hash);
    }
}

void SceneStage::soundtrackStart() {
    // Scene event 13: the prepared soundtrack starts, and the music ducks while it plays.
    if (audio::SoundEngine* engine = soundEngine(); engine != nullptr) {
        engine->startSceneSound();
    }
}

void SceneStage::sound(std::uint32_t hash, std::optional<double> object) {
    // At the human the scene holds, when it holds him; otherwise Coney's choice: unplaced, on the effects bus.
    ++m_sounds;
    if (m_soundPlayer == nullptr) {
        return;
    }
    if (object) {
        if (const std::optional<scenes::RoleFrame> frame = frameOf(*object, 1.0F); frame) {
            m_soundPlayer->play3D(hash, audio::SoundVec{frame->feet.x, frame->feet.y, frame->feet.z});
            return;
        }
    }
    m_soundPlayer->play(hash, audio::VoiceParams{.bus = audio::Bus::Sfx});
}

void SceneStage::setCinematic(bool playing) {
    // The music's duck is the engine's while its scene soundtrack plays (docs/research/sound.md#music). Coney's choice:
    // the soundtrack ends with the cinematic, so a skip silences it.
    if (audio::SoundEngine* engine = soundEngine(); engine != nullptr && !playing) {
        engine->stopSceneSound();
    }
}

void SceneStage::particle(std::string_view /*name*/, const scenes::ScenePose& /*pose*/) { ++m_particles; }

void SceneStage::rumble(int /*strength*/) { ++m_rumbles; }

} // namespace coney::platform
