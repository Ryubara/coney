// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/reference_renderer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
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
#include "gamemodes/load_entry_mode.h"
#include "platform/character_lights.h"
#include "platform/character_mesh.h"
#include "platform/character_viewer_mode.h"
#include "platform/level_file.h"
#include "platform/texture_dictionary.h"
#include "world/level_object.h"
#include "world_objects/object_list.h"

namespace coney::platform {

namespace {

// **Coney's choice** for the reference lights: as the character viewer's, from in front of the model, above and
// to its right, so the three-quarter camera sees the lit side.
constexpr float kAmbient = 0.45F;
constexpr float kDirectional = 0.75F;
constexpr anim::Vec3 kLightDirection{-0.45F, -0.6F, -0.65F};
// The near and far clip: a character is a couple of metres tall and the camera a few metres away; the largest
// objects are several times that.
constexpr float kNearClip = 0.05F;
constexpr float kFarClip = 1000.0F;

// librw's vector from ours.
rw::V3d toRw(anim::Vec3 v) { return rw::V3d{v.x, v.y, v.z}; }

// librw's matrix from ours: the same four columns.
rw::Matrix toRw(const anim::Mat34& m) {
    rw::Matrix matrix;
    matrix.setIdentity();
    matrix.right = toRw(m.x);
    matrix.up = toRw(m.y);
    matrix.at = toRw(m.z);
    matrix.pos = toRw(m.t);
    matrix.update();
    return matrix;
}

// Ours from a level model's frame: its right, up and at axes and its position are the matrix's columns.
anim::Mat34 toMat34(const world::FrameMatrix& frame) {
    return anim::Mat34{{frame.right.x, frame.right.y, frame.right.z},
                       {frame.up.x, frame.up.y, frame.up.z},
                       {frame.at.x, frame.at.y, frame.at.z},
                       {frame.position.x, frame.position.y, frame.position.z}};
}

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

    // Frames `points`, draws `atomic`, reads the frame back, reduces it and writes it to `path` as a `size` × `size`
    // PNG: the steps every reference image shares.
    std::expected<void, Error> shoot(std::span<const anim::Vec3> points, rw::Atomic* atomic,
                                     const CharacterLights& lights, bool cullBack, int size,
                                     const std::filesystem::path& path) {
        place(characters::frameReference(points));
        draw(atomic, lights, cullBack);
        auto pixels = read();
        if (!pixels) {
            return std::unexpected(std::move(pixels.error()));
        }
        const std::vector<std::uint8_t> image =
            characters::downsampleRgba(*pixels, size * kReferenceSupersample, kReferenceSupersample);
        return writePng(path, image, size);
    }

  private:
    // Only create() makes one.
    explicit OffscreenCamera(int size) : m_size(size) {}

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

