// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

#include "animation/anim_math.h"
#include "core/chunk_system.h"
#include "fileio/wad.h"
#include "platform/object_models.h"
#include "world_objects/object_list.h"

namespace rw {
struct Atomic;
} // namespace rw

namespace coney::platform {

/// The transform that draws an object's model in RenderWare's axes: the model's vertices taken in the game's axes (z
/// up) and placed by the object's pose (`rotation` and `position`, game axes), then carried into RenderWare's, where a
/// game point (x, y, z) is (x, z, −y). The model's own frame (its clump's atomic frame) is left out. **Coney's
/// reading** (the instance's matrix is not on docs/research/objects.md#models): the frames are either the identity or
/// a turn of z up into RenderWare's y up, some with a stray offset (the objective column's 2.69 m), and only with the
/// frame left out does the objective disc stand upright on top of its column where the runtime sees it
/// (docs/research/objects.md#objective-markers).
[[nodiscard]] anim::Mat34 objectRenderTransform(anim::Quat rotation, anim::Vec3 position);

/// Dynamic objects drawn where something puts them: each by its handle, with the model of its type (the Object List
/// record of the type's model hash), loaded from the disc the first time a type is placed and shared by every object
/// of that type. A model that does not load is reported once through `print`, and its objects are not drawn.
///
/// Coney's stand-in for the original's object instances and `ObjectRender_Draw` (docs/research/objects.md#models,
/// docs/research/objects.md#tint): each object's tint multiplies its model's colours and its alpha the model's
/// opacity; given the camera, the size cull and the `ObjShow` distance fade it; an alpha under 10 is not drawn. The
/// front end draws without a camera (its Wonder Wheel types are exempt from the size cull) and with the world
/// renderer's ambient; play draws through the level's object lights.
class PlacedObjects {
  public:
    /// How an object looks beyond its model and pose.
    struct Look {
        std::uint32_t tint = 0xFFFFFFFFU; ///< `0xRRGGBBAA`; white opaque is no tint.
        float fadeDistance = 0.0F;        ///< `ObjShow`'s distance; 0 for none.
        bool sizeCullExempt = false;      ///< Never size-culled.
        /// Drawn translucent with Z writes off: **Coney's stand-in** for the objective column's blend, which is not
        /// traced (alpha blend over what is drawn, after the opaque objects).
        bool translucent = false;
    };

    /// What a draw needs beyond the render state.
    struct DrawOptions {
        /// The camera in RenderWare's axes: with it, the size cull and the `ObjShow` distance apply.
        std::optional<anim::Vec3> camera;
        /// Draws one placed atomic (its lights); null: the atomic's own render with the current lights.
        std::function<void(rw::Atomic*)> render;
    };

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
    /// game's axes) looking as `look` says, loading the type's model if it is not yet.
    void place(double handle, std::uint32_t modelHash, anim::Vec3 position, anim::Quat rotation, const Look& look = {});
    /// Shows or hides object `handle` (`simple_object`'s messages 0x12 and 0x13); an unplaced handle is ignored. A
    /// placed object starts shown.
    void setVisible(double handle, bool visible);
    /// Forgets object `handle`.
    void remove(double handle);
    /// Forgets every object (the models stay loaded).
    void clear() { m_objects.clear(); }

    /// Draws every shown object whose model loaded and whose alpha comes to 10 or more, the opaque ones first, then
    /// the translucent ones, as `options` says.
    /// @orig 0x0017fd78 ObjectRender_Draw (unknown)
    void draw(const DrawOptions& options = {});

    /// Objects placed, and of them shown with a model.
    [[nodiscard]] std::size_t placed() const { return m_objects.size(); }
    [[nodiscard]] std::size_t drawable() const;
    /// How many objects the last draw() drew.
    [[nodiscard]] std::size_t drawn() const { return m_drawn; }
    /// The model loaded for `modelHash` (loading it now), null when it does not load: its atomic for a held object.
    [[nodiscard]] rw::Atomic* atomicOf(std::uint32_t modelHash);
    /// Whether object `handle` is placed and shown.
    [[nodiscard]] bool visible(double handle) const;

  private:
    // One placed object.
    struct Placed {
        std::uint32_t modelHash = 0;
        anim::Vec3 position;
        anim::Quat rotation;
        Look look;
        bool visible = true;
    };

    // The model of `modelHash`, loading it on first use; null when it did not load.
    const ObjectModel* model(std::uint32_t modelHash);
    // Places and draws one object with its alpha `alpha`, its materials tinted for the draw and put back after.
    void drawOne(const Placed& object, rw::Atomic* atomic, int alpha, const DrawOptions& options) const;
    // The alpha `object` is drawn at (its tint's, the size cull's and the `ObjShow` distance's), placing its atomic.
    int alphaOf(const Placed& object, rw::Atomic* atomic, const DrawOptions& options) const;

    const io::Wad& m_wad;
    const world_objects::ObjectList& m_list;
    std::function<void(std::string_view)> m_print;
    chunk::ChunkHandlerTable m_table;
    bool m_forDrawing;
    std::map<std::uint32_t, std::unique_ptr<ObjectModel>> m_models; // null: tried and failed
    std::map<double, Placed> m_objects;
    std::size_t m_drawn = 0;
};

} // namespace coney::platform
