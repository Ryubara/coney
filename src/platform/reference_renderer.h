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

/// What `coney --render-references` renders (docs/guides/building.md#reference-images).
struct ReferenceRenderSettings {
    std::string outDir;            ///< Where the PNGs go, in a folder per list (characters::referenceFolder()); made
                                   ///< if missing.
    bool characters = true;        ///< Whether to render the Character List's records.
    bool objects = true;           ///< Whether to render the Object List's records.
    bool cars = true;              ///< Whether to render the six car types.
    bool radar = true;             ///< Whether to render the radar icons.
    bool particles = true;         ///< Whether to render the particle effects whose sprite is traced.
    std::vector<std::string> only; ///< Names or name hashes (`0x` and hex) to render: model, object type, car type
                                   ///< or particle type names, or `icon-<n>` for a radar icon; empty renders all.
    std::string namesFile;         ///< A text file of model and object type names, one per line; a record whose
                                   ///< hash matches one is filed under its name. Empty: none.
    int size = 256;                ///< The model images' width and height in pixels.
};

/// What a batch did: counts only.
struct ReferenceRenderReport {
    std::size_t characters = 0; ///< Character images written.
    std::size_t objects = 0;    ///< Object images written.
    std::size_t cars = 0;       ///< Car images written.
    std::size_t radar = 0;      ///< Radar icon images written.
    std::size_t particles = 0;  ///< Particle effect images written.
    std::size_t noModel = 0;    ///< Object records whose model Coney cannot load as an object: left without an image.
    std::size_t failed = 0;     ///< Records whose image could not be made otherwise.
};

/// The supersampling factor: each model image is drawn at this many times its size in each direction and averaged
/// down (characters::downsampleRgba()), which smooths the edges the same way on every run.
inline constexpr int kReferenceSupersample = 4;

/// Renders a reference image of every record of the lists `settings` selects (or of those `settings.only` names) as
/// RGBA PNGs on a transparent background, named by characters::referenceFileName() (graphics::radarIconFileName()
/// for a radar icon) in the list's folder (characters::referenceFolder()). The models are `size` × `size`, lit by
/// fixed lights (CharacterLights) and seen by the fixed three-quarter camera framed on them
/// (characters::frameReference()):
///
/// - a character as the character viewer's first frame shows it: its default clip at time 0 (the bind pose,
///   characters::bindPoseVertices(), for a character without clips), textured with its dictionary's texture;
/// - an object's model as the level file's models are read (readPreinstanceObjectChunk()), turned into the reference
///   pose's axes (characters::objectReferenceTransform()), its material given its dictionary's first texture;
/// - a car type (world_objects::kCarTypeNames) as its Object List record's model, read the same way: the first
///   world_objects::kCarParts atomics, the undamaged car, each given the dictionary's first texture. The models
///   already stand in the pose's axes (z up, the front towards +y).
///
/// The sprites are cut from their sheet's texture (graphics::rectTexels()) and written without drawing: a radar icon
/// (graphics::radarIconIds()) at its own size, at most graphics::kReferenceIconLimit a side; a particle effect
/// (graphics::particleSprites()) scaled to fit that square (graphics::fitWithin(), graphics::resampleRgba()).
///
/// Draws the models offscreen into a librw camera texture, so the window can stay hidden, and needs the OpenGL
/// backend: librw's NULL device draws nothing. Deterministic: no clock, no randomness, fixed sampling, so a re-render
/// gives byte-identical files on the same machine and driver. `print` gets one line per record (counts only). Fails
/// with ErrorCode::PlatformFailure on the NULL backend, ErrorCode::NotFound when `only` names no record, and when a
/// list or the output folder fails; a record that fails is counted and reported, and the batch goes on.
[[nodiscard]] std::expected<ReferenceRenderReport, Error>
renderReferences(RenderEngine& engine, const io::Wad& wad, const ReferenceRenderSettings& settings,
                 const std::function<void(std::string_view)>& print);

} // namespace coney::platform
