// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>
#include <vector>

#include "core/chunk_system.h"
#include "core/error.h"
#include "fileio/wad.h"
#include "platform/texture_dictionary.h"
#include "world_objects/object_list.h"

namespace rw {
struct Texture;
} // namespace rw

namespace coney::platform {

/// A dictionary resource's first texture, converted for drawing, with the dictionaries that own it (null when the
/// first dictionary holds none). A character's and an object's dictionary holds the texture their untextured material
/// is drawn with (docs/research/characters.md#files, docs/research/level-loading.md#the-object-list).
struct FirstTexture {
    std::vector<TextureDictionary> dictionaries;
    rw::Texture* texture = nullptr;
};

/// Loads the dictionaries of `entry` and picks their first texture, converted for drawing when `forDrawing` (an engine
/// that draws pixels; the NULL one cannot convert). Needs a running RenderEngine; fails as
/// loadTextureDictionaries() and the conversion for drawing do.
[[nodiscard]] std::expected<FirstTexture, Error> loadFirstTexture(const io::Wad& wad, const io::WadEntry& entry,
                                                                  const chunk::ChunkHandlerTable& table,
                                                                  bool forDrawing = true);

/// Why an Object List record's model did not load: Coney cannot load it as one yet (`noModel`: a resource missing or
/// not a `0x47` model), or something else went wrong.
struct ObjectFailure {
    bool noModel = false;
    Error error;
};

/// An Object List record's model, as the level file's `0x47` reader pushes it (a LevelAtomicObject, or a
/// LevelClumpObject for a car's), and its dictionary's first texture. The texture is declared last so that it goes
/// first: the model's materials must have let go of it by then.
struct ObjectModel {
    std::unique_ptr<chunk::LoadedObject> model;
    FirstTexture texture;
};

/// Loads the model and texture dictionary of `record` from `wad`, the model read as the level file's models are (the
/// resources the original's `ObjectModel_MakeInstance` puts an instance over; the second dictionary is not loaded).
/// Needs a running RenderEngine started with the world plugins; `forDrawing` as loadFirstTexture().
///
/// Research: docs/research/objects.md#models, docs/research/level-loading.md#the-object-list
[[nodiscard]] std::expected<ObjectModel, ObjectFailure> loadObjectModel(const io::Wad& wad,
                                                                        const world_objects::ObjectRecord& record,
                                                                        const chunk::ChunkHandlerTable& table,
                                                                        bool forDrawing = true);

} // namespace coney::platform
