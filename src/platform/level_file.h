// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/chunk_stacks.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/stream.h"
#include "fileio/wad.h"
#include "platform/world_atomic.h"
#include "world/level_object.h"

namespace rw {
struct Atomic;
} // namespace rw

// The level file's RenderWare parts read with librw: the skyline, sky box and cloud box models (chunk 0x47) and the
// glow world (chunk 0x15), and the linking of each model to its dictionary. The level object itself is
// platform-neutral (world/level_object.h). Research: docs/research/level-loading.md#the-level-object

namespace coney::platform {

/// A level model or the level world on the chunk stack and in the level object: one atomic read with librw, unpacked
/// into plain geometry, its frame placed.
class LevelAtomicObject final : public chunk::LoadedObject {
  public:
    /// Wraps `atomic`, placed at `frame`; `what` names it for diagnostics ("level model", "level world").
    LevelAtomicObject(WorldAtomic atomic, const world::FrameMatrix& frame, std::string_view what)
        : m_atomic(std::move(atomic)), m_frame(frame), m_what(what) {}
    [[nodiscard]] std::string_view describe() const override { return m_what; }
    /// The librw atomic. Valid as long as this object.
    [[nodiscard]] rw::Atomic* atomic() const { return m_atomic.atomic(); }
    /// The frame it was placed with at load (the cloud box turns from there each frame).
    [[nodiscard]] const world::FrameMatrix& frame() const { return m_frame; }
    /// Places the atomic at `frame` for drawing, leaving frame() as it was loaded.
    void place(const world::FrameMatrix& frame) { m_atomic.setTransform(frame); }

  private:
    WorldAtomic m_atomic;
    world::FrameMatrix m_frame;
    std::string_view m_what;
};

/// A model of several atomics on the chunk stack, as a car's is (docs/research/cars.md#model): each atomic read
/// with librw and unpacked, in the clump's order, with the frame it was placed at. The level file holds none.
class LevelClumpObject final : public chunk::LoadedObject {
  public:
    /// One atomic and the frame it was placed at.
    struct Part {
        WorldAtomic atomic;
        world::FrameMatrix frame;
    };

    explicit LevelClumpObject(std::vector<Part> parts) : m_parts(std::move(parts)) {}
    [[nodiscard]] std::string_view describe() const override { return "level clump"; }
    /// The atomics, in the clump's order. Valid as long as this object.
    [[nodiscard]] std::span<const Part> parts() const { return m_parts; }

  private:
    std::vector<Part> m_parts;
};

/// The librw atomic of a level object's model or level world, or null when `object` is not a LevelAtomicObject.
[[nodiscard]] rw::Atomic* levelAtomic(const chunk::LoadedObject* object);

/// The stream reader of chunk 0x47 (Preinstance Object): a clump, rearranged by world::extractClumpModels(), each
/// atomic read as a game-pipeline atomic, placed by its frames and unpacked; pushed under type 0x41 as a
/// LevelAtomicObject for a clump of one atomic, as a LevelClumpObject for one of several (a car). Fails as those
/// steps do.
/// @orig 0x0017f2c0 ChunkReader_PreinstanceObject (unknown)
[[nodiscard]] std::expected<void, Error> readPreinstanceObjectChunk(io::Stream& chunk, const chunk::ChunkHeader& header,
                                                                    chunk::ChunkStacks& stacks);

/// The stream reader of chunk 0x15 (Sector BSP Data): a world of one sector, rearranged by world::extractLevelWorld()
/// and read as an atomic in RenderWare's default layout, at the origin, then unpacked; pushed as a LevelAtomicObject
/// under type 0x42. Its materials find their textures in the texture dictionary just below it on the chunk stack (the
/// glow sprites): RenderWare finds them in every live dictionary, newest first, and Coney registers only that one for
/// the read. Fails as those steps do.
/// @orig 0x00197b30 ChunkReader_SectorBspData (unknown)
[[nodiscard]] std::expected<void, Error> readSectorBspChunk(io::Stream& chunk, const chunk::ChunkHeader& header,
                                                            chunk::ChunkStacks& stacks);

/// Links a level model to its dictionary as the original does: the model's first material gets the dictionary's first
/// texture, whatever the material named (the disc's models name none). The game's pipeline was given when the model
/// was read. Fails with ErrorCode::Invalid when the objects are not a LevelAtomicObject and a TextureDictionaryObject
/// or the dictionary is empty.
/// @orig 0x0040cdd8 LevelObject_LinkModel (unknown)
[[nodiscard]] std::expected<void, Error> linkLevelModel(chunk::LoadedObject& model, chunk::LoadedObject& dictionary);

/// Registers every handler the level file needs in `table`: the texture dictionaries (0x0B, 0x2A), the models (0x47),
/// the level world (0x15) and the platform-neutral ones (world::addLevelFileHandlers()).
void addLevelFileReaders(chunk::ChunkHandlerTable& table);

/// Loads `<level>.lev` from `wad` into a level object. With `forDrawing`, its four texture dictionaries are converted
/// for the OpenGL renderer afterwards. Needs a running RenderEngine started with the world plugins. Fails with
/// ErrorCode::NotFound when the WAD has no such file, and as world::loadLevelFile() does.
[[nodiscard]] std::expected<std::unique_ptr<world::LevelObject>, Error>
loadLevel(const io::Wad& wad, std::string_view level, bool forDrawing);

} // namespace coney::platform