    // Clears to transparent black and draws `atomic` lit by `lights`, as the character viewer does: back faces culled
    // when `cullBack` (a character's closed skin), both sides drawn otherwise (an object's open shapes, such as a
    // sign or a fence).
    void draw(rw::Atomic* atomic, const CharacterLights& lights, bool cullBack) {
        rw::RGBA clear = rw::makeRGBA(0, 0, 0, 0);
        m_camera->clear(&clear, rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
        m_camera->beginUpdate();
        lights.use();
        rw::SetRenderState(rw::ZTESTENABLE, 1);
        rw::SetRenderState(rw::ZWRITEENABLE, 1);
        rw::SetRenderState(rw::CULLMODE, cullBack ? rw::CULLBACK : rw::CULLNONE);
        rw::SetRenderState(rw::FOGENABLE, 0);
        rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
        rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
        atomic->render();
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

    int m_size;
    rw::Camera* m_camera = nullptr; // owned, with its frame and buffers
};

// The names in the text file at `path` (characters::parseNameList()).
std::expected<std::vector<std::string>, Error> readNameList(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("could not open the name list {}", path));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return characters::parseNameList(text.str());
}

// A dictionary resource's first texture, converted for drawing, with the dictionaries that own it (null when the
// first dictionary holds none). A character's and an object's dictionary holds the texture their untextured
// material is drawn with (characters.md#files, level-loading.md#the-object-list).
struct FirstTexture {
    std::vector<TextureDictionary> dictionaries;
    rw::Texture* texture = nullptr;
};

// Loads the dictionaries of `entry` and picks their first texture (FirstTexture).
std::expected<FirstTexture, Error> loadFirstTexture(const io::Wad& wad, const io::WadEntry& entry,
                                                    const chunk::ChunkHandlerTable& table) {
    auto dictionaries = loadTextureDictionaries(wad, entry, table);
    if (!dictionaries) {
        return std::unexpected(std::move(dictionaries.error()));
    }
    for (TextureDictionary& dictionary : *dictionaries) {
        if (auto converted = dictionary.convertForDrawing(); !converted) {
            return std::unexpected(std::move(converted.error()));
        }
    }
    FirstTexture result;
    if (!dictionaries->empty()) {
        const std::vector<rw::Texture*> textures = dictionaries->front().textures();
        result.texture = textures.empty() ? nullptr : textures.front();
    }
    result.dictionaries = std::move(*dictionaries);
    return result;
}

// Renders one character's image into `path`: loads its resources, poses it, frames it, draws, reduces and writes.
std::expected<void, Error> renderCharacter(const io::Wad& wad, const characters::CharacterRecord& record,
                                           const chunk::ChunkHandlerTable& table, OffscreenCamera& camera,
                                           const CharacterLights& lights, int size, const std::filesystem::path& path) {
    auto assets = characters::loadCharacterAssets(wad, record, table);
    if (!assets) {
        return std::unexpected(std::move(assets.error()));
    }
    auto texture = loadFirstTexture(wad, *assets->textures, table);
    if (!texture) {
        return std::unexpected(std::move(texture.error()));
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
    CharacterMesh mesh(model, texture->texture);
    mesh.update(positions, normals);
    return camera.shoot(positions, mesh.atomic(), lights, true, size, path);
}

// Why an object got no image: Coney cannot load its model yet (`noModel`), or something else went wrong.
struct ObjectFailure {
    bool noModel = false;
    Error error;
};

// Renders one object's image into `path`: loads its model as the level file's models are read and its texture,
// turns it into the reference pose's axes, frames it, draws, reduces and writes.
std::expected<void, ObjectFailure> renderObject(const io::Wad& wad, const world_objects::ObjectRecord& record,
                                                const chunk::ChunkHandlerTable& table, OffscreenCamera& camera,
                                                const CharacterLights& lights, int size,
                                                const std::filesystem::path& path) {
    const auto failed = [](bool noModel, Error error) {
        return std::unexpected(ObjectFailure{noModel, std::move(error)});
    };
    auto modelEntry = wad.lookup(characters::resourceFileName(record.modelHash));
    auto texturesEntry = wad.lookup(characters::resourceFileName(record.texturesHash));
    if (!modelEntry || !texturesEntry) {
        return failed(true, !modelEntry ? modelEntry.error() : texturesEntry.error());
    }

    // The model: one 0x47 chunk, read as the level file's models are, which pushes it as a level model (0x41). That
    // reader refuses a clump of another shape (several atomics): Coney cannot load such a model yet.
    auto load = loadWadEntry(wad, **modelEntry, table);
    if (!load) {
        return failed(
            true, Error{load.error().code, std::format("model {:#010x}: {}", record.modelHash, load.error().message)});
    }
    std::vector<chunk::ChunkData> models = load->stacks.takeChunks(world::kLevelModelResult);
    auto* model = models.empty() ? nullptr : dynamic_cast<LevelAtomicObject*>(models.front().object.get());
    if (model == nullptr) {
        return failed(true, Error{ErrorCode::Invalid, std::format("model {:#010x}: no model", record.modelHash)});
    }
    auto texture = loadFirstTexture(wad, **texturesEntry, table);
    if (!texture) {
        return failed(false, texture.error());
    }
    // The material names no texture: it gets the dictionary's first, as a level model does (linkLevelModel()).
    rw::Atomic* atomic = model->atomic();
    rw::Geometry* geometry = atomic->geometry;
    const bool textured = texture->texture != nullptr && geometry->matList.numMaterials > 0;
    if (textured) {
        geometry->matList.materials[0]->setTexture(texture->texture);
    }

    // Turned into the reference pose's axes; the points framed are the vertices where they are drawn.
    const anim::Mat34 transform = characters::objectReferenceTransform(toMat34(model->frame()));
    rw::Matrix matrix = toRw(transform);
    atomic->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
    const rw::MorphTarget& target = geometry->morphTargets[0];
    std::vector<anim::Vec3> points(static_cast<std::size_t>(geometry->numVertices));
    for (std::size_t i = 0; i < points.size(); ++i) {
        const rw::V3d& v = target.vertices[i];
        points[i] = anim::transformPoint(transform, {v.x, v.y, v.z});
    }
    auto shot = camera.shoot(points, atomic, lights, false, size, path);
    // The dictionary goes before the model: let go of its texture first.
    if (textured) {
        geometry->matList.materials[0]->setTexture(nullptr);
    }
    if (!shot) {
        return failed(false, shot.error());
    }
    return {};
}

// The records of `all` to render, in the list's order: those `only` asks for (every one when it is empty), each
// name hash once. A hash listed twice would write the same file twice: the first record wins and the repeats are
// reported.
template <typename Record>
std::vector<const Record*> selectRecords(std::span<const Record> all, const std::vector<std::string>& only,
                                         std::string_view list, const std::function<void(std::string_view)>& print) {
    std::vector<const Record*> records;
    std::set<std::uint32_t> seen;
    for (const Record& record : all) {
        const bool wanted = only.empty() || std::ranges::any_of(only, [&record](const std::string& request) {
                                return characters::referenceRequestHash(request) == record.nameHash;
                            });
        if (!wanted) {
            continue;
        }
        if (!seen.insert(record.nameHash).second) {
            print(std::format("{}/{:08x}: listed again (model {:08x}); the first record's image is kept\n", list,
                              record.nameHash, record.modelHash));
            continue;
        }
        records.push_back(&record);
    }
    return records;
}

// Makes the folder of one list below the output folder.
std::expected<std::filesystem::path, Error> makeFolder(const std::string& outDir, characters::ReferenceList list) {
    std::filesystem::path folder = std::filesystem::path(outDir) / characters::referenceFolder(list);
    std::error_code made;
    std::filesystem::create_directories(folder, made);
    if (made) {
        return fail(ErrorCode::Io, std::format("could not make {}: {}", folder.string(), made.message()));
    }
    return folder;
}

} // namespace

std::expected<ReferenceRenderReport, Error> renderReferences(RenderEngine& engine, const io::Wad& wad,
                                                             const ReferenceRenderSettings& settings,
                                                             const std::function<void(std::string_view)>& print) {
    if (!engine.drawsPixels()) {
        return fail(ErrorCode::PlatformFailure, "reference images need the OpenGL renderer: the headless one draws "
                                                "nothing");
    }

    // The names known for the records' hashes: the name list's, then the names `--only` gives.
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
    for (const std::string& request : settings.only) {
        if (!request.starts_with("0x") && !request.starts_with("0X")) {
            knownNames.emplace(characters::characterNameHash(request), request);
        }
    }
    const auto fileName = [&knownNames](std::uint32_t nameHash) {
        const auto known = knownNames.find(nameHash);
        return characters::referenceFileName(nameHash, known != knownNames.end() ? known->second : "");
    };

    // The records of each list selected, in the list's order.
    std::optional<characters::CharacterList> characterList;
    std::vector<const characters::CharacterRecord*> characterRecords;
    if (settings.characters) {
        auto list = characters::loadCharacterList(wad);
        if (!list) {
            return std::unexpected(std::move(list.error()));
        }
        characterList = std::move(*list);
        characterRecords = selectRecords(characterList->records(), settings.only, "characters", print);
    }
    std::optional<world_objects::ObjectList> objectList;
    std::vector<const world_objects::ObjectRecord*> objectRecords;
    if (settings.objects) {
        auto list = world_objects::loadObjectList(wad);
        if (!list) {
            return std::unexpected(std::move(list.error()));
        }
        objectList = std::move(*list);
        objectRecords = selectRecords(objectList->records(), settings.only, "objects", print);
    }
    if (characterRecords.empty() && objectRecords.empty()) {
        return fail(ErrorCode::NotFound, "no Character List or Object List record matches --only");
    }

    auto camera = OffscreenCamera::create(settings.size * kReferenceSupersample);
    if (!camera) {
        return std::unexpected(std::move(camera.error()));
    }
    const CharacterLights lights(kAmbient, kDirectional, kLightDirection);
    chunk::ChunkHandlerTable table = chunk::ChunkHandlerTable::withDefaults();
    characters::addCharacterDataHandlers(table);
    addTextureDictionaryHandlers(table);
    table.setHandlers(world::kPreinstanceObjectChunk, chunk::ChunkHandlers{{}, readPreinstanceObjectChunk});

    // One image per record; a failure is reported and counted, and the batch goes on.
    ReferenceRenderReport report;
    if (!characterRecords.empty()) {
        auto folder = makeFolder(settings.outDir, characters::ReferenceList::Characters);
        if (!folder) {
            return std::unexpected(std::move(folder.error()));
        }
        for (const characters::CharacterRecord* record : characterRecords) {
            const std::string file = fileName(record->nameHash);
            if (auto rendered = renderCharacter(wad, *record, table, **camera, lights, settings.size, *folder / file);
                !rendered) {
                ++report.failed;
                print(std::format("characters/{:08x}: failed: {}\n", record->nameHash, rendered.error().message));
                continue;
            }
            ++report.characters;
            print(std::format("characters/{:08x}: {} (model {:08x}, textures {:08x})\n", record->nameHash, file,
                              record->modelHash, record->texturesHash));
        }
    }
    if (!objectRecords.empty()) {
        auto folder = makeFolder(settings.outDir, characters::ReferenceList::Objects);
        if (!folder) {
            return std::unexpected(std::move(folder.error()));
        }
        for (const world_objects::ObjectRecord* record : objectRecords) {
            const std::string file = fileName(record->nameHash);
            if (auto rendered = renderObject(wad, *record, table, **camera, lights, settings.size, *folder / file);
                !rendered) {
                ++(rendered.error().noModel ? report.noModel : report.failed);
                print(std::format("objects/{:08x}: {}: {}\n", record->nameHash,
                                  rendered.error().noModel ? "no image" : "failed", rendered.error().error.message));
                continue;
            }
            ++report.objects;
            print(std::format("objects/{:08x}: {} (model {:08x}, textures {:08x})\n", record->nameHash, file,
                              record->modelHash, record->texturesHash));
        }
    }
    return report;
}

} // namespace coney::platform
