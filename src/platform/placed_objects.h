// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string_view>

#include "animation/anim_math.h"
#include "core/chunk_system.h"
#include "fileio/wad.h"
#include "platform/object_models.h"
#include "world_objects/object_list.h"

// librw's atomic, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

/// The transform that draws an object's model in RenderWare's axes: the turn of the model's own frame (`modelFrame`,
/// as it was placed when read), then the object's pose (`rotation` and `position`, in the game's axes, z up) carried
/// into RenderWare's, where a game point (x, y, z) is (x, z, −y). The model's own axes are taken to turn the same way,
/// its y up being the game's z up (inferred: the models stand with y up,
/// docs/research/level-loading.md#the-object-list, and the original carries game positions into RenderWare's axes so,
/// docs/research/world.md).
///
/// The model frame's translation is not used: it is where the model was authored (the Wonder Wheel's neon signs hold
/// (−29.32, 19.94, 0.07), its carts (−78.2, 0, 0)), and the object's pose replaces it. With it, the neons would stand
/// 35 m off the hub they ring at runtime (inferred, docs/research/objects.md#models).
[[nodiscard]] anim::Mat34 objectRenderTransform(anim::Quat rotation, anim::Vec3 position,
                                                const anim::Mat34& modelFrame);

/// Dynamic objects drawn where something puts them: each by its handle, with the model of its type (the Object List
/// record of the type's model hash), loaded from the disc the first time a type is placed and shared by every object
/// of that type. A model that does not load is reported once through `print`, and its objects are not drawn.
///
/// Coney's stand-in for the original's object instances and `ObjectRender_Draw` (docs/research/objects.md#models), as
/// far as the front end needs them: no size cull (the Wonder Wheel's types are exempt from it), no per-object lights
/// (the world renderer's ambient), and no tint, whose use by the renderer is not traced.
class PlacedObjects {
  public:
    /// Loads models from `wad` by `list`'s records, their textures converted for drawing when `forDrawing` (an engine
    /// that draws pixels). Needs a running RenderEngine started with the world plugins; must be destroyed before it
    /// stops. `list` must outlive this.
    PlacedObjects(const io::Wad& wad, const world_objects::ObjectList& list,
                  std::function<void(std::string_view)> print, bool forDrawing);
    PlacedObjects(const PlacedObjects&) = delete;
    PlacedObjects& operator=(const PlacedObjects&) = delete;
    PlacedObjects(PlacedObjects&&) = delete;
    PlacedObjects& operator=(PlacedObjects&&) = delete;
    ~PlacedObjects();

    /// Puts object `handle`, of the type whose model hash is `modelHash`, at `position` turned by `rotation` (the
    /// game's axes), loading the type's model if it is not yet.
    void place(double handle, std::uint32_t modelHash, anim::Vec3 position, anim::Quat rotation);
    /// Shows or hides object `handle` (`simple_object`'s messages 0x12 and 0x13); an unplaced handle is ignored. A
    /// placed object starts shown.
    void setVisible(double handle, bool visible);
    /// Forgets object `handle`.
    void remove(double handle);
    /// Forgets every object (the models stay loaded).
    void clear() { m_objects.clear(); }

    /// Draws every shown object whose model loaded, with the current camera and render states, each atomic through
    /// `render` (the caller's lights, SceneLighting); empty renders it with the current librw world's lights, which
    /// must then be set.
    void draw(const std::function<void(rw::Atomic*)>& render = {}) const;

    /// Objects placed, and of them shown with a model.
    [[nodiscard]] std::size_t placed() const { return m_objects.size(); }
    [[nodiscard]] std::size_t drawable() const;
    /// Whether object `handle` is placed and shown.
    [[nodiscard]] bool visible(double handle) const;

  private:
    // One placed object.
    struct Placed {
        std::uint32_t modelHash = 0;
        anim::Vec3 position;
        anim::Quat rotation;
        bool visible = true;
    };

    // The model of `modelHash`, loading it on first use; null when it did not load.
    const ObjectModel* model(std::uint32_t modelHash);

    const io::Wad& m_wad;
    const world_objects::ObjectList& m_list;
    std::function<void(std::string_view)> m_print;
    chunk::ChunkHandlerTable m_table;
    bool m_forDrawing;
    std::map<std::uint32_t, std::unique_ptr<ObjectModel>> m_models; // null: tried and failed
    std::map<double, Placed> m_objects;
};

} // namespace coney::platform
