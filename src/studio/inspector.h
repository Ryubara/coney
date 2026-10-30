// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "debug/menu_model.h"
#include "studio/level_document.h"

// The Inspector's contents for the selection, as a page of the debug menu's model
// (docs/guides/debug-menu.md#one-model): the same item kinds and the same layout as the merged entity panel (#1053), a
// group per subject (Identity, Transform, ...) each a submenu, so the developer overlay's renderer draws it and the
// panel can later take the live entity panel's groups in Play. In the prototype every value is a watch: editing waits
// on undo and the edit commands (docs/guides/studio/level-view.md#undo). Coney's own; the original has no editor.

namespace coney::studio {

/// The Inspector's title, also its page's.
inline constexpr std::string_view kInspectorTitle = "Inspector";

/// The Inspector page for `entity` of `level`, or with nothing selected the level's own settings (the sky, sun,
/// ambient light, fog, occlusion and shadows: docs/guides/studio/editor-experience.md#hierarchy-tree). An unknown id
/// shows the level, as no selection does. The values are read when the page is made: a changed level makes a new page.
[[nodiscard]] std::shared_ptr<debug::MenuPage> inspectorPage(const LevelDocument& level,
                                                             std::optional<EntityId> entity);

} // namespace coney::studio
