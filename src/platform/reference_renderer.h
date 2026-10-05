// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "fileio/wad.h"
#include "platform/render_engine.h"

namespace coney::platform {

/// What `coney --render-references` renders (docs/guides/building.md#character-reference-images).
struct ReferenceRenderSettings {
    std::string outDir;            ///< Where the PNGs go; made if missing.
    std::vector<std::string> only; ///< Model names or name hashes (`0x` and hex) to render; empty renders all.
    std::string namesFile;         ///< A text file of model names, one per line; a record whose hash matches one
                                   ///< is filed under its name. Empty: none.
    int size = 256;                ///< The images' width and height in pixels.
};

/// What a batch did: counts only.
struct ReferenceRenderReport {
    std::size_t rendered = 0; ///< Images written.
    std::size_t failed = 0;   ///< Records whose resources did not load or whose image could not be written.
};

/// The supersampling factor: each image is drawn at this many times its size in each direction and averaged down
/// (characters::downsampleRgba()), which smooths the edges the same way on every run.
inline constexpr int kReferenceSupersample = 4;

/// Renders a reference image of every Character List record of `wad` (or of those `settings.only` names): the model
/// as the character viewer's first frame shows it (its default clip at time 0; the bind pose,
/// characters::bindPoseVertices(), for a character without clips), textured with its dictionary's texture, lit by fixed
/// lights (CharacterLights), seen by the fixed three-quarter camera framed on it (characters::frameReference()), on a
/// transparent background, as a `size` × `size` RGBA PNG named by characters::referenceFileName(). Draws offscreen
/// into a librw camera texture, so the window can stay hidden, and needs the OpenGL backend: librw's NULL device draws
/// nothing. Deterministic: no clock, no randomness, fixed sampling, so a re-render gives byte-identical files on the
/// same machine and driver. `print` gets one line per image (counts only). Fails with ErrorCode::PlatformFailure on
/// the NULL backend, ErrorCode::NotFound when `only` names no record, and when the Character List or the output
/// folder fails; a record that fails is counted and reported, and the batch goes on.
[[nodiscard]] std::expected<ReferenceRenderReport, Error>
renderCharacterReferences(RenderEngine& engine, const io::Wad& wad, const ReferenceRenderSettings& settings,
                          const std::function<void(std::string_view)>& print);

} // namespace coney::platform
