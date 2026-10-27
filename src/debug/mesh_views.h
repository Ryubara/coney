// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "debug/debug_lines.h"
#include "graphics/render_device.h"

// The Debug draw page's mesh views (docs/guides/debug-menu.md#mesh-views): a model wireframe over the shaded scene,
// a wireframe-only view, flat views coloured by material, by draw call or by level of detail, and the normals and bone
// axes of the Entity inspector's selection. Here are the switches' state and the rules that decide, for each model the
// scene draws, which passes it gets and in which colours: platform-neutral, so the tests check them without a window.
// The platform's pass (src/platform/mesh_views.h) draws what the plan says. Coney's own debug tooling: the original has
// nothing like it, and with every view off the scene is drawn exactly as without them.

namespace coney::debug {

/// Which models a mesh view applies to.
enum class MeshScope : std::uint8_t {
    Off,        ///< None.
    Selection,  ///< The Entity inspector's selection only.
    Humans,     ///< Every human, and the selection.
    Everything, ///< Every model drawn: scenery, humans and objects.
};

/// How the models a view applies to are drawn.
enum class MeshShading : std::uint8_t {
    Shaded,    ///< As the game draws them (the view is off).
    Wireframe, ///< Their triangles' edges alone, no fill.
    Material,  ///< Flat, unlit, one colour per material.
    DrawCall,  ///< Flat, unlit, one colour per draw call.
    Lod,       ///< Flat, unlit, one colour per level of detail and distance fade.
};

/// What kind of model the scene draws.
enum class MeshKind : std::uint8_t {
    Scenery, ///< A world sector, the skyline.
    Human,   ///< A human's body.
    Object,  ///< A world object, a door, a car, a carried prop.
};

/// The mesh views' switches, part of the Debug draw options (DebugDrawOptions::mesh).
struct MeshViewOptions {
    MeshScope wireframe = MeshScope::Off;        ///< The wireframe over the shaded models.
    MeshShading view = MeshShading::Shaded;      ///< The view the models of viewScope get.
    MeshScope viewScope = MeshScope::Everything; ///< Which models the view applies to.
    bool normals = false;                        ///< The selection's vertex normals, as lines.
    bool boneAxes = false;                       ///< The selection's bones, as three axes each.
};

/// Whether any mesh view is on. Off, the scene draws every model as the game does and the platform's pass is left out.
[[nodiscard]] bool meshViewsOn(const MeshViewOptions& options);

/// Whether a view draws passes of its own over the models (the wireframe or a mesh view other than Shaded); the normals
/// and bone axes alone are lines of the Debug draw page and need none.
[[nodiscard]] bool meshPassesOn(const MeshViewOptions& options);

/// Whether a model of `kind` (`selected`: it is the Entity inspector's selection) is in `scope`.
[[nodiscard]] bool inMeshScope(MeshScope scope, MeshKind kind, bool selected);

/// The passes one model is drawn with.
struct MeshDrawPlan {
    bool shaded = true; ///< The game's own draw (with its second layers: blood, dual).
    bool flat = false;  ///< A flat, unlit fill coloured by `colouring`, in place of the shaded one.
    MeshShading colouring = MeshShading::Shaded;
    bool wire = false; ///< Its triangles' edges in `wireColour`, over whatever is drawn.
    graphics::Rgba wireColour{};

