// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"

// The sandbox's layout files: Coney's own test worlds of primitives (boxes, ramps, stairs, cylinders, spheres,
// capsules), with spawn points, camera viewpoints and light settings, authored as text so an experiment needs no
// recompiling. The original game has no test level (docs/research/debug.md); this is Coney's own feature, not a
// reimplementation. Format and how to add an experiment: docs/guides/sandbox.md.
//
// Everything is in the game's axes and units: metres, z up (docs/research/collision.md, docs/research/characters.md),
// so a distance in the sandbox means the same as in a level.

namespace coney::sandbox {

/// A colour as three channels from 0 to 1.
struct Colour {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;

    friend bool operator==(const Colour&, const Colour&) = default;
};

/// The kinds of primitive a layout can place.
enum class Shape : std::uint8_t {
    Box,      ///< A cuboid: `size` is width (local x), depth (local y) and height.
    Ramp,     ///< A wedge rising along local +y from 0 to `size.z`: `size` is width, run and height.
    Stairs,   ///< Solid steps rising along local +y: `steps` steps of `rise` and `run`, `size.x` wide.
    Cylinder, ///< An upright cylinder of `radius` and `size.z` high, with `segments` sides.
    Sphere,   ///< A sphere of `radius` resting on its base point.
    Capsule,  ///< An upright capsule of `radius`, `size.z` high in all, resting on its base point.
};

/// The name a layout uses for a shape (`box`, `ramp`, ...).
[[nodiscard]] std::string_view shapeName(Shape shape);

/// The collision a primitive's triangles carry (docs/research/collision.md#triangles). The meanings of the type bits
/// are not researched yet, so a layout can set them raw to experiment.
struct SurfaceTags {
    std::uint16_t flags = 0;   ///< Triangle flag bits (bit 1 two-sided, bits 2-10 type bits).
    std::uint8_t material = 5; ///< A `MATERIAL_*` id; 5 is `MATERIAL_CONCRETE`.
    std::uint8_t area = 1;     ///< The area byte; not 0, so dropToMarkedGround() stops on it.
    bool solid = true;         ///< false: drawn but left out of the collision mesh (a marker you can walk through).

