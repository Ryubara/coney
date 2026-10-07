// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/placed_objects.h"

#include <cmath>
#include <format>
#include <utility>
#include <vector>

#include <rw.h>

#include "platform/level_file.h"
#include "world/level_object.h"
#include "world_objects/object_tasks.h"

namespace coney::platform {

namespace {

// The game's axes into RenderWare's, (x, y, z) to (x, z, -y).
constexpr anim::Mat34 kGameToRw{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {}};

// Our matrix as a frame for librw.
world::FrameMatrix toFrame(const anim::Mat34& m) {
    return world::FrameMatrix{.right = {m.x.x, m.x.y, m.x.z},
                              .up = {m.y.x, m.y.y, m.y.z},
                              .at = {m.z.x, m.z.y, m.z.z},
                              .position = {m.t.x, m.t.y, m.t.z}};
}

} // namespace

anim::Mat34 objectRenderTransform(anim::Quat rotation, anim::Vec3 position) {
    return anim::multiply(kGameToRw, anim::transform(rotation, position));
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

void PlacedObjects::place(double handle, std::uint32_t modelHash, anim::Vec3 position, anim::Quat rotation,
                          const Look& look) {
    (void)model(modelHash);
    Placed& object = m_objects[handle];
    object.modelHash = modelHash;
    object.position = position;
    object.rotation = rotation;
    object.look = look;
}

rw::Atomic* PlacedObjects::atomicOf(std::uint32_t modelHash) {
    const ObjectModel* loaded = model(modelHash);
    return loaded != nullptr ? levelAtomic(loaded->model.get()) : nullptr;
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

const PlacedObjects::Look* PlacedObjects::look(double handle) const {
    const auto found = m_objects.find(handle);
    return found != m_objects.end() ? &found->second.look : nullptr;
}

int PlacedObjects::alphaOf(const Placed& object, rw::Atomic* atomic, const DrawOptions& options) const {
    float alpha = static_cast<float>(object.look.tint & 0xFFU);
    if (!options.camera) {
        return static_cast<int>(alpha);
    }
    // The size cull by the model's bounding radius over its squared camera distance, then the ObjShow distance.
    const rw::Sphere* sphere = atomic->getWorldBoundingSphere();
    const anim::Vec3& eye = *options.camera;
    const float dx = sphere->center.x - eye.x;
    const float dy = sphere->center.y - eye.y;
    const float dz = sphere->center.z - eye.z;
    const float distanceSq = dx * dx + dy * dy + dz * dz;
    if (!object.look.sizeCullExempt) {
        alpha *= world_objects::sizeFade(sphere->radius, distanceSq);
    }
    alpha *= world_objects::showDistanceFade(object.look.fadeDistance, std::sqrt(distanceSq));
    return static_cast<int>(alpha);
}

void PlacedObjects::drawOne(const Placed& object, rw::Atomic* atomic, int alpha, const DrawOptions& options) const {
    // The tint multiplies each material's colour, its alpha the material's; the materials are shared by every object
    // of the type, so they are put back after.
    rw::Geometry* geometry = atomic->geometry;
    std::vector<rw::RGBA> own;
    const auto channel = [](rw::uint8 base, std::uint32_t tint) {
        return static_cast<rw::uint8>((static_cast<std::uint32_t>(base) * (tint & 0xFFU) + 127U) / 255U);
    };
    const std::uint32_t tint = object.look.tint;
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        rw::RGBA& colour = geometry->matList.materials[i]->color;
        own.push_back(colour);
        colour = rw::RGBA{channel(colour.red, tint >> 24U), channel(colour.green, tint >> 16U),
                          channel(colour.blue, tint >> 8U), channel(colour.alpha, static_cast<std::uint32_t>(alpha))};
    }
    // librw multiplies by the material's colour only for a geometry that modulates: set for the draw when the colour
    // changes anything.
    const rw::uint32 flags = geometry->flags;
    if (tint != 0xFFFFFFFFU || alpha < 255) {
        geometry->flags |= rw::Geometry::MODULATE;
    }
    if (options.render) {
        options.render(atomic);
    } else {
        atomic->render();
    }
    geometry->flags = flags;
    for (rw::int32 i = 0; i < geometry->matList.numMaterials; ++i) {
        geometry->matList.materials[i]->color = own.at(static_cast<std::size_t>(i));
    }
}

void PlacedObjects::draw(const DrawOptions& options) {
    m_drawn = 0;
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    // Two passes: the opaque objects with Z writes, then the translucent ones over them (Coney's order: the original
    // draws objects in its instance order).
    for (const bool translucentPass : {false, true}) {
        for (const auto& [handle, object] : m_objects) {
            const auto found = m_models.find(object.modelHash);
            if (!object.visible || found == m_models.end() || found->second == nullptr) {
                continue;
            }
            auto* level = dynamic_cast<LevelAtomicObject*>(found->second->model.get());
            if (level == nullptr) {
                continue;
            }
            // Objects of one type share their model's atomic: each is placed, then drawn, in turn.
            level->place(toFrame(objectRenderTransform(object.rotation, object.position)));
            const int alpha = alphaOf(object, level->atomic(), options);
            if (alpha < world_objects::kMinDrawnAlpha) {
                continue;
            }
            if ((object.look.translucent || alpha < 255) != translucentPass) {
                continue;
            }
            rw::SetRenderState(rw::ZWRITEENABLE, object.look.translucent ? 0 : 1);
            drawOne(object, level->atomic(), alpha, options);
            ++m_drawn;
        }
    }
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
}

} // namespace coney::platform
