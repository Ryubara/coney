// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "debug/debug_lines.h"
#include "debug/entity_inspect.h"
#include "graphics/light_manager.h"
#include "graphics/lighting_frame.h"

// The lighting lab (docs/guides/realtime-lighting.md#lab): what the Lighting page shows of a frame's lighting, built
// here from the lighting frame and the light manager, platform-neutral and testable. Coney's own debug tooling: the
// original has nothing like it, so nothing here carries an @orig tag.

namespace coney::debug {

/// What the Lighting page's *Debug draw* group switches: lines a play mode draws into the scene from the lighting
/// frame. All off by default.
struct LightingDrawOptions {
    bool pointLights = false; ///< Point and spot lights as spheres of their radius, in their colour.
    bool coronaOnly = false;  ///< Lights of radius 0 (a corona alone) as crosses.
    bool directional = false; ///< Directional lights as arrows, from the light view's place.
    bool cullReach = false;   ///< The cull's reach round the light view: three quarters of the draw distance.
    bool selection = false;   ///< The Selection group's atomic: its sphere, and a line to each light it chose.
    std::int32_t marked = -1; ///< The light record marked in the scene (chosen in the Lights group); -1 for none.

    friend bool operator==(const LightingDrawOptions&, const LightingDrawOptions&) = default;
};

/// The *Counts* group's figures for one frame.
struct LightingCounts {
    std::size_t records = 0;                     ///< Light records in use, of graphics::kLightCapacity.
    std::size_t points = 0;                      ///< Visible point and spot lights (the objects' list).
    std::size_t worldPoints = 0;                 ///< Those that light the world.
    std::size_t coronas = 0;                     ///< Corona sprites made.
    std::size_t atomics = 0;                     ///< Atomics lit: calls of the selection.
    std::array<std::size_t, 4> humansByLights{}; ///< Humans lit by 3, 4, 5 and 6 lights.
    std::size_t humansOther = 0;                 ///< Humans lit by fewer than 3 or more than 6.
    std::size_t full = 0;                        ///< Selections whose list reached its cap.
};

/// The counts of `frame` with `manager`'s records in use.
[[nodiscard]] LightingCounts lightingCounts(const graphics::LightingFrame& frame,
                                            const graphics::LightManager& manager);

/// A light's kind as the lab names it: "ambient", "directional", "point", "spot", or "corona only" for a point light
/// of radius 0.
[[nodiscard]] std::string_view lightKindName(graphics::LightType type, float radius);
/// What a light lights: "objects", "world", "both" or "nothing".
[[nodiscard]] std::string_view lightsWhatName(std::uint16_t lights);
/// Where a light came from: "built in (A)", "built in (B)", "built in (glow)", "Coney's stand-in", or "script or light
/// task" for an untagged light (the script and function that made it are not tracked yet).
[[nodiscard]] std::string lightOriginName(graphics::LightOrigin origin, std::uint16_t record);
/// A caller's name: "sector", "background", "human", "object".
[[nodiscard]] std::string_view selectionCallerName(graphics::SelectionCaller caller);
/// The return address the original's trace records for a caller (docs/research/lighting.md#select).
[[nodiscard]] std::uint32_t selectionCallerAddress(graphics::SelectionCaller caller);
/// RenderWare's light type number, as the trace reads it from an RpLight (`+0x01`): 0x80 point, 0x81 spot, 1
/// directional, 2 ambient.
[[nodiscard]] std::uint32_t renderWareLightType(graphics::LightType type);

/// One line of the *Lights* group for record `index`: its kind, what it lights, on or off, its colours, radius,
/// corona, flicker and source.
[[nodiscard]] std::string lightLine(const graphics::LightManager& manager, std::uint16_t index);
/// The *Selection* group's line for the call of `selection`: its caller, where its sphere is (game axes), the lights it
/// chose of its cap, and the distance the cap was read from.
[[nodiscard]] std::string selectionSummary(const graphics::FrameSelection& selection);
/// The *Selection* group's lines for `selection` of `frame`: one per light chosen, in order, with where it came from
/// (the copied list, the glow, the point lists) and a point light's score; then the point lights of the list the call
/// read that overlapped its sphere and were dropped.
[[nodiscard]] std::vector<std::string> selectionLines(const graphics::FrameSelection& selection,
                                                      const graphics::LightingFrame& frame);

/// The selection of `frame` drawn for an atomic nearest `at` (game axes) among `caller`'s: the one the Selection group
/// shows for a human or an object. Coney's choice, as the design's comparison matches atomics: by caller and by the
/// sphere's centre. Null when the frame has none of `caller`'s.
[[nodiscard]] const graphics::FrameSelection* selectionNear(const graphics::LightingFrame& frame,
                                                            graphics::SelectionCaller caller, anim::Vec3 at);

/// The Selection group's atomic in `frame`: the Entity inspector's selection `inspected` among `entities` (a human's
/// or an object's nearest selection), or the player's, at `playerFeet`, when nothing is inspected. Null for none.
[[nodiscard]] const graphics::FrameSelection* labSelection(const graphics::LightingFrame& frame,
                                                           std::span<const EntityRef> entities, EntityId inspected,
                                                           anim::Vec3 playerFeet);

/// The *Debug draw* group's lines for `frame` (game axes), as `options` switches them; `selected` is the Selection
/// group's atomic (null: none).
[[nodiscard]] DebugLines lightingLines(const graphics::LightingFrame& frame, const LightingDrawOptions& options,
                                       const graphics::FrameSelection* selected);

/// The column names of a dump's selections file: the caller's return address, the selection's inputs, the count, then
/// each of the 8 slots' type, radius, colour and position: the columns of the original's trace
/// (docs/research/lighting.md#select).
[[nodiscard]] std::vector<std::string> selectionColumns();
/// The selections of `frame` as CSV in selectionColumns(): a header line, then one line per call, numbers only.
[[nodiscard]] std::string selectionsCsv(const graphics::LightingFrame& frame);
/// The lights of `frame` as CSV: a header line, then one line per light of the frame's lists with the lists it is in,
/// numbers only.
[[nodiscard]] std::string frameLightsCsv(const graphics::LightingFrame& frame);

/// *Dump this frame*: writes `frame`'s lights and selections into `folder` as `lighting-<n>-lights.csv` and
/// `lighting-<n>-selections.csv` (n from 1, the first number whose files do not exist yet). Returns the selections
/// file's path.
std::expected<std::filesystem::path, Error> writeLightingDump(const graphics::LightingFrame& frame,
                                                              const std::filesystem::path& folder);

/// A debug edit of one light (the *Lights* group): what to change, queued and applied between two steps.
struct LightEdit {
    std::uint16_t record = 0;
    std::optional<bool> on = std::nullopt;
    std::optional<float> radius = std::nullopt;
    /// The base colour's red, green and blue, each its own edit, so edits queued in one frame do not undo each other.
    std::array<std::optional<float>, 3> colour{};
};

/// The Lighting page's edits, queued while the page acts (from either front end, at any time in a frame) and applied
/// between two steps, as the Entity inspector's are.
class LightEditQueue {
  public:
    /// Queues `edit`.
    void push(const LightEdit& edit) { m_pending.push_back(edit); }
    /// Edits waiting.
    [[nodiscard]] std::size_t pending() const { return m_pending.size(); }
    /// Applies the queued edits to `manager` (null: drops them) in order and empties the queue; returns how many
    /// changed a light that is still in use.
    std::size_t applyPending(graphics::LightManager* manager);

  private:
    std::vector<LightEdit> m_pending;
};

} // namespace coney::debug
