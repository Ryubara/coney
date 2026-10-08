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

std::optional<WorldView> SceneStage::cameraView(float alpha, float aspect) const {
    if (!m_camera) {
        return std::nullopt;
    }
    const CameraState& a = m_camera->previous();
    const CameraState& b = m_camera->current();
    // The camera looks along its rotation's +y with +z up (Coney's reading, docs/research/scenes.md).
    scenes::ScenePose pose;
    pose.position = anim::lerp(a.pose.position, b.pose.position, alpha);
    pose.rotation = anim::slerp(a.pose.rotation, b.pose.rotation, alpha);
    return worldViewOf(sceneCameraView(pose, b.lens), aspect, b.lens.farClip);
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
    // The bars unless the display setting leaves them out.
    const float bar =
        scenes::letterboxSettings().drawn ? m_letterbox.barHeight(nowMs) * static_cast<float>(size.height) : 0.0F;
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
    if (m_onJoin) {
        m_onJoin(human, true);
    }
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

bool SceneStage::humanFree(double human) { return !m_isFree || m_isFree(human); }

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
        m_onObjectPose(object, pose.position, pose.rotation);
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
    // A human the scene never took in (one in a grab at the start) and not skipped stays where it is.
    const bool placed = endPose.has_value() || frame.has_value();
    if (endPose) {
        release.feet = endPose->position;
        release.heading = scenes::headingOf(endPose->rotation);
    } else if (frame) {
        release.feet = frame->current().feet;
        release.heading = frame->current().heading;
    }
    if (placed && m_onRelease) {
        m_onRelease(release);
    }
    // The join goal is popped: its destroy gives a pad human its brain back, placed or not.
    if (m_onJoin) {
        m_onJoin(human, false);
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
    // The engine stops the previous scene's soundtrack and buffers this one silent on a stereo stream pair, or keeps
    // it pending until a pair frees (docs/research/sound.md#scene-sound).
    ++m_soundtracks;
    audio::SoundEngine* engine = soundEngine();
    if (engine == nullptr) {
        m_print(std::format("scene sound: {:#010x} not prepared: no sound engine\n", hash));
        return;
    }
    const audio::SoundHandle sound = engine->preloadSceneSound(hash);
    m_print(std::format("scene sound: {:#010x} {}\n", hash,
                        engine->pendingSceneSound() == hash ? "pending (waiting for a stream pair)"
                        : !sound.valid()                    ? "not prepared (no such sound, or refused)"
                        : engine->isVirtual(sound)          ? "prepared virtually (silent)"
                                                            : "prepared"));
}

void SceneStage::soundtrackStart() {
    // Scene event 13: whatever soundtrack is prepared starts.
    if (audio::SoundEngine* engine = soundEngine(); engine != nullptr) {
        const audio::SceneSoundStart started = engine->startSceneSound();
        m_print(std::format("scene sound: {}\n", started == audio::SceneSoundStart::Started ? "started"
                                                 : started == audio::SceneSoundStart::Virtual
                                                     ? "started virtually (silent)"
                                                     : "nothing prepared to start"));
    }
}

bool SceneStage::soundtrackReady() {
    audio::SoundEngine* engine = soundEngine();
    return engine == nullptr || engine->sceneSoundReady();
}

void SceneStage::soundtrackStop() {
    if (audio::SoundEngine* engine = soundEngine(); engine != nullptr) {
        engine->stopSceneSound();
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
    // The engine ducks the music while a cinematic runs and keeps the soundtrack's pair from the music
    // (docs/research/sound.md#scene-sound). The soundtrack itself is not stopped here: only a skip or a give-up stops
    // it, else it plays on to its own end or the next scene's preload.
    if (audio::SoundEngine* engine = soundEngine(); engine != nullptr) {
        engine->setCinematic(playing);
    }
}

void SceneStage::particle(std::string_view /*name*/, const scenes::ScenePose& /*pose*/) { ++m_particles; }

void SceneStage::rumble(int /*strength*/) { ++m_rumbles; }

} // namespace coney::platform
