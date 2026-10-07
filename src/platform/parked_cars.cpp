// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/parked_cars.h"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <utility>

#include <rw.h>

#include "graphics/car_draw.h"
#include "platform/level_file.h"
#include "platform/texture_dictionary.h"
#include "world/level_object.h"

namespace coney::platform {

namespace {

// The game's axes into RenderWare's: (x, y, z) to (x, z, -y).
constexpr anim::Mat34 kGameToRw{{1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, 1.0F, 0.0F}, {}};

// A loaded frame as our matrix.
anim::Mat34 toMat34(const world::FrameMatrix& m) {
    return anim::Mat34{{m.right.x, m.right.y, m.right.z},
                       {m.up.x, m.up.y, m.up.z},
                       {m.at.x, m.at.y, m.at.z},
                       {m.position.x, m.position.y, m.position.z}};
}

// Places `atomic`'s frame at `m` (RenderWare's axes).
void place(rw::Atomic* atomic, const anim::Mat34& m) {
    rw::Matrix matrix;
    matrix.setIdentity();
    matrix.right = rw::V3d{m.x.x, m.x.y, m.x.z};
    matrix.up = rw::V3d{m.y.x, m.y.y, m.y.z};
    matrix.at = rw::V3d{m.z.x, m.z.y, m.z.z};
    matrix.pos = rw::V3d{m.t.x, m.t.y, m.t.z};
    matrix.update();
    atomic->getFrame()->transform(&matrix, rw::COMBINEREPLACE);
}

// The car's clump, or null when the model is not one with the undamaged car's atomics.
const LevelClumpObject* clumpOf(const ObjectModel& model) {
    const auto* clump = dynamic_cast<const LevelClumpObject*>(model.model.get());
    return clump != nullptr && clump->parts().size() >= world_objects::kCarParts ? clump : nullptr;
}

// The twelve triangles of the box [`min`, `max`] (model axes) placed by `pose`, each facing out of the box.
void addBox(std::vector<raycast::BuildTriangle>& triangles, anim::Vec3 min, anim::Vec3 max, const anim::Mat34& pose) {
    // Corner i has x from bit 0, y from bit 1, z from bit 2.
    std::array<raycast::Vec3, 8> corners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const anim::Vec3 local{(i & 1U) != 0 ? max.x : min.x, (i & 2U) != 0 ? max.y : min.y,
                               (i & 4U) != 0 ? max.z : min.z};
        const anim::Vec3 p = anim::transformPoint(pose, local);
        corners.at(i) = raycast::Vec3{p.x, p.y, p.z};
    }
    // Each face as four corners counter-clockwise seen from outside, so (b - a) × (c - a) points out.
    constexpr std::array<std::array<std::size_t, 4>, 6> kFaces{{
        {0, 2, 3, 1}, // bottom (-z)
        {4, 5, 7, 6}, // top (+z)
        {0, 1, 5, 4}, // -y
        {2, 6, 7, 3}, // +y
        {0, 4, 6, 2}, // -x
        {1, 3, 7, 5}, // +x
    }};
    for (const auto& face : kFaces) {
        raycast::BuildTriangle first;
        first.corners = {corners.at(face[0]), corners.at(face[1]), corners.at(face[2])};
        raycast::BuildTriangle second;
        second.corners = {corners.at(face[0]), corners.at(face[2]), corners.at(face[3])};
        triangles.push_back(first);
        triangles.push_back(second);
    }
}

} // namespace

ParkedCars::ParkedCars(const io::Wad& wad, const world_objects::Cars& cars, bool forDrawing,
                       std::function<void(std::string_view)> print)
    : m_wad(wad), m_cars(cars), m_forDrawing(forDrawing), m_print(std::move(print)),
      m_table(chunk::ChunkHandlerTable::withDefaults()) {
    // The readers an Object List model needs: its texture dictionary and the level file's 0x47 model.
    addTextureDictionaryHandlers(m_table);
    m_table.setHandlers(world::kPreinstanceObjectChunk, chunk::ChunkHandlers{{}, readPreinstanceObjectChunk});
}

ParkedCars::~ParkedCars() {
    // The materials let go of their textures before the dictionaries that own them go.
    for (auto& [type, model] : m_models) {
        const LevelClumpObject* clump = model != nullptr ? clumpOf(model->model) : nullptr;
        if (clump == nullptr) {
            continue;
        }
        for (const LevelClumpObject::Part& part : clump->parts()) {
            rw::Geometry* geometry = part.atomic.atomic()->geometry;
            for (rw::int32 m = 0; m < geometry->matList.numMaterials; ++m) {
                geometry->matList.materials[m]->setTexture(nullptr);
            }
        }
    }
}

