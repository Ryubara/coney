// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/character_viewer_mode.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>
#include <utility>

#include <rw.h>

#include "characters/character_rig.h"
#include "core/chunk_system.h"
#include "gamemodes/game_mode_stack.h"

namespace coney::platform {

namespace {

// Where the camera looks: about a standing character's middle (Coney's choice). It starts in front of the character,
// which faces +y when its clips move it forwards (a walk's root velocity points along +y), a little above it.
constexpr anim::Vec3 kCameraTarget{0.0F, 0.0F, 0.95F};
constexpr float kCameraDistance = 3.2F;
constexpr float kCameraYaw = 1.5707963F;
constexpr float kCameraPitch = 0.15F;
// The view window's half height, the near and the far clip: Coney's choice.
constexpr float kHalfHeight = 0.4F;
constexpr float kNearClip = 0.1F;
constexpr float kFarClip = 100.0F;
// The directional light's direction of travel: down, and from in front of the character towards its back.
constexpr anim::Vec3 kLightDirection{0.35F, -0.55F, -0.75F};

// librw's vector from ours.
rw::V3d toRw(anim::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// Places librw's camera at `pose`. librw's GL3 renderer flips the camera frame's x axis, so the frame's `right` is
// the screen's left: -pose.right, which keeps the frame right-handed (as the world renderer does).
void placeCamera(rw::Camera* camera, const characters::OrbitPose& pose, float aspect) {
    rw::Matrix matrix;
    matrix.setIdentity();
    matrix.right = toRw(anim::scale(pose.right, -1.0F));
    matrix.up = toRw(pose.up);
    matrix.at = toRw(pose.forward);
    matrix.pos = toRw(pose.position);
    matrix.update();
    matrix.optimize();
    camera->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
    camera->setNearPlane(kNearClip);
    camera->setFarPlane(kFarClip);
    const rw::V2d window{kHalfHeight * aspect, kHalfHeight};
    camera->setViewWindow(&window);
}

// The clip `request` names in `data`: an anim id in decimal, else a clip's name; empty picks the default.
std::expected<const anim::AnimClip*, Error> pickClip(const characters::CharacterData& data, std::string_view request) {
    if (data.clips().empty()) {
        return fail(ErrorCode::NotFound, "the character has no animations");
    }
    if (request.empty()) {
        const anim::AnimClip* walk = data.animation(CharacterViewerMode::kDefaultAnimId);
        return walk != nullptr ? walk : data.clips().front().get();
    }
    std::size_t id = 0;
    const auto parsed = std::from_chars(request.data(), request.data() + request.size(), id);
    if (parsed.ec == std::errc{} && parsed.ptr == request.data() + request.size()) {
        const anim::AnimClip* byId = data.animation(id);
        if (byId == nullptr) {
            return fail(ErrorCode::NotFound, std::format("anim id {} has no clip of this character's own", id));
        }
        return byId;
    }
    const anim::AnimClip* byName = data.findClip(request);
    if (byName == nullptr) {
        return fail(ErrorCode::NotFound, std::format("the character has no clip called {}", request));
    }
    return byName;
}

} // namespace

std::expected<std::unique_ptr<CharacterViewerMode>, Error>
CharacterViewerMode::create(RenderEngine& engine, const io::Wad& wad, std::string_view name, std::string_view clip,
                            std::function<void(std::string_view)> print) {
    // The character's resources, through the Character List.
    auto list = characters::loadCharacterList(wad);
    if (!list) {
        return std::unexpected(std::move(list.error()));
    }
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    characters::addCharacterDataHandlers(table);
    addTextureDictionaryHandlers(table);
    auto assets = characters::loadCharacterAssets(wad, *list, table, name);
    if (!assets) {
        return std::unexpected(std::move(assets.error()));
    }
    auto dictionaries = loadTextureDictionaries(wad, *assets->textures, table);
    if (!dictionaries) {
        return fail(dictionaries.error().code, std::format("{}: textures: {}", name, dictionaries.error().message));
    }
    if (engine.drawsPixels()) {
        for (TextureDictionary& dictionary : *dictionaries) {
            if (auto converted = dictionary.convertForDrawing(); !converted) {
                return std::unexpected(std::move(converted.error()));
            }
        }
    }

    // The clip to start with, as an index into the clips.
    auto picked = pickClip(assets->data, clip);
    if (!picked) {
        return fail(picked.error().code, std::format("{}: {}", name, picked.error().message));
    }
    const auto clips = assets->data.clips();
    const auto found = std::ranges::find_if(clips, [&picked](const auto& c) { return c.get() == *picked; });
    const auto clipIndex = static_cast<std::size_t>(found - clips.begin());

    const characters::CharacterModel& model = assets->model;
    std::size_t textures = 0;
    for (const TextureDictionary& dictionary : *dictionaries) {
        textures += dictionary.info().textures.size();
    }
    print(std::format("{}: model {:#010x} ({} frames, {} bones, {} vertices, {} triangles), character data {:#010x} "
                      "({} clips), textures {:#010x} ({} textures)\n",
                      name, assets->record.modelHash, model.clump.frames.size(), model.clump.hierarchy.size(),
                      model.vertices.size(), model.triangles.size(), assets->record.dataHash, clips.size(),
                      assets->record.texturesHash, textures));
    return std::unique_ptr<CharacterViewerMode>(
        new CharacterViewerMode(engine, std::move(*assets), std::move(*dictionaries), clipIndex, std::move(print)));
}

CharacterViewerMode::CharacterViewerMode(RenderEngine& engine, characters::CharacterAssets assets,
                                         std::vector<TextureDictionary> dictionaries, std::size_t clipIndex,
                                         std::function<void(std::string_view)> print)
    : m_engine(engine), m_assets(std::move(assets)), m_dictionaries(std::move(dictionaries)),
      m_skeleton(characters::characterSkeleton(m_assets.model)), m_cursor(*m_assets.data.clips()[clipIndex]),
      m_camera(characters::OrbitCamera(kCameraTarget, kCameraDistance, kCameraYaw, kCameraPitch)),
      m_print(std::move(print)), m_positions(m_assets.model.vertices.size()),
      m_normals(m_assets.model.vertices.size()) {
    for (const auto& clip : m_assets.data.clips()) {
        m_clips.push_back(clip.get());
    }
    // The texture: each character's dictionary holds one, which every material uses (the materials name none).
    rw::Texture* texture = nullptr;
    if (!m_dictionaries.empty()) {
        const std::vector<rw::Texture*> textures = m_dictionaries.front().textures();
        texture = textures.empty() ? nullptr : textures.front();
    }
    m_mesh = std::make_unique<CharacterMesh>(m_assets.model, texture);

    // The stand-in lights: an ambient one and a directional one on a frame of its own.
    m_lights = std::make_unique<CharacterLights>(kAmbient, kDirectional, kLightDirection);

    startClip(clipIndex);
}

CharacterViewerMode::~CharacterViewerMode() {
    m_lights.reset();
    m_mesh.reset(); // before the dictionaries, whose texture it holds
}

void CharacterViewerMode::startClip(std::size_t index) {
    m_clipIndex = index;
    m_cursor = anim::AnimCursor(*m_clips[index]);
    m_clipTime.reset(m_cursor.time());
    const anim::AnimClip& clip = *m_clips[index];
    m_print(std::format("playing clip {} of {}: {} ({:.3f} s, {} rotation channels)\n", index + 1, m_clips.size(),
                        clip.name, clip.duration, clip.rotations.size()));
}

void CharacterViewerMode::skin(float time) {
    const anim::Pose pose = anim::samplePose(m_cursor.clip(), time, m_skeleton.bindRotations);
    const auto bones = anim::boneTransforms(m_skeleton, pose);
    const std::vector<anim::Mat34> matrices = characters::skinningMatrices(m_assets.model, bones);
    characters::skinVertices(m_assets.model, matrices, m_positions, m_normals);
    m_lastMismatch = characters::jointMismatch(m_assets.model, matrices);
}

void CharacterViewerMode::draw(const characters::OrbitCamera& orbit) {
    m_engine.beginWindowFrame(kBackground);
    rw::Camera* camera = m_engine.camera();
    if (camera == nullptr) {
        m_engine.present(); // NULL backend: nothing to draw
        return;
    }
    m_mesh->update(m_positions, m_normals);
    const graphics::Extent size = m_engine.frameSize();
    const float aspect = size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 1.0F;
    placeCamera(camera, orbit.pose(), aspect);
    camera->beginUpdate();
    m_lights->use();
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    rw::SetRenderState(rw::FOGENABLE, 0);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    m_mesh->atomic()->render();
    m_engine.present();
}

ModeResult CharacterViewerMode::update(GameModeStack& stack, const FrameTime& frame) {
    const auto seconds = static_cast<float>(frame.seconds);
    const Pad& pad = stack.pads().port(0);

    // The values render() blends move on a step.
    m_camera.commit();
    m_clipTime.commit();

    // The camera first, then the clip: circle plays the next, square the one before.
    m_camera.current().update(pad, seconds);
    if ((pad.pressed() & pad::kCircle) != 0) {
        startClip((m_clipIndex + 1) % m_clips.size());
    } else if ((pad.pressed() & pad::kSquare) != 0) {
        startClip((m_clipIndex + m_clips.size() - 1) % m_clips.size());
    } else if (const float overshoot = m_cursor.advance(seconds); overshoot > 0.0F) {
        // Past the end: loop, carrying the overshoot into the next pass so no time is lost.
        const float duration = m_cursor.clip().duration;
        m_cursor.restart(duration > 0.0F ? std::fmod(overshoot, duration) : 0.0F);
        ++m_loops;
    }
    m_clipTime.current() = m_cursor.time();
    ++m_frames;
    return ModeResult::Stay;
}

void CharacterViewerMode::render(const RenderTime& time) {
    // The playhead goes forwards between the last two steps, wrapping round the clip's end when it looped.
    skin(lerpLooping(m_clipTime.previous(), m_clipTime.current(), time.alpha, m_cursor.clip().duration));
    const characters::OrbitCamera& from = m_camera.previous();
    const characters::OrbitCamera& to = m_camera.current();
    const anim::Vec3 target{lerp(from.target().x, to.target().x, time.alpha),
                            lerp(from.target().y, to.target().y, time.alpha),
                            lerp(from.target().z, to.target().z, time.alpha)};
    draw(characters::OrbitCamera(target, lerp(from.distance(), to.distance(), time.alpha),
                                 lerpAngle(from.yaw(), to.yaw(), time.alpha),
                                 lerp(from.pitch(), to.pitch(), time.alpha)));
}

std::string CharacterViewerMode::summary() const {
    return std::format("character viewer: {} frames, clip {} at {:.3f} s, {} loops, joint mismatch {:.4f} m\n",
                       m_frames, m_cursor.clip().name, m_cursor.time(), m_loops, m_lastMismatch);
}

} // namespace coney::platform
