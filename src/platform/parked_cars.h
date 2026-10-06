// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/chunk_system.h"
#include "fileio/wad.h"
#include "platform/object_models.h"
#include "raycast/collision_builder.h"
#include "world_objects/cars.h"
#include "world_objects/object_list.h"

namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

/// The parked cars (world_objects::Cars) as the play mode shows them: each drawn with its type's model, the atomics
/// of the undamaged car (world_objects::kCarParts) at their frames, less the removed parts, in its paint; and each a
/// box the humans walk against.
///
/// A type's model is its Object List record's (found by the type name, docs/research/cars.md#model), loaded the first
/// time a car of it is drawn or measured; one that does not load is reported once through `print` and its cars are
/// neither drawn nor solid.
///
/// **Coney's stand-ins** where the page is silent: the paint colours the body's parts (not the lights, windows or
/// wheels) as the model's material colour; the obstacle is one box round the undamaged car's atomics, not the type
/// record's two physics boxes (whose sizes the page gives for the sedan only), turned with the car.
class ParkedCars {
  public:
    /// Draws `cars` (which must outlive this) with models from `wad`, their textures converted for drawing when
    /// `forDrawing`. Needs a running RenderEngine started with the world plugins; must be destroyed before it stops.
    ParkedCars(const io::Wad& wad, const world_objects::Cars& cars, bool forDrawing,
               std::function<void(std::string_view)> print);
    ParkedCars(const ParkedCars&) = delete;
    ParkedCars& operator=(const ParkedCars&) = delete;
    ParkedCars(ParkedCars&&) = delete;
    ParkedCars& operator=(ParkedCars&&) = delete;
    ~ParkedCars();

    /// Draws every car whose model loaded, with the current camera and render states, each atomic placed and handed to
    /// `render` (which lights and draws it).
    void draw(const std::function<void(rw::Atomic*)>& render);

    /// The triangles of every car's box, facing out, in the game's axes: for the level's collision.
    [[nodiscard]] std::vector<raycast::BuildTriangle> obstacles();

    /// Cars drawn by the last draw().
    [[nodiscard]] std::size_t drawn() const { return m_drawn; }

  private:
    // A type's model and the box round its undamaged atomics, in the model's (the game's) axes.
    struct TypeModel {
        ObjectModel model;
        anim::Vec3 min;
        anim::Vec3 max;
    };

    // The model of car type `type`, loading it on first use; null when it does not load.
    TypeModel* model(std::uint8_t type);

    const io::Wad& m_wad;
    const world_objects::Cars& m_cars;
    bool m_forDrawing;
    std::function<void(std::string_view)> m_print;
    chunk::ChunkHandlerTable m_table;
    std::optional<world_objects::ObjectList> m_list; // loaded with the first model
    bool m_listTried = false;
    std::map<std::uint8_t, std::unique_ptr<TypeModel>> m_models; // null: tried and failed
    std::size_t m_drawn = 0;
};

} // namespace coney::platform
