// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/reference_renderer.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

#include <rw.h>

#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_assets.h"
#include "characters/character_data.h"
#include "characters/character_list.h"
#include "characters/character_rig.h"
#include "characters/reference_render.h"
#include "core/chunk_system.h"
#include "platform/character_lights.h"
#include "platform/character_mesh.h"
#include "platform/character_viewer_mode.h"
#include "platform/texture_dictionary.h"

namespace coney::platform {

namespace {

// **Coney's choice** for the reference lights: as the character viewer's, from in front of the character, above and
// to its right, so the three-quarter camera sees the lit side.
constexpr float kAmbient = 0.45F;
constexpr float kDirectional = 0.75F;
constexpr anim::Vec3 kLightDirection{-0.45F, -0.6F, -0.65F};
// The near and far clip: a character is a couple of metres tall and the camera a few metres away.
constexpr float kNearClip = 0.05F;
constexpr float kFarClip = 100.0F;

// librw's vector from ours.
rw::V3d toRw(anim::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// A librw camera that draws into a texture rather than the window (an OpenGL frame buffer object), with its own
// depth buffer, square. The image can then be read back whatever the window's size, or with the window hidden.
class OffscreenCamera {
  public:
    // Makes the camera and its buffers, or fails when librw cannot make them.
    static std::expected<std::unique_ptr<OffscreenCamera>, Error> create(int size) {
        std::unique_ptr<OffscreenCamera> camera(new OffscreenCamera(size));
        camera->m_camera = rw::Camera::create();
        camera->m_camera->setFrame(rw::Frame::create());
        camera->m_camera->frameBuffer =
            rw::Raster::create(size, size, 32, static_cast<rw::int32>(rw::Raster::CAMERATEXTURE) | rw::Raster::C8888);
        camera->m_camera->zBuffer = rw::Raster::create(size, size, 0, rw::Raster::ZBUFFER);
        if (camera->m_camera->frameBuffer == nullptr || camera->m_camera->zBuffer == nullptr) {
            return fail(ErrorCode::PlatformFailure,
                        std::format("could not make a {0}x{0} offscreen frame buffer", size));
        }
        return camera;
    }

    ~OffscreenCamera() {
        // Buffers and frame first: Camera::destroy leaves them alone.
        if (m_camera->frameBuffer != nullptr) {
            m_camera->frameBuffer->destroy();
        }
        if (m_camera->zBuffer != nullptr) {
            m_camera->zBuffer->destroy();
        }
        rw::Frame* frame = m_camera->getFrame();
        m_camera->setFrame(nullptr);
        if (frame != nullptr) {
            frame->destroy();
        }
        m_camera->destroy();
    }
    OffscreenCamera(const OffscreenCamera&) = delete;
    OffscreenCamera& operator=(const OffscreenCamera&) = delete;
    OffscreenCamera(OffscreenCamera&&) = delete;
    OffscreenCamera& operator=(OffscreenCamera&&) = delete;

    // Places the camera at `view`. librw's GL3 renderer flips the camera frame's x axis, so the frame's `right` is
    // the screen's left, as in the character viewer.
    void place(const characters::ReferenceView& view) {
        rw::Matrix matrix;
        matrix.setIdentity();
        matrix.right = toRw(anim::scale(view.right, -1.0F));
        matrix.up = toRw(view.up);
        matrix.at = toRw(view.forward);
        matrix.pos = toRw(view.position);
        matrix.update();
        matrix.optimize();
        m_camera->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
        m_camera->setNearPlane(kNearClip);
        m_camera->setFarPlane(kFarClip);
        const rw::V2d window{characters::kReferenceHalfView, characters::kReferenceHalfView};
        m_camera->setViewWindow(&window);
    }

    // Clears to transparent black and draws `mesh` lit by `lights`, as the character viewer does.
    void draw(const CharacterMesh& mesh, const CharacterLights& lights) {
        rw::RGBA clear = rw::makeRGBA(0, 0, 0, 0);
        m_camera->clear(&clear, rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
        m_camera->beginUpdate();
        lights.use();
        rw::SetRenderState(rw::ZTESTENABLE, 1);
        rw::SetRenderState(rw::ZWRITEENABLE, 1);
        rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
        rw::SetRenderState(rw::FOGENABLE, 0);
        rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        mesh.atomic()->render();
        m_camera->endUpdate();
    }

    // Reads the drawn image back, rows top down. librw leaves the camera's frame buffer object bound after drawing.
    std::expected<std::vector<std::uint8_t>, Error> read() const {
        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        if (bound == 0) {
            return fail(ErrorCode::PlatformFailure, "the offscreen frame buffer is not bound after drawing");
        }
        const auto side = static_cast<std::size_t>(m_size);
        const std::size_t rowBytes = side * 4;
        std::vector<std::uint8_t> bottomUp(rowBytes * side);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, m_size, m_size, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
        if (glGetError() != GL_NO_ERROR) {
            return fail(ErrorCode::PlatformFailure, "glReadPixels could not read the offscreen frame");
        }
        // OpenGL's rows run bottom up; images run top down.
        std::vector<std::uint8_t> rgba(bottomUp.size());
        for (std::size_t y = 0; y < side; ++y) {
            std::memcpy(&rgba[y * rowBytes], &bottomUp[(side - 1 - y) * rowBytes], rowBytes);
        }
        return rgba;
    }

  private:
    // Only create() makes one.
    explicit OffscreenCamera(int size) : m_size(size) {}

    int m_size;
    rw::Camera* m_camera = nullptr; // owned, with its frame and buffers
};

// Writes `rgba` (`size` × `size`, rows top down) as a PNG at `path`. librw's writer (lodepng) adds no time stamp, so
// equal pixels give equal files. It reports a failure only through its own error state, so the old file is removed
// first and the new one checked for afterwards.
std::expected<void, Error> writePng(const std::filesystem::path& path, std::span<const std::uint8_t> rgba, int size) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    rw::Image* image = rw::Image::create(size, size, 32);
    image->allocate();
    std::memcpy(image->pixels, rgba.data(), rgba.size());
    rw::writePNG(image, path.string().c_str());
    image->destroy();
    std::error_code error;
    if (!std::filesystem::exists(path, error) || std::filesystem::file_size(path, error) == 0) {
        return fail(ErrorCode::Io, std::format("could not write {}", path.string()));
    }
    return {};
}

// The name hash `text` stands for: `0x` and hex digits as given, otherwise the hash of a model name.
std::uint32_t hashOfRequest(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X")) {
        std::uint32_t value = 0;
        const std::string_view digits = text.substr(2);
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value, 16);
        if (parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && !digits.empty()) {
            return value;
        }
    }
    return characters::characterNameHash(text);
}

// The names in the text file at `path`, one per line; blank lines and lines starting with '#' are skipped, and spaces
// around a name are trimmed.
std::expected<std::vector<std::string>, Error> readNameList(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("could not open the name list {}", path));
    }
    std::vector<std::string> names;
    std::string line;
    while (std::getline(file, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        const auto last = line.find_last_not_of(" \t\r");
        names.push_back(line.substr(first, last - first + 1));
    }
    return names;
}