ParkedCars::TypeModel* ParkedCars::model(std::uint8_t type) {
    if (const auto found = m_models.find(type); found != m_models.end()) {
        return found->second.get();
    }
    std::unique_ptr<TypeModel>& slot = m_models[type];
    const std::string_view name = world_objects::kCarTypeNames.at(type);
    if (!m_listTried) {
        m_listTried = true;
        if (auto list = world_objects::loadObjectList(m_wad); list) {
            m_list = std::move(*list);
        } else if (m_print) {
            m_print(std::format("cars: no Object List: {}\n", list.error().message));
        }
    }
    const world_objects::ObjectRecord* record = m_list ? m_list->find(name) : nullptr;
    if (record == nullptr) {
        if (m_print) {
            m_print(std::format("cars: no Object List record for {}\n", name));
        }
        return nullptr;
    }
    auto loaded = loadObjectModel(m_wad, *record, m_table, m_forDrawing);
    if (!loaded || clumpOf(*loaded) == nullptr) {
        if (m_print) {
            m_print(std::format("cars: model of {} not loaded: {}\n", name,
                                loaded ? std::string("not a car's clump") : loaded.error().error.message));
        }
        return nullptr;
    }
    auto typeModel = std::make_unique<TypeModel>(TypeModel{std::move(*loaded), {}, {}});
    const LevelClumpObject& clump = *clumpOf(typeModel->model);
    // Every atomic, damaged forms included, draws with the dictionary's first texture, as the model's untextured
    // materials are linked.
    for (const LevelClumpObject::Part& part : clump.parts()) {
        rw::Geometry* geometry = part.atomic.atomic()->geometry;
        geometry->flags |= rw::Geometry::MODULATE;
        for (rw::int32 m = 0; m < geometry->matList.numMaterials; ++m) {
            geometry->matList.materials[m]->setTexture(typeModel->model.texture.texture);
        }
    }
    // The box round the undamaged car's vertices, each placed by its part's frame.
    constexpr float kHuge = std::numeric_limits<float>::max();
    anim::Vec3 min{kHuge, kHuge, kHuge};
    anim::Vec3 max{-kHuge, -kHuge, -kHuge};
    for (const LevelClumpObject::Part& part : clump.parts().first(world_objects::kCarParts)) {
        rw::Geometry* geometry = part.atomic.atomic()->geometry;
        const anim::Mat34 frame = toMat34(part.frame);
        if (geometry->numMorphTargets == 0) {
            continue;
        }
        const rw::V3d* vertices = geometry->morphTargets[0].vertices;
        for (rw::int32 v = 0; vertices != nullptr && v < geometry->numVertices; ++v) {
            const anim::Vec3 p = anim::transformPoint(frame, anim::Vec3{vertices[v].x, vertices[v].y, vertices[v].z});
            min = anim::Vec3{std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
            max = anim::Vec3{std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
        }
    }
    if (min.x > max.x) {
        min = max = anim::Vec3{};
    }
    typeModel->min = min;
    typeModel->max = max;
    slot = std::move(typeModel);
    return slot.get();
}

void ParkedCars::draw(const std::function<void(rw::Atomic*)>& render, graphics::CarPass pass) {
    const bool glass = pass == graphics::CarPass::Glass;
    if (!glass) {
        m_drawn = 0;
    }
    // Both passes draw both sides; only the opaque one writes Z, so the glass blends over what is behind it.
    rw::SetRenderState(rw::ZTESTENABLE, 1);
    rw::SetRenderState(rw::ZWRITEENABLE, glass ? 0 : 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
    const std::vector<world_objects::Car>& cars = m_cars.all();
    for (std::size_t i = 0; i < cars.size(); ++i) {
        // The glass pass goes from the end of the list.
        const world_objects::Car& car = cars[glass ? cars.size() - 1 - i : i];
        TypeModel* typeModel = car.type ? model(*car.type) : nullptr;
        if (typeModel == nullptr) {
            continue;
        }
        const LevelClumpObject& clump = *clumpOf(typeModel->model);
        // The models stand in the game's axes (z up, front to +y), so the car's pose then the turn into RenderWare's.
        const anim::Mat34 pose = anim::multiply(kGameToRw, anim::transform(car.rotation, car.position));
        const world_objects::CarPaint paint =
            car.painted ? world_objects::paintOf(car.paint[0]) : world_objects::CarPaint{};
        // Every atomic of the model: the undamaged parts and, when the model has them, their damaged forms.
        const auto parts = clump.parts();
        for (std::size_t a = 0; a < parts.size(); ++a) {
            if (!graphics::carAtomicDraws(a, car.removedParts, car.damage, pass)) {
                continue;
            }
            rw::Atomic* atomic = parts[a].atomic.atomic();
            // The paint on the painted parts, white elsewhere so the texture shows as it is (chrome, lights, glass).
            const rw::RGBA colour = graphics::carPartPainted(graphics::carPartOf(a))
                                        ? rw::RGBA{paint.r, paint.g, paint.b, 255}
                                        : rw::RGBA{255, 255, 255, 255};
            for (rw::int32 m = 0; m < atomic->geometry->matList.numMaterials; ++m) {
                atomic->geometry->matList.materials[m]->color = colour;
            }
            place(atomic, anim::multiply(pose, toMat34(parts[a].frame)));
            render(atomic);
        }
        if (!glass) {
            ++m_drawn;
        }
    }
    rw::SetRenderState(rw::ZWRITEENABLE, 1);
    rw::SetRenderState(rw::CULLMODE, rw::CULLBACK);
}

std::vector<raycast::BuildTriangle> ParkedCars::obstacles() {
    std::vector<raycast::BuildTriangle> triangles;
    for (const world_objects::Car& car : m_cars.all()) {
        const TypeModel* typeModel = car.type ? model(*car.type) : nullptr;
        if (typeModel == nullptr) {
            continue;
        }
        addBox(triangles, typeModel->min, typeModel->max, anim::transform(car.rotation, car.position));
    }
    return triangles;
}

} // namespace coney::platform
