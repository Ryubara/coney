// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

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
/// binds). Needs a current OpenGL context with librw's loader run (RenderEngine::start); idempotent.
void installGlCounters();

/// Puts the original OpenGL functions back (before the context goes).
void removeGlCounters();

/// The OpenGL renderer and version strings ("NVIDIA GeForce ..., 4.6.0 ..."), or "none" without a context, for a
/// bench report to say what it ran on.
[[nodiscard]] std::string glRendererName();

} // namespace coney::platform
