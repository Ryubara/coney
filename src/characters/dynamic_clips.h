// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "animation/anim_clip.h"
#include "core/error.h"
#include "fileio/wad.h"

// The dynamic animations: clips a level's scripts name by file (`SetDynamicAnimation`, `HuUseAnim`), each its own
// animation resource `<name>.anm` on the disc (a keyframe chunk 0x00 and its descriptor 0x02), loaded when first asked
// for and kept for the level.
// Research: docs/research/formats/wad-contents.md, docs/research/rumble.md#match-end

namespace coney::characters {

/// The clip of the animation resource `name` (`.anm` added when it has none), loaded from `wad` through the chunk
/// system's default handlers and the Anim Data handler. Fails when the entry is missing or builds no clip.
[[nodiscard]] std::expected<anim::AnimClip, Error> loadAnimResource(const io::Wad& wad, std::string_view name);

/// The dynamic clips of a level, by name, each loaded once.
class DynamicClips {
  public:
    /// Loads one clip by name (loadAnimResource() over the disc in a run, synthetic clips in the tests).
    using Loader = std::function<std::expected<anim::AnimClip, Error>(std::string_view name)>;

    /// Clips loaded with `load` (empty: none ever loads).
    explicit DynamicClips(Loader load) : m_load(std::move(load)) {}

    /// The clip named `name`, loaded on first use; null for an empty name or one that failed to load (a failure is
    /// kept, so it is tried once). The clip stays at the same address while this lives.
    [[nodiscard]] const anim::AnimClip* find(std::string_view name);
    /// Names asked for whose load failed.
    [[nodiscard]] std::size_t failures() const;

  private:
    Loader m_load;
    std::map<std::string, std::unique_ptr<anim::AnimClip>, std::less<>> m_clips; // null for a failed load
};

} // namespace coney::characters
