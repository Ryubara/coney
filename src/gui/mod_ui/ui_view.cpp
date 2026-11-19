// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/mod_ui/ui_view.h"

#include <utility>

namespace coney::gui {

UiView::UiView(FontLookup fonts, SheetLookup sheets) : m_fonts(std::move(fonts)), m_sheets(std::move(sheets)) {}
void UiView::list(const UiModel&, const GuiFrame&) {}
void UiView::queue(graphics::OverlayPass&) {}
const graphics::SpriteBatch* UiView::textBatch(int) const { return nullptr; }
const graphics::SpriteBatch* UiView::labelBatch(int) const { return nullptr; }
const graphics::SpriteBatch* UiView::spriteBatch(std::string_view) const { return nullptr; }

} // namespace coney::gui
