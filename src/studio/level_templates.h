// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>
#include <string_view>

// The levels a new Coney Studio project can start with (docs/guides/studio/editor-experience.md#new-level-coney): the
// *Basic* template, written here, or a copy of one of Coney's own sandbox courses (assets/sandbox). Both are sandbox
// layouts (docs/guides/sandbox.md#the-layout-format), the level format Coney plays today. Coney's own content.

namespace coney::studio {

/// The name of the *Basic* template.
inline constexpr std::string_view kBasicTemplate = "basic";

/// The templates New project offers: Basic, then the shipped courses by their layout names.
[[nodiscard]] std::span<const std::string_view> levelTemplates();

/// The *Basic* level: a 60 m ground, a sky and sun, a spawn point `start` at its middle and a camera view behind it, so
/// Run in Coney works at once. Its one texture is `../textures/dark_grid.png`, which New project copies in.
[[nodiscard]] std::string basicLevelText();

/// `layoutText` with each `texture NAME FILE` line's file put under `prefix` (`../textures/`), for a shipped layout
/// copied into a project's `levels/` folder while its PNG files go into `textures/`. Every other line, comments
/// included, is kept as it is.
[[nodiscard]] std::string rehomeTextures(std::string_view layoutText, std::string_view prefix);

} // namespace coney::studio
