// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "core/chunk_system.h"
#include "fileio/wad.h"
#include "graphics/car_draw.h"
#include "platform/object_models.h"
#include "world_objects/cars.h"
#include "world_objects/object_list.h"

namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

/// The parked cars (world_objects::Cars) as the play mode shows them: each drawn with its type's model in the two
/// passes of docs/research/graphics.md#car-draw, each part's atomic at its frame (its damaged form from half damage),
/// less the removed parts, the paint on the painted parts; and each a box the humans walk against.
///
/// A type's model is its Object List record's (found by the type name, docs/research/cars.md#model), loaded the first
/// time a car of it is drawn or measured; one that does not load is reported once through `print` and its cars are
/// neither drawn nor solid.
///
/// **Coney's stand-ins** where the page is silent: the paint is the model's material colour; the car's environment map
/// (its second texture resource) and its first second's fade-in are not drawn; the obstacle is one box round the
/// undamaged car's atomics, not the type record's two physics boxes (whose sizes the page gives for the sedan only),
/// turned with the car.
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

    /// Draws `pass` of every car whose model loaded, through the current camera: each atomic graphics::carAtomicDraws()
    /// picks (by part, damage and pass) placed, coloured by the paint mask and handed to `render` (which lights and
    /// draws it). Both sides of every triangle are drawn; the opaque pass writes Z and goes through the cars in order,
    /// the glass pass does not write Z and goes from the last car. Leaves Z write on and back faces culled.
    /// @orig 0x00172c70 CarInstance_Render (unknown)
    void draw(const std::function<void(rw::Atomic*)>& render, graphics::CarPass pass);

    /// Cars drawn by the last draw().
    [[nodiscard]] std::size_t drawn() const { return m_drawn; }

  private:
    // A type's model.
    struct TypeModel {
        ObjectModel model;
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
