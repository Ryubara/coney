// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/placed_objects.h"

#include <format>
#include <utility>

#include <rw.h>

#include "platform/level_file.h"
#include "world/level_object.h"

namespace coney::platform {

namespace {

// The game's axes into RenderWare's, (x, y, z) to (x, z, -y), and back.
constexpr anim::Mat34 kGameToRw{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {}};
constexpr anim::Mat34 kRwToGame{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, -1.0F, 0.0F}, {}};

// A loaded frame as our matrix.
anim::Mat34 toMat34(const world::FrameMatrix& m) {
    return anim::Mat34{{m.right.x, m.right.y, m.right.z},
                       {m.up.x, m.up.y, m.up.z},
                       {m.at.x, m.at.y, m.at.z},
                       {m.position.x, m.position.y, m.position.z}};
}

// Our matrix as a frame for librw.
world::FrameMatrix toFrame(const anim::Mat34& m) {
    return world::FrameMatrix{.right = {m.x.x, m.x.y, m.x.z},
                              .up = {m.y.x, m.y.y, m.y.z},
                              .at = {m.z.x, m.z.y, m.z.z},
                              .position = {m.t.x, m.t.y, m.t.z}};
}

} // namespace

anim::Mat34 objectRenderTransform(anim::Quat rotation, anim::Vec3 position, const anim::Mat34& modelFrame) {
    const anim::Mat34 game = anim::transform(rotation, position);
    const anim::Mat34 world = anim::multiply(kGameToRw, anim::multiply(game, kRwToGame));
    return anim::multiply(world, modelFrame);
}

PlacedObjects::PlacedObjects(const io::Wad& wad, const world_objects::ObjectList& list,
                             std::function<void(std::string_view)> print, bool forDrawing)
    : m_wad(wad), m_list(list), m_print(std::move(print)), m_table(chunk::ChunkHandlerTable::withDefaults()),
      m_forDrawing(forDrawing) {
    // The readers an Object List model needs: its texture dictionary and the level file's 0x47 model.
    addTextureDictionaryHandlers(m_table);
    m_table.setHandlers(world::kPreinstanceObjectChunk, chunk::ChunkHandlers{{}, readPreinstanceObjectChunk});
}

PlacedObjects::~PlacedObjects() {
    // The materials let go of their textures before the dictionaries that own them go.
    for (auto& [hash, model] : m_models) {
        rw::Atomic* atomic = model != nullptr ? levelAtomic(model->model.get()) : nullptr;
        if (atomic != nullptr && atomic->geometry->matList.numMaterials > 0) {
            atomic->geometry->matList.materials[0]->setTexture(nullptr);
        }
    }
}

const ObjectModel* PlacedObjects::model(std::uint32_t modelHash) {
    if (const auto found = m_models.find(modelHash); found != m_models.end()) {
        return found->second.get();
    }
    std::unique_ptr<ObjectModel>& slot = m_models[modelHash];
    const world_objects::ObjectRecord* record = m_list.findByHash(modelHash);
    if (record == nullptr) {
        if (m_print) {
            m_print(std::format("objects: no Object List record {:#010x}\n", modelHash));
        }
        return nullptr;
    }
    auto loaded = loadObjectModel(m_wad, *record, m_table, m_forDrawing);
    if (!loaded || levelAtomic(loaded->model.get()) == nullptr) {
        if (m_print) {
            m_print(std::format("objects: model {:#010x} not loaded: {}\n", modelHash,
                                loaded ? std::string("not one atomic") : loaded.error().error.message));
        }
        return nullptr;
    }
    // As a level model is linked: the model's first material draws with the dictionary's first texture.
    if (rw::Atomic* atomic = levelAtomic(loaded->model.get());
        loaded->texture.texture != nullptr && atomic->geometry->matList.numMaterials > 0) {
        atomic->geometry->matList.materials[0]->setTexture(loaded->texture.texture);
    }
    slot = std::make_unique<ObjectModel>(std::move(*loaded));
    return slot.get();
}

void PlacedObjects::place(double handle, std::uint32_t modelHash, anim::Vec3 position, anim::Quat rotation) {
    (void)model(modelHash);
    Placed& object = m_objects[handle];
    object.modelHash = modelHash;
    object.position = position;
    object.rotation = rotation;
}

void PlacedObjects::setVisible(double handle, bool visible) {
    if (const auto found = m_objects.find(handle); found != m_objects.end()) {
        found->second.visible = visible;
    }
}

void PlacedObjects::remove(double handle) { m_objects.erase(handle); }

std::size_t PlacedObjects::drawable() const {
    std::size_t count = 0;
    for (const auto& [handle, object] : m_objects) {
        const auto found = m_models.find(object.modelHash);
        if (object.visible && found != m_models.end() && found->second != nullptr) {
            ++count;
        }
    }
    return count;
}

bool PlacedObjects::visible(double handle) const {
    const auto found = m_objects.find(handle);
    return found != m_objects.end() && found->second.visible;
}

void PlacedObjects::draw() const {
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    // Objects of one type share their model's atomic: each is placed, then drawn, in turn.
    for (const auto& [handle, object] : m_objects) {
        const auto found = m_models.find(object.modelHash);
        if (!object.visible || found == m_models.end() || found->second == nullptr) {
            continue;
        }
        auto* atomic = dynamic_cast<LevelAtomicObject*>(found->second->model.get());
        if (atomic == nullptr) {
            continue;
        }
        atomic->place(toFrame(objectRenderTransform(object.rotation, object.position, toMat34(atomic->frame()))));
        atomic->atomic()->render();
    }
}

} // namespace coney::platform
