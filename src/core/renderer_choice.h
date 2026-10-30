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

/// The renderer a run starts with: `option` (`--renderer`) when given; else, outside test mode, `saved` (the
/// overrides file's `Display/Renderer`); else GL3. Test mode never takes the saved one, so a test run draws the same
/// whatever a player saved.
[[nodiscard]] RendererKind chooseRenderer(std::optional<RendererKind> option, RendererKind saved, bool testMode);

/// The live value of the `Display/Renderer` setting, which the debug menus' Display page edits and the overrides file
/// saves (0 GL3, the default; 1 the GPU renderer). The running renderer never changes with it: it is read at start.
[[nodiscard]] int& rendererSetting();

} // namespace coney
