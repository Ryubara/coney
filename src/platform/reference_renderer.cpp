// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/reference_renderer.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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
#include "graphics/particle_page.h"
#include "graphics/reference_sprites.h"
#include "platform/character_lights.h"
#include "platform/character_mesh.h"
#include "platform/character_viewer_mode.h"
#include "platform/level_file.h"
#include "platform/object_models.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "world/level_object.h"
#include "world_objects/car_types.h"
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

// Writes `rgba` (`width` × `height`, rows top down) as a PNG at `path`. librw's writer (lodepng) adds no time stamp,
// so equal pixels give equal files. It reports a failure only through its own error state, so the old file is
// removed first and the new one checked for afterwards.
std::expected<void, Error> writePng(const std::filesystem::path& path, std::span<const std::uint8_t> rgba, int width,
                                    int height) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    rw::Image* image = rw::Image::create(width, height, 32);
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

    // Frames `points`, draws `atomics`, reads the frame back, reduces it and writes it to `path` as a `size` × `size`
    // PNG: the steps every model image shares.
    std::expected<void, Error> shoot(std::span<const anim::Vec3> points, std::span<rw::Atomic* const> atomics,
                                     const CharacterLights& lights, bool cullBack, int size,
                                     const std::filesystem::path& path) {
        place(characters::frameReference(points));
        draw(atomics, lights, cullBack);
        auto pixels = read();
        if (!pixels) {
            return std::unexpected(std::move(pixels.error()));
        }
        const std::vector<std::uint8_t> image =
            characters::downsampleRgba(*pixels, size * kReferenceSupersample, kReferenceSupersample);
        return writePng(path, image, size, size);
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

    // Clears to transparent black and draws `atomics` lit by `lights`, as the character viewer does: back faces
    // culled when `cullBack` (a character's closed skin), both sides drawn otherwise (an object's open shapes, such
    // as a sign or a fence).
    void draw(std::span<rw::Atomic* const> atomics, const CharacterLights& lights, bool cullBack) {
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
        for (rw::Atomic* atomic : atomics) {
            atomic->render();
        }
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
    rw::Atomic* const atomic = mesh.atomic();
    return camera.shoot(positions, {&atomic, 1}, lights, true, size, path);
}

// One atomic of a model placed for its image: the transform into the reference pose's axes.
struct PlacedAtomic {
    rw::Atomic* atomic = nullptr;
    anim::Mat34 transform;
};

// Draws `atomics` with `texture` on their first material (an untextured material gets the dictionary's first
// texture, as a level model does, linkLevelModel()), framed on their vertices where they are drawn, and writes the
// image to `path`. The materials let go of the texture afterwards.
std::expected<void, Error> shootModel(std::span<const PlacedAtomic> atomics, rw::Texture* texture,
                                      OffscreenCamera& camera, const CharacterLights& lights, int size,
                                      const std::filesystem::path& path) {
    std::vector<anim::Vec3> points;
    std::vector<rw::Atomic*> drawn;
    for (const PlacedAtomic& placed : atomics) {
        rw::Geometry* geometry = placed.atomic->geometry;
        if (texture != nullptr && geometry->matList.numMaterials > 0) {
            geometry->matList.materials[0]->setTexture(texture);
        }
        rw::Matrix matrix = toRw(placed.transform);
        placed.atomic->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
        const rw::MorphTarget& target = geometry->morphTargets[0];
        for (rw::int32 i = 0; i < geometry->numVertices; ++i) {
            const rw::V3d& v = target.vertices[i];
            points.push_back(anim::transformPoint(placed.transform, {v.x, v.y, v.z}));
        }
        drawn.push_back(placed.atomic);
    }
    auto shot = camera.shoot(points, drawn, lights, false, size, path);
    for (rw::Atomic* atomic : drawn) {
        if (texture != nullptr && atomic->geometry->matList.numMaterials > 0) {
            atomic->geometry->matList.materials[0]->setTexture(nullptr);
        }
    }
    return shot;
}

// Renders one object's image into `path`: its model, turned into the reference pose's axes, framed, drawn, reduced
// and written. A model of several atomics (a car's) is not an object's: it is refused as one Coney cannot load.
std::expected<void, ObjectFailure> renderObject(const io::Wad& wad, const world_objects::ObjectRecord& record,
                                                const chunk::ChunkHandlerTable& table, OffscreenCamera& camera,
                                                const CharacterLights& lights, int size,
                                                const std::filesystem::path& path) {
    auto loaded = loadObjectModel(wad, record, table);
    if (!loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    auto* model = dynamic_cast<LevelAtomicObject*>(loaded->model.get());
    if (model == nullptr) {
        return std::unexpected(ObjectFailure{
            true, Error{ErrorCode::Invalid,
                        std::format("model {:#010x}: several atomics (a car's: --kind cars)", record.modelHash)}});
    }
    const PlacedAtomic placed{model->atomic(), characters::objectReferenceTransform(toMat34(model->frame()))};
    if (auto shot = shootModel({&placed, 1}, loaded->texture.texture, camera, lights, size, path); !shot) {
        return std::unexpected(ObjectFailure{false, std::move(shot.error())});
    }
    return {};
}

// Renders one car type's image into `path`: its Object List record's model, the atomics of the undamaged car
// (world_objects::kCarParts) each at its own frame, framed, drawn, reduced and written. The car models stand in the
// reference pose's axes already (z up, the front towards +y; docs/research/cars.md#model), so nothing turns them.
std::expected<void, ObjectFailure> renderCar(const io::Wad& wad, const world_objects::ObjectRecord& record,
                                             const chunk::ChunkHandlerTable& table, OffscreenCamera& camera,
                                             const CharacterLights& lights, int size,
                                             const std::filesystem::path& path) {
    auto loaded = loadObjectModel(wad, record, table);
    if (!loaded) {
        return std::unexpected(std::move(loaded.error()));
    }
    const auto* model = dynamic_cast<const LevelClumpObject*>(loaded->model.get());
    if (model == nullptr || model->parts().size() < world_objects::kCarParts) {
        return std::unexpected(ObjectFailure{
            false, Error{ErrorCode::Invalid, std::format("model {:#010x}: not a clump of at least {} atomics",
                                                         record.modelHash, world_objects::kCarParts)}});
    }
    std::vector<PlacedAtomic> placed;
    for (const LevelClumpObject::Part& part : model->parts().first(world_objects::kCarParts)) {
        placed.push_back(PlacedAtomic{part.atomic.atomic(), toMat34(part.frame)});
    }
    if (auto shot = shootModel(placed, loaded->texture.texture, camera, lights, size, path); !shot) {
        return std::unexpected(ObjectFailure{false, std::move(shot.error())});
    }
    return {};
}

// A sprite sheet's texture as pixels and its rectangles, for cutting sprites out.
struct SheetPixels {
    RgbaImage image;
    graphics::ParticlePage page;
};

// Loads the sprite sheet whose name hashes to `sheetHash` (its WAD file is named by the hash in decimal) as pixels,
// once: later calls get the kept copy.
std::expected<const SheetPixels*, Error> sheetPixels(const io::Wad& wad, std::uint32_t sheetHash,
                                                     const chunk::ChunkHandlerTable& table,
                                                     std::map<std::uint32_t, SheetPixels>& sheets) {
    if (const auto kept = sheets.find(sheetHash); kept != sheets.end()) {
        return &kept->second;
    }
    auto entry = wad.lookup(characters::resourceFileName(sheetHash));
    if (!entry) {
        return std::unexpected(
            Error{entry.error().code, std::format("sheet {:#010x}: {}", sheetHash, entry.error().message)});
    }
    auto loaded = loadSpriteSheets(wad, **entry, table);
    if (!loaded) {
        return std::unexpected(
            Error{loaded.error().code, std::format("sheet {:#010x}: {}", sheetHash, loaded.error().message)});
    }
    auto image = loaded->front()->texture()->toImage();
    if (!image) {
        return std::unexpected(std::move(image.error()));
    }
    return &sheets.emplace(sheetHash, SheetPixels{std::move(*image), loaded->front()->page()}).first->second;
}

// Writes rectangle `rect` of `sheet` to `path`: cut out whole (graphics::rectTexels()), then scaled to fit
// graphics::kReferenceIconLimit, up as well as down when `enlarge`. Returns the image's size.
std::expected<graphics::ImageSize, Error> writeSprite(const SheetPixels& sheet, std::uint32_t rect, bool enlarge,
                                                      const std::filesystem::path& path) {
    if (rect >= sheet.page.rects.size()) {
        return fail(ErrorCode::NotFound, std::format("rectangle {} of a sheet of {}", rect, sheet.page.rects.size()));
    }
    const RgbaImage& image = sheet.image;
    const graphics::TexelBox box = graphics::rectTexels(sheet.page.rect(rect), image.width, image.height);
    const std::vector<std::uint8_t> cut = graphics::cropRgba(image.pixels, image.width, image.height, box);
    const graphics::ImageSize fitted =
        graphics::fitWithin(box.width, box.height, graphics::kReferenceIconLimit, enlarge);
    const std::vector<std::uint8_t> scaled =
        graphics::resampleRgba(cut, box.width, box.height, fitted.width, fitted.height);
    if (auto written = writePng(path, scaled, fitted.width, fitted.height); !written) {
        return std::unexpected(std::move(written.error()));
    }
    return fitted;
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

// Whether `only` asks for the entry called `name` (every entry when it is empty): a request matches by its name hash
// (characters::referenceRequestHash()), so `0x` and the hash work too.
bool wanted(const std::vector<std::string>& only, std::string_view name) {
    const std::uint32_t hash = characters::characterNameHash(name);
    return only.empty() || std::ranges::any_of(only, [hash](const std::string& request) {
               return characters::referenceRequestHash(request) == hash;
           });
}

// A sprite one image shows: its file, its sheet's name hash and its rectangle.
struct SpriteImage {
    std::string file;
    std::uint32_t sheetHash = 0;
    std::uint32_t rect = 0;
};

// Writes the images of `sprites` into the folder of `list`, each loaded from its sheet (kept in `sheets`), counting
// what is written in `written` and what fails in `report.failed`.
std::expected<void, Error> writeSprites(const io::Wad& wad, const std::string& outDir, characters::ReferenceList list,
                                        std::span<const SpriteImage> sprites, bool enlarge,
                                        const chunk::ChunkHandlerTable& table,
                                        std::map<std::uint32_t, SheetPixels>& sheets, std::size_t& written,
                                        ReferenceRenderReport& report,
                                        const std::function<void(std::string_view)>& print) {
    auto folder = makeFolder(outDir, list);
    if (!folder) {
        return std::unexpected(std::move(folder.error()));
    }
    const std::string_view name = characters::referenceFolder(list);
    for (const SpriteImage& sprite : sprites) {
        auto sheet = sheetPixels(wad, sprite.sheetHash, table, sheets);
        auto size = sheet ? writeSprite(**sheet, sprite.rect, enlarge, *folder / sprite.file)
                          : std::expected<graphics::ImageSize, Error>(std::unexpected(std::move(sheet.error())));
        if (!size) {
            ++report.failed;
            print(std::format("{}/{}: failed: {}\n", name, sprite.file, size.error().message));
            continue;
        }
        ++written;
        print(std::format("{}/{}: {}x{} (sheet {:08x}, rectangle {})\n", name, sprite.file, size->width, size->height,
                          sprite.sheetHash, sprite.rect));
    }
    return {};
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

    // The records of each list selected, in the list's order. The cars are found in the Object List too.
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
    std::vector<std::string_view> cars;
    if (settings.objects || settings.cars) {
        auto list = world_objects::loadObjectList(wad);
        if (!list) {
            return std::unexpected(std::move(list.error()));
        }
        objectList = std::move(*list);
    }
    if (settings.objects) {
        objectRecords = selectRecords(objectList->records(), settings.only, "objects", print);
    }
    if (settings.cars) {
        std::ranges::copy_if(world_objects::kCarTypeNames, std::back_inserter(cars),
                             [&settings](std::string_view name) { return wanted(settings.only, name); });
    }
    std::vector<SpriteImage> radarIcons;
    if (settings.radar) {
        for (const std::uint32_t id : graphics::radarIconIds()) {
            if (wanted(settings.only, std::format("icon-{}", id))) {
                radarIcons.push_back(SpriteImage{graphics::radarIconFileName(id),
                                                 characters::referenceRequestHash(graphics::kRadarSheet), id});
            }
        }
    }
    std::vector<SpriteImage> particles;
    if (settings.particles) {
        for (const graphics::ReferenceSprite& sprite : graphics::particleSprites()) {
            if (wanted(settings.only, sprite.name)) {
                particles.push_back(SpriteImage{characters::referenceFileName(0, sprite.name),
                                                characters::referenceRequestHash(sprite.sheet), sprite.rect});
            }
        }
    }
    if (characterRecords.empty() && objectRecords.empty() && cars.empty() && radarIcons.empty() && particles.empty()) {
        return fail(ErrorCode::NotFound, "no character, object, car, radar icon or particle effect matches --only");
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
    if (!cars.empty()) {
        auto folder = makeFolder(settings.outDir, characters::ReferenceList::Cars);
        if (!folder) {
            return std::unexpected(std::move(folder.error()));
        }
        for (const std::string_view name : cars) {
            const world_objects::ObjectRecord* record = objectList ? objectList->find(name) : nullptr;
            const std::string file = characters::referenceFileName(0, name);
            auto rendered = record != nullptr
                                ? renderCar(wad, *record, table, **camera, lights, settings.size, *folder / file)
                                : std::expected<void, ObjectFailure>(std::unexpected(
                                      ObjectFailure{false, Error{ErrorCode::NotFound, "not in the Object List"}}));
            if (!rendered) {
                ++report.failed;
                print(std::format("cars/{}: failed: {}\n", name, rendered.error().error.message));
                continue;
            }
            ++report.cars;
            print(std::format("cars/{}: {} (model {:08x}, textures {:08x})\n", name, file, record->modelHash,
                              record->texturesHash));
        }
    }

    // The sprites: cut from their sheets' pixels, so only the dictionaries' readers are needed.
    chunk::ChunkHandlerTable sheetTable = chunk::ChunkHandlerTable::withDefaults();
    addTextureDictionaryHandlers(sheetTable);
    addSpriteSheetHandlers(sheetTable);
    std::map<std::uint32_t, SheetPixels> sheets;
    if (!radarIcons.empty()) {
        if (auto written = writeSprites(wad, settings.outDir, characters::ReferenceList::Radar, radarIcons, false,
                                        sheetTable, sheets, report.radar, report, print);
            !written) {
            return std::unexpected(std::move(written.error()));
        }
    }
    if (!particles.empty()) {
        if (auto written = writeSprites(wad, settings.outDir, characters::ReferenceList::Particles, particles, true,
                                        sheetTable, sheets, report.particles, report, print);
            !written) {
            return std::unexpected(std::move(written.error()));
        }
    }
    return report;
}

} // namespace coney::platform
