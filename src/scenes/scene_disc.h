// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>

#include "core/error.h"
#include "fileio/wad.h"
#include "scenes/scene_cache.h"
#include "scenes/scene_list.h"

namespace coney::scenes {

/// Loads the scene list from `scene_list.cnk` in `wad`: the chunk container read as the boot reads it (step 13), its
/// Scene List chunk decoded. Fails with ErrorCode::NotFound when the file or the chunk is missing, and as the chunk
/// loader and SceneList::parse() do.
/// @orig 0x003535e8 Boot_LoadSceneList (unknown)
[[nodiscard]] std::expected<SceneList, Error> loadSceneList(const io::Wad& wad);

/// Reads `<name>.scn` records from `wad`, which must outlive the source. **Coney stand-in**: the 24 records whose
/// list names are cut to 16 characters hash to no WAD name and are not found (the disc check finds them by content);
/// no scene the first mission plays is among them.
[[nodiscard]] SceneRecordSource wadSceneSource(const io::Wad& wad);

} // namespace coney::scenes
