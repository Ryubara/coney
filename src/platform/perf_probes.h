// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace coney::platform {

/// The platform's side of Coney's profiler (core/perf.h, docs/guides/performance.md): the real clock its zones read,
/// and the counters only the platform can see. Debug tooling: nothing here is read by the game.

/// Installs the steady clock as the profiler's clock (perf::setClock). Reading a real clock is allowed here, as in
/// the frame pacer, because the profiler only shows what it measures and never feeds a step.
void installPerfClock();

/// What the counting operator new has seen since the process started, on every thread.
struct AllocationTotals {
    std::uint64_t allocations = 0; ///< Calls of any operator new.
    std::uint64_t bytes = 0;       ///< Bytes they asked for.
};
/// The allocation totals so far.
[[nodiscard]] AllocationTotals allocationTotals();

/// Registers the allocation counters (`alloc.count`, `alloc.bytes`) with the profiler.
void addAllocationCounters();

/// Wraps the OpenGL calls librw and Coney draw and upload with (through librw's GL loader) in counting ones, and
/// registers their counters with the profiler: `gl.draws` (draw calls), `gl.triangles`, `gl.uploads` and
/// `gl.upload_bytes` (buffer data and sub-data calls), `gl.programs` (program switches) and `gl.textures` (texture
/// binds), and the scene counters `scene.<kind>.draws`, `.triangles` and `.vertices` by DrawKind. Needs a current
/// OpenGL context with librw's loader run (RenderEngine::start); idempotent.
void installGlCounters();

/// Puts the original OpenGL functions back (before the context goes).
void removeGlCounters();

/// What a draw call draws, for the scene counters: the frame's draws split by the kind of thing drawn.
enum class DrawKind : std::uint8_t {
    Scenery, ///< The streamed world and the background.
    Humans,  ///< The humans' bodies.
    Objects, ///< World objects, doors, cars, glass, carried props.
    Effects, ///< Water, particles, glints, blob shadows, health rings, the throw arc.
    Other,   ///< Everything else: the 2D layers (HUD, menus, movies) and the scene debug lines. The debug menu and the
             ///< developer overlay are drawn outside the counted wrappers and are not counted at all.
};
/// How many kinds there are, and their names in the counters, by DrawKind (`scene.<name>.draws`).
inline constexpr std::size_t kDrawKinds = 5;
inline constexpr std::array<std::string_view, kDrawKinds> kDrawKindNames{"scenery", "humans", "objects", "effects",
                                                                         "other"};

/// Counts the draw calls made in its lifetime as `kind` (the kind before it comes back after): render code marks
/// what it draws with one, and scopes nest. Outside every scope draws count as Other.
class DrawKindScope {
  public:
    explicit DrawKindScope(DrawKind kind);
    ~DrawKindScope();
    DrawKindScope(const DrawKindScope&) = delete;
    DrawKindScope& operator=(const DrawKindScope&) = delete;
    DrawKindScope(DrawKindScope&&) = delete;
    DrawKindScope& operator=(DrawKindScope&&) = delete;

  private:
    DrawKind m_previous;
};

/// The kind draws count as now: the innermost DrawKindScope's, Other outside every scope. The mesh views' hook takes
/// the kind of each model from it.
[[nodiscard]] DrawKind currentDrawKind();

/// One kind's running totals.
struct DrawTotals {
    std::uint64_t draws = 0;
    std::uint64_t triangles = 0;
    std::uint64_t vertices = 0; ///< The vertices the draws sent: an indexed draw's index count, else its vertex count.
};
/// The running totals of `kind` so far.
[[nodiscard]] DrawTotals drawTotals(DrawKind kind);
/// Adds one draw call of `triangles` triangles and `vertices` vertices to the current kind (what the OpenGL counters
/// do for each draw; the tests call it directly).
void recordDraw(std::uint64_t triangles, std::uint64_t vertices);

/// The OpenGL renderer and version strings ("NVIDIA GeForce ..., 4.6.0 ..."), or "none" without a context, for a
/// bench report to say what it ran on.
[[nodiscard]] std::string glRendererName();

} // namespace coney::platform