// Renders one record's image into `path`: loads its resources, poses it, frames it, draws, reduces and writes.
std::expected<void, Error> renderOne(const io::Wad& wad, const characters::CharacterRecord& record,
                                     const chunk::ChunkHandlerTable& table, OffscreenCamera& camera,
                                     const CharacterLights& lights, int size, const std::filesystem::path& path) {
    auto assets = characters::loadCharacterAssets(wad, record, table);
    if (!assets) {
        return std::unexpected(std::move(assets.error()));
    }
    auto dictionaries = loadTextureDictionaries(wad, *assets->textures, table);
    if (!dictionaries) {
        return std::unexpected(std::move(dictionaries.error()));
    }
    for (TextureDictionary& dictionary : *dictionaries) {
        if (auto converted = dictionary.convertForDrawing(); !converted) {
            return std::unexpected(std::move(converted.error()));
        }
    }
    // Each character's dictionary holds one texture, which the untextured materials use (characters.md).
    rw::Texture* texture = nullptr;
    if (!dictionaries->empty()) {
        const std::vector<rw::Texture*> textures = dictionaries->front().textures();
        texture = textures.empty() ? nullptr : textures.front();
    }

    // The pose, framed by the fixed camera. The bind pose lies along the clump's up axis (x), and only a clip's root
    // rotation stands the character up, so the image shows the character viewer's first frame: its default clip
    // (CharacterViewerMode::kDefaultAnimId, else the first clip) at time 0. Without clips, the bind pose.
    const characters::CharacterModel& model = assets->model;
    std::vector<anim::Vec3> positions(model.vertices.size());
    std::vector<anim::Vec3> normals(model.vertices.size());
    const anim::AnimClip* clip = assets->data.animation(CharacterViewerMode::kDefaultAnimId);
    if (clip == nullptr && !assets->data.clips().empty()) {
        clip = assets->data.clips().front().get();
    }
    if (clip != nullptr) {
        const anim::Skeleton skeleton = characters::characterSkeleton(model);
        const anim::Pose pose = anim::samplePose(*clip, 0.0F, anim::referenceRotations());
        const auto bones = anim::boneTransforms(skeleton, pose);
        characters::skinVertices(model, characters::skinningMatrices(model, bones), positions, normals);
    } else {
        characters::bindPoseVertices(model, positions, normals);
    }
    CharacterMesh mesh(model, texture);
    mesh.update(positions, normals);
    camera.place(characters::frameReference(positions));
    camera.draw(mesh, lights);
    auto pixels = camera.read();
    if (!pixels) {
        return std::unexpected(std::move(pixels.error()));
    }
    const std::vector<std::uint8_t> image =
        characters::downsampleRgba(*pixels, size * kReferenceSupersample, kReferenceSupersample);
    return writePng(path, image, size);
}

} // namespace