    friend bool operator==(const MeshDrawPlan&, const MeshDrawPlan&) = default;
};

/// The passes a model of `kind` gets under `options` (`selected`: it is the inspector's selection). With every view off
/// it is the game's draw alone (MeshDrawPlan{}). A model the view applies to loses its shaded draw: flat for the colour
/// views, nothing but the wire for Wireframe; the wireframe switch adds the wire over either.
[[nodiscard]] MeshDrawPlan meshPlanFor(const MeshViewOptions& options, MeshKind kind, bool selected);

/// The wireframe's colour for a model of `kind`: the selection white, humans green, objects orange, scenery grey.
[[nodiscard]] graphics::Rgba meshWireColour(MeshKind kind, bool selected);

/// One of the flat views' palette of twelve distinct opaque colours, by `key` (any number; the same key, the same
/// colour).
[[nodiscard]] graphics::Rgba meshPaletteColour(std::uint32_t key);

/// The key the Material view colours a material by: its texture's name and its index in its model, so two models with
/// the same textures look alike and every material of one model differs.
[[nodiscard]] std::uint32_t meshMaterialKey(std::string_view texture, std::size_t index);

/// The Lod view's colour of a model drawn at `fade` (1 opaque, less while it fades out with distance): green at full
/// detail, yellow fading. Every model the game draws has one level of detail (docs/research/graphics.md#lod), so only
/// the distance fade changes it.
[[nodiscard]] graphics::Rgba meshLodColour(float fade);

/// The placement of a character's space (game axes, the feet at the origin, facing +y) in the scene, in game axes:
/// leaned by `lean` radians about the forward axis at the feet, turned by `heading` radians about z (0 faces +y,
/// anticlockwise from above) and moved to `feet`; the same placement the play mode draws a human with.
[[nodiscard]] anim::Mat34 characterPlacement(anim::Vec3 feet, float heading, float lean);

/// The colour of the normals and of a bone's three axes (x red, y green, z blue).
inline constexpr graphics::Rgba kNormalColour{80, 160, 255, 255};
inline constexpr std::array<graphics::Rgba, 3> kBoneAxisColours{
    graphics::Rgba{255, 60, 60, 255}, graphics::Rgba{60, 255, 60, 255}, graphics::Rgba{60, 120, 255, 255}};

/// How long a normal is drawn, and a bone's axes, in metres; and the most normals drawn for one model.
inline constexpr float kNormalLength = 0.05F;
inline constexpr float kBoneAxisLength = 0.08F;
inline constexpr std::size_t kMaxNormals = 20000;

/// Adds a line along each vertex's normal: from `positions[i]` (the posed vertex) `length` along `normals[i]`, both in
/// the character's space and placed by `placement`; at most kMaxNormals, the first ones. Returns how many it added.
std::size_t addNormals(DebugLines& lines, std::span<const anim::Vec3> positions, std::span<const anim::Vec3> normals,
                       const anim::Mat34& placement, float length = kNormalLength);

/// Adds three axes at each bone of `bones` (bone transforms in the character's space, as anim::boneTransforms() gives
/// them), placed by `placement`, and a line from each bone to its parent (`parents[i]`, negative for none). Entry 0,
/// the root's motion, is left out. Returns how many bones it drew.
std::size_t addBoneAxes(DebugLines& lines, std::span<const anim::Mat34> bones, std::span<const int> parents,
                        const anim::Mat34& placement, float length = kBoneAxisLength);

/// Which entity `--mesh-view` selects in the Entity inspector.
enum class MeshViewSelect : std::uint8_t {
    None,    ///< None: the selection is left as it is.
    Nearest, ///< `select=nearest`: the human nearest the player (the player himself when there is no other).
    Player,  ///< `select=player`: player 1.
};

/// What `--mesh-view` asks for (docs/guides/running-options.md): the views from the start of a run, the inspector's
/// selection, and for how many frames they stay on.
struct MeshViewSpec {
    MeshViewOptions options;
    MeshViewSelect select = MeshViewSelect::None; ///< What to select once the play mode lists its entities.
    int frames = 0; ///< `frames=N`: switch every view off after N frames; 0 keeps them on.
};

/// Parses `--mesh-view`'s value: items separated by commas, `wire=selection|humans|all`, `view=wire|material|draw|lod`,
/// `scope=selection|humans|all`, `normals`, `bones`, `select=nearest|player` and `frames=N`. Fails with
/// ErrorCode::InvalidArgument naming the item it does not know.
[[nodiscard]] std::expected<MeshViewSpec, Error> parseMeshViewSpec(std::string_view text);

/// The kinds the scene counters split the frame's draws into, in the order they are shown.
inline constexpr std::array<std::string_view, 5> kSceneCounterKinds{"scenery", "humans", "objects", "effects", "other"};

/// One counter's value by name, for sceneCounterLine().
struct NamedCount {
    std::string name;
    double value = 0.0;
};

/// The Performance page's Scene line from the profiler's counters: for each kind of kSceneCounterKinds, its
/// `scene.<kind>.triangles`, `.vertices` and `.draws` a frame, "scenery 12.3k tris 20.1k verts 310 draws; humans ...";
/// "-" when no counter of them is there.
[[nodiscard]] std::string sceneCounterLine(std::span<const NamedCount> counters);

/// The Performance page's line for one kind: its `scene.<kind>.triangles`, `.vertices` and `.draws` a frame, "12.3k
/// tris 20.1k verts 310 draws"; "-" when none of its counters is there.
[[nodiscard]] std::string sceneKindLine(std::span<const NamedCount> counters, std::string_view kind);

} // namespace coney::debug
