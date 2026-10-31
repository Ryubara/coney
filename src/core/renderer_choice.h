// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Which renderer a run draws with, chosen at start (docs/engine/renderer.md#migration): librw's GL3 device, the
// default, or Coney's renderer on SDL3's GPU API. The choosing lives here, free of SDL and librw, so it can be tested;
// src/platform/game_run.cpp starts the renderer it names.

#include <cstdint>
#include <optional>
#include <string_view>

namespace coney {

/// A renderer a windowed run can draw with. A headless run (`--headless`, `--load`) has neither: it runs librw's
/// NULL device whatever this says.
enum class RendererKind : std::uint8_t {
    Gl3, ///< librw's GL3 device: a window with an OpenGL 3.3 context, the game's whole picture. The default.
    Gpu, ///< SDL3's GPU API (Vulkan, Direct3D 12, Metal) with librw opened for data only; clears and presents so far.
};

/// The renderer `name` names, as `--renderer` takes it: `gl3` or `gpu`; nothing for anything else.
[[nodiscard]] std::optional<RendererKind> parseRendererName(std::string_view name);

/// The name `--renderer` takes for `kind`: `gl3` or `gpu`.
[[nodiscard]] std::string_view rendererName(RendererKind kind);

/// The renderer the `Display/Renderer` setting's value names: 1 is the GPU renderer, anything else GL3.
[[nodiscard]] RendererKind rendererOfSetting(int value);

/// Whether the GPU renderer draws its own menus: the 2D layers (#1203) and the developer overlay (#1204). Until it
/// does, a saved `Display/Renderer` of 1 is refused (chooseRenderer()), so a windowed start never comes up blank with
/// the menus that set it back unseen (docs/engine/gpu-renderer.md#way-back). Turn it true when both land, and remove
/// the guard with it: tests/core/renderer_choice_test.cpp then stops compiling (a STATIC_REQUIRE_FALSE on it).
/// @coney-only a guard for Coney's unfinished renderer; the original has one renderer
inline constexpr bool kGpuRendererDrawsMenus = false;

/// The renderer a run starts with, and whether a saved GPU renderer was refused for it.
struct RendererChoice {
    RendererKind kind = RendererKind::Gl3;
    bool savedGpuRefused = false; ///< the saved setting named the GPU renderer, refused: the run log says so
};

/// The renderer a run starts with: `option` (`--renderer`) when given; else, unless `ignoreSaved`, `saved` (the
/// overrides file's `Display/Renderer`); else GL3. Test mode and reference renders ignore the saved one
/// (takesSavedRenderer() in core/options.h), so a test run draws the same whatever a player saved. A saved GPU
/// renderer is refused for GL3 while `gpuDrawsMenus` is false (kGpuRendererDrawsMenus); `--renderer gpu` never is.
/// @coney-only Coney's renderer choice; the original has one renderer
[[nodiscard]] RendererChoice chooseRenderer(std::optional<RendererKind> option, RendererKind saved, bool ignoreSaved,
                                            bool gpuDrawsMenus = kGpuRendererDrawsMenus);

/// The run log's warning when a saved GPU renderer is refused (RendererChoice::savedGpuRefused): what happened and
/// how a developer still starts it.
[[nodiscard]] std::string_view savedGpuRefusedWarning();

/// The live value of the `Display/Renderer` setting, which the debug menus' Display page edits and the overrides file
/// saves (0 GL3, the default; 1 the GPU renderer). The running renderer never changes with it: it is read at start.
[[nodiscard]] int& rendererSetting();

} // namespace coney