std::expected<ReferenceRenderReport, Error>
renderCharacterReferences(RenderEngine& engine, const io::Wad& wad, const ReferenceRenderSettings& settings,
                          const std::function<void(std::string_view)>& print) {
    if (!engine.drawsPixels()) {
        return fail(ErrorCode::PlatformFailure, "reference images need the OpenGL renderer: the headless one draws "
                                                "nothing");
    }
    auto list = characters::loadCharacterList(wad);
    if (!list) {
        return std::unexpected(std::move(list.error()));
    }

    // The records to render, in the list's order, and the names known for their hashes.
    std::map<std::uint32_t, std::string> knownNames;
    if (!settings.namesFile.empty()) {
        auto names = readNameList(settings.namesFile);
        if (!names) {
            return std::unexpected(std::move(names.error()));
        }
        for (const std::string& name : *names) {
            knownNames.emplace(characters::characterNameHash(name), name);
        }
    }
    // A name hash listed twice would write the same file twice: the first record wins and the repeats are reported.
    std::vector<const characters::CharacterRecord*> records;
    std::set<std::uint32_t> seen;
    for (const characters::CharacterRecord& record : list->records()) {
        const bool wanted =
            settings.only.empty() || std::ranges::any_of(settings.only, [&record](const std::string& r) {
                return hashOfRequest(r) == record.nameHash;
            });
        if (!wanted) {
            continue;
        }
        if (!seen.insert(record.nameHash).second) {
            print(std::format("{:08x}: listed again (model {:08x}); the first record's image is kept\n",
                              record.nameHash, record.modelHash));
            continue;
        }
        records.push_back(&record);
    }
    for (const std::string& request : settings.only) {
        if (!request.starts_with("0x") && !request.starts_with("0X")) {
            knownNames.emplace(characters::characterNameHash(request), request);
        }
    }
    if (records.empty()) {
        return fail(ErrorCode::NotFound, "no Character List record matches --only");
    }

    std::error_code made;
    std::filesystem::create_directories(settings.outDir, made);
    if (made) {
        return fail(ErrorCode::Io, std::format("could not make {}: {}", settings.outDir, made.message()));
    }
    auto camera = OffscreenCamera::create(settings.size * kReferenceSupersample);
    if (!camera) {
        return std::unexpected(std::move(camera.error()));
    }
    const CharacterLights lights(kAmbient, kDirectional, kLightDirection);
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    characters::addCharacterDataHandlers(table);
    addTextureDictionaryHandlers(table);

    // One image per record; a failure is reported and counted, and the batch goes on.
    ReferenceRenderReport report;
    for (const characters::CharacterRecord* record : records) {
        const auto known = knownNames.find(record->nameHash);
        const std::string fileName =
            characters::referenceFileName(record->nameHash, known != knownNames.end() ? known->second : "");
        const std::filesystem::path path = std::filesystem::path(settings.outDir) / fileName;
        if (auto rendered = renderOne(wad, *record, table, **camera, lights, settings.size, path); !rendered) {
            ++report.failed;
            print(std::format("{:08x}: failed: {}\n", record->nameHash, rendered.error().message));
            continue;
        }
        ++report.rendered;
        print(std::format("{:08x}: {} (model {:08x}, textures {:08x})\n", record->nameHash, fileName, record->modelHash,
                          record->texturesHash));
    }
    return report;
}

} // namespace coney::platform