    friend bool operator==(const SurfaceTags&, const SurfaceTags&) = default;
};

/// One primitive of a layout, already expanded from a `repeat`.
struct Primitive {
    Shape shape = Shape::Box;
    std::size_t line = 0;               ///< The layout line it came from, for messages.
    anim::Vec3 base;                    ///< The middle of its footprint, at its bottom.
    float yawDegrees = 0.0F;            ///< Turn about +z, counter-clockwise seen from above (from +x towards +y).
    anim::Vec3 size;                    ///< Width (local x), depth (local y), height (z); see Shape for each kind.
    float radius = 0.0F;                ///< Cylinder, sphere and capsule.
    std::uint32_t segments = 0;         ///< Cylinder sides, sphere and capsule segments round the axis.
    std::uint32_t steps = 0;            ///< Stairs.
    std::optional<std::size_t> texture; ///< Index into SandboxLayout::textures; none draws it untextured (white).
    float uvScale = 1.0F;               ///< Texture tiles per kTileMetres; 2 makes the pattern half as big.
    Colour tint{1.0F, 1.0F, 1.0F};      ///< Multiplies the lighting, to tell objects apart.
    SurfaceTags surface;
};

/// A texture a layout declares: a short name its primitives use and a PNG file beside the layout.
struct TextureRef {
    std::string name;
    std::string file; ///< Relative to the layout's folder.
};

/// A named place to start: the player's feet and heading.
struct SpawnPoint {
    std::string name;
    anim::Vec3 position;
    float headingDegrees = 0.0F; ///< As a level's player start: 0 faces +y.
};

/// A passive human to fight (human::TargetHuman): Coney's own test target, only ever placed by a sandbox layout.
struct TargetPoint {
    std::string name;
    anim::Vec3 position;         ///< Its feet.
    float headingDegrees = 0.0F; ///< 0 faces +y.
    int health = 600;            ///< As the street civilian the research fought (600).
};

/// The most targets one layout may place.
inline constexpr std::size_t kMaxTargets = 16;

/// An AI human that fights the player (ai::AiHumans): a sparring Warrior's brain, placed only by a sandbox layout; it
/// takes the player on when he comes within its melee range.
struct FighterPoint {
    std::string name;
    anim::Vec3 position;         ///< Its feet.
    float headingDegrees = 0.0F; ///< 0 faces +y.
};

/// The most fighters one layout may place.
inline constexpr std::size_t kMaxFighters = 8;

/// A named camera viewpoint for the free camera: the camera's position and where it looks.
struct Viewpoint {
    std::string name;
    anim::Vec3 position;
    float yawDegrees = 0.0F;   ///< Heading, 0 looking along +y, counter-clockwise seen from above.
    float pitchDegrees = 0.0F; ///< Positive looks up.
};

/// The light and atmosphere: Coney's choices, since the game's LightManager is not researched.
struct Lighting {
    Colour sky{0.62F, 0.72F, 0.84F};               ///< The clear colour, and the fog's.
    anim::Vec3 sunDirection{-0.5F, 0.45F, -0.74F}; ///< The direction the sunlight travels (normalised when used).
    Colour sun{0.62F, 0.58F, 0.50F};               ///< The sun's colour and strength.
    Colour skyAmbient{0.36F, 0.40F, 0.48F};        ///< Light from above, on faces pointing up.
    Colour groundAmbient{0.20F, 0.19F, 0.18F};     ///< Light bounced from the ground, on faces pointing down.
    float fogStart = 80.0F;                        ///< Metres from the camera where the fog begins.
    float fogEnd = 220.0F;                         ///< Where it is complete; also the far clip.
    bool occlusion = true;                         ///< Bake ambient occlusion into the vertex colours.
    float occlusionRadius = 1.5F;                  ///< How far the occlusion rays reach, metres.
    float occlusionStrength = 0.65F;               ///< 0 none, 1 fully dark where every ray is blocked.
    bool shadows = true;                           ///< Bake the sun's shadows into the vertex colours.
};

/// A whole layout: what parseSandboxLayout() reads.
struct SandboxLayout {
    std::string title; ///< From the `title` line; empty without one.
    std::vector<TextureRef> textures;
    std::vector<Primitive> primitives;
    std::vector<SpawnPoint> spawns;     ///< At least one: a layout without a `spawn` line gets one at the origin.
    std::vector<TargetPoint> targets;   ///< The `target` lines: humans to fight (play mode only).
    std::vector<FighterPoint> fighters; ///< The `fighter` lines: AI humans that fight back (play mode only).
    std::vector<Viewpoint> views;       ///< At least one: without a `view` line, one behind the first spawn.
    Lighting lighting;
    float tessellation = 1.0F; ///< Largest edge, metres, of the drawn faces (finer faces carry finer lighting).
};

/// One texture tile covers this many metres: Kenney's prototype textures are made for 1 m (their labelled ones say
/// "1x1 meter"), so the bright lines fall every 50 cm and the faint ones every 12.5 cm.
inline constexpr float kTileMetres = 1.0F;

/// The most primitives one layout may expand to (after `repeat`): keeps a mistyped repeat from eating memory.
inline constexpr std::size_t kMaxPrimitives = 4096;

/// Parses a layout's text (docs/guides/sandbox.md#the-layout-format). One statement per line, a keyword then
/// `key=value` arguments; `#` starts a comment. Fails with ErrorCode::Invalid and a message that starts with
/// `line N:` for an unknown keyword or argument, a missing, repeated or out-of-range value, an unknown texture name,
/// or too many primitives.
[[nodiscard]] std::expected<SandboxLayout, Error> parseSandboxLayout(std::string_view text);

/// Reads and parses the layout file at `path`. Fails with ErrorCode::NotFound or Io when it cannot be read, and as
/// parseSandboxLayout() does, with the path in front of the message.
[[nodiscard]] std::expected<SandboxLayout, Error> loadSandboxLayout(const std::filesystem::path& path);

/// The file extension of a layout.
inline constexpr std::string_view kLayoutExtension = ".layout";
/// The layout `--sandbox` loads without a name.
inline constexpr std::string_view kDefaultLayout = "default";

/// The names of the layouts in `folder` (each `<name>.layout`), sorted; empty when the folder is missing. For the
/// command line and the debug menu.
[[nodiscard]] std::vector<std::string> listSandboxLayouts(const std::filesystem::path& folder);

/// Where layout `nameOrPath` is: a path to an existing file as given, otherwise `<folder>/<name>.layout`. Fails with
/// ErrorCode::NotFound listing the layouts in `folder` when neither exists.
[[nodiscard]] std::expected<std::filesystem::path, Error> findSandboxLayout(const std::filesystem::path& folder,
                                                                            std::string_view nameOrPath);

} // namespace coney::sandbox
