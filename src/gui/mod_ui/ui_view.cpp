// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#include "gui/mod_ui/ui_view.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <utility>

#include "gui/bar.h"
#include "gui/base_widget.h"
#include "gui/circled_text_header.h"
#include "gui/colour_table.h"
#include "gui/message_box.h"
#include "gui/profile_management_gui/pm_new_game_screens.h"
#include "gui/usage_info.h"

namespace coney::gui {

namespace {

// A list's row pitch at font scale `scale` (Coney's choice): the grid's, the profile manager's measured 0.0505 at 1.15.
float listPitch(float scale) { return 0.0505F * scale / 1.15F; }

// The key of one drawn part of a widget: its id, and a part name for widgets drawn as several.
std::string keyOf(UiWidgetId id, std::string_view part = {}) {
    return std::to_string(static_cast<std::uint32_t>(id)) + "/" + std::string(part);
}

// The widget kept under `key` in `widgets` as a `T`, made the first time and marked drawn this frame in `drawn`. A
// key's kind never changes: a widget id keeps its kind for life.
template <typename T>
T& kept(std::map<std::string, std::unique_ptr<Widget>, std::less<>>& widgets, std::set<std::string, std::less<>>& drawn,
        const std::string& key, const std::function<std::unique_ptr<T>()>& make) {
    drawn.insert(key);
    auto it = widgets.find(key);
    if (it == widgets.end()) {
        it = widgets.emplace(key, make()).first;
    }
    return static_cast<T&>(*it->second);
}

} // namespace

UiView::UiView(FontLookup fonts, SheetLookup sheets) : m_fonts(std::move(fonts)), m_sheets(std::move(sheets)) {
    m_canvas.fonts = m_fonts;
    m_canvas.textBatch = [this](int slot) { return fontBatch(m_textBatches, slot, kTextDepth); };
    m_labelCanvas.fonts = m_fonts;
    m_labelCanvas.textBatch = [this](int slot) { return fontBatch(m_labelBatches, slot, kLabelDepth); };
}

void UiView::list(const UiModel& model, const GuiFrame& frame) {
    for (auto* batches : {&m_textBatches, &m_labelBatches}) {
        for (auto& [slot, batch] : *batches) {
            batch->clear();
        }
    }
    for (auto& [sheet, batch] : m_spriteBatches) {
        batch->clear();
    }
    m_labelTexts.clear();
    m_drawn.clear();

    // The mods' layer: the shown HUD screens, then each slot's top menu screen (a covered one is not drawn).
    for (const auto& [id, slot] : model.hud()) {
        if (const UiScreen* screen = model.screen(id)) {
            listScreen(*screen, frame);
        }
    }
    for (std::size_t slot = 0; slot < kUiSlots; ++slot) {
        if (const std::optional<UiScreenId> top = model.top(slot)) {
            listScreen(*model.screen(*top), frame);
        }
    }
    // The trusted layer: the frame label of every shown screen's project, from the registered title only.
    for (std::size_t slot = 0; slot < kUiSlots; ++slot) {
        if (const std::optional<UiScreenId> top = model.top(slot)) {
            listLabel(model.projectTitle(model.screen(*top)->project()), frame);
        }
    }
    for (const auto& [id, slot] : model.hud()) {
        if (const UiScreen* screen = model.screen(id)) {
            listLabel(model.projectTitle(screen->project()), frame);
        }
    }
    // The game's widgets of parts not drawn this frame go, so a screen opened again starts afresh.
    std::erase_if(m_widgets, [&](const auto& entry) { return !m_drawn.contains(entry.first); });
}

void UiView::queue(graphics::OverlayPass& pass) {
    for (auto& [sheet, batch] : m_spriteBatches) {
        if (!batch->sprites().empty()) {
            pass.queue(*batch);
        }
    }
    for (auto* batches : {&m_textBatches, &m_labelBatches}) {
        for (auto& [slot, batch] : *batches) {
            if (!batch->sprites().empty()) {
                pass.queue(*batch);
            }
        }
    }
}

const graphics::SpriteBatch* UiView::textBatch(int slot) const {
    const auto it = m_textBatches.find(slot);
    return it == m_textBatches.end() ? nullptr : it->second.get();
}

const graphics::SpriteBatch* UiView::labelBatch(int slot) const {
    const auto it = m_labelBatches.find(slot);
    return it == m_labelBatches.end() ? nullptr : it->second.get();
}

const graphics::SpriteBatch* UiView::spriteBatch(std::string_view sheet) const {
    const auto it = m_spriteBatches.find(sheet);
    return it == m_spriteBatches.end() ? nullptr : it->second.get();
}

graphics::SpriteBatch* UiView::fontBatch(std::map<int, std::unique_ptr<graphics::SpriteBatch>>& batches, int slot,
                                         float depth) {
    if (const auto it = batches.find(slot); it != batches.end()) {
        return it->second.get();
    }
    const graphics::Font* font = m_fonts ? m_fonts(slot) : nullptr;
    if (font == nullptr) {
        return nullptr;
    }
    return batches.emplace(slot, std::make_unique<graphics::SpriteBatch>(font->sheet(), kTextCapacity, depth))
        .first->second.get();
}

graphics::SpriteBatch* UiView::sheetBatch(const std::string& sheet) {
    if (const auto it = m_spriteBatches.find(sheet); it != m_spriteBatches.end()) {
        return it->second.get();
    }
    const graphics::SpriteSheet* found = m_sheets ? m_sheets(sheet) : nullptr;
    if (found == nullptr) {
        return nullptr;
    }
    return m_spriteBatches
        .emplace(sheet, std::make_unique<graphics::SpriteBatch>(*found, kSpriteCapacity, kSpriteDepth))
        .first->second.get();
}

void UiView::listScreen(const UiScreen& screen, const GuiFrame& frame) {
    for (const std::unique_ptr<UiWidget>& w : screen.widgets()) {
        if (w->visible) {
            listWidget(screen, *w, frame);
        }
    }
    // A dialog over the widgets: the message box's choice layout, its text centred on y 0.5, its choices at kChoiceY.
    if (const UiDialog* dialog = screen.dialog()) {
        TextWidget& text =
            kept<TextWidget>(m_widgets, m_drawn, "dialog/" + std::to_string(static_cast<std::uint32_t>(screen.id())),
                             [] { return std::make_unique<TextWidget>(); });
        text.setup(TextWidgetSetup{.x = 0.5F,
                                   .y = 0.5F,
                                   .scale = 1.0F,
                                   .colour = kMenuGrey,
                                   .alignment = TextAlignment::Centre,
                                   .fontSlot = kTextFontSlot,
                                   .wrapWidth = MessageBox::kMessageWrap});
        if (text.text() != dialog->text) {
            text.setText(dialog->text);
        }
        text.update(frame);
        text.render(m_canvas);
        dialog->choices->render(m_canvas);
    }
}

void UiView::listWidget(const UiScreen& screen, const UiWidget& w, const GuiFrame& frame) {
    const UiWidgetDef& def = w.def;
    switch (def.kind) {
    case UiWidgetKind::Text:
        listText(keyOf(w.id), def, def.text, def.x, def.y, def.colour, frame);
        return;
    case UiWidgetKind::Usage: {
        UsageInfo& usage =
            kept<UsageInfo>(m_widgets, m_drawn, keyOf(w.id), [] { return std::make_unique<UsageInfo>(); });
        usage.place(def.x, def.y, def.alignment == TextAlignment::Left);
        if (usage.text() != def.text) {
            usage.setLegend(def.text);
        }
        usage.update(frame);
        usage.render(m_canvas);
        return;
    }
    case UiWidgetKind::Sprite: {
        graphics::SpriteBatch* batch = sheetBatch(def.sheet);
        BaseWidget& sprite = kept<BaseWidget>(m_widgets, m_drawn, keyOf(w.id),
                                              [&] { return std::make_unique<BaseWidget>(batch, def.rect); });
        sprite.setBatch(batch);
        sprite.setRect(def.rect);
        BaseWidgetSetup setup;
        setup.x = def.x;
        setup.y = def.y;
        setup.height = def.size * kSpriteHeightPerSize;
        setup.colour = def.colour;
        sprite.setup(setup);
        sprite.render(m_canvas);
        return;
    }
    case UiWidgetKind::Bar: {
        graphics::SpriteBatch* batch = sheetBatch(def.sheet);
        Bar& bar = kept<Bar>(m_widgets, m_drawn, keyOf(w.id), [&] { return std::make_unique<Bar>(batch, def.rect); });
        bar.setBatch(batch);
        BarSetup setup;
        setup.x = def.x;
        setup.y = def.y;
        if (def.width > 0.0F) {
            setup.width = def.width;
        }
        bar.setup(setup);
        bar.setFill(w.fill);
        bar.render(m_canvas);
        return;
    }
    case UiWidgetKind::Header: {
        CircledTextHeader& header = kept<CircledTextHeader>(m_widgets, m_drawn, keyOf(w.id),
                                                            [] { return std::make_unique<CircledTextHeader>(); });
        header.setBatch(sheetBatch(def.sheet));
        CircledTextHeaderSetup setup;
        setup.x = def.x;
        setup.y = def.y;
        header.setup(setup);
        header.setTitle(def.text, 0, "");
        header.update(frame);
        header.render(m_canvas);
        return;
    }
    case UiWidgetKind::Message: {
        // The message box's timed message, shown from the first frame it is drawn.
        const std::string key = keyOf(w.id);
        const bool fresh = !m_widgets.contains(key);
        MessageBox& box = kept<MessageBox>(m_widgets, m_drawn, key, [] { return std::make_unique<MessageBox>(); });
        if (fresh) {
            box.showTimed(def.text, def.durationMs, MessageStyle::Centre, frame.timeMs);
        }
        box.update(frame);
        box.render(m_canvas);
        return;
    }
    case UiWidgetKind::Grid:
        // The grid's own render: its items in their colours, the selected one grey while focused.
        w.grid->render(m_canvas);
        return;
    case UiWidgetKind::List: {
        // The window's entries as rows, the selected one grey while focused.
        const ScrollingMenu& list = *w.list;
        const float scale = def.size > 0.0F ? def.size : 1.0F;
        for (std::size_t row = 0; row < list.visibleCount(); ++row) {
            const std::size_t entry = list.firstVisible() + row;
            const bool selected = entry == list.selected() && screen.focused() == w.id;
            UiWidgetDef rowDef = def;
            rowDef.size = scale;
            listText(keyOf(w.id, std::to_string(row)), rowDef, w.items.at(entry).text, def.x,
                     def.y + static_cast<float>(row) * listPitch(scale), selected ? kSelectedGrey : kMenuRed, frame);
        }
        return;
    }
    case UiWidgetKind::Keyboard: {
        // The cells at PM_Create's pitches from the widget's place, the cursor grey.
        const NameKeyboard& keyboard = *w.keyboard;
        std::size_t index = 0;
        for (std::size_t row = 0; row < keyboard.rows().size(); ++row) {
            for (std::size_t column = 0; column < keyboard.rows().at(row); ++column, ++index) {
                const bool selected = index == keyboard.selected() && screen.focused() == w.id;
                UiWidgetDef cellDef = def;
                cellDef.fontSlot = kBigFontSlot;
                listText(keyOf(w.id, std::to_string(index)), cellDef, keyboard.cells().at(index).label,
                         def.x + static_cast<float>(column) * PmCreate::kKeyColumnPitch,
                         def.y + static_cast<float>(row) * PmCreate::kKeyRowPitch, selected ? kSelectedGrey : kMenuRed,
                         frame);
            }
        }
        listText(keyOf(w.id, "typed"), def, w.typed, def.x, def.y - PmCreate::kKeyRowPitch, kMenuGrey, frame);
        return;
    }
    }
}

void UiView::listText(const std::string& key, const UiWidgetDef& def, std::string_view text, float x, float y,
                      graphics::Rgba colour, const GuiFrame& frame) {
    TextWidget& widget = kept<TextWidget>(m_widgets, m_drawn, key, [] { return std::make_unique<TextWidget>(); });
    widget.setup(TextWidgetSetup{.x = x,
                                 .y = y,
                                 .scale = def.size,
                                 .colour = colour,
                                 .alignment = def.alignment,
                                 .fontSlot = def.fontSlot,
                                 .wrapWidth = def.width});
    if (widget.text() != text) {
        widget.setText(text);
    }
    widget.update(frame);
    widget.render(m_canvas);
}

void UiView::listLabel(std::string_view title, const GuiFrame& frame) {
    if (std::ranges::find(m_labelTexts, title) != m_labelTexts.end()) {
        return;
    }
    const std::size_t line = m_labelTexts.size();
    TextWidget& label = kept<TextWidget>(m_widgets, m_drawn, "label/" + std::to_string(line),
                                         [] { return std::make_unique<TextWidget>(); });
    label.setup(TextWidgetSetup{.x = kLabelX,
                                .y = kLabelY + static_cast<float>(line) * kLabelLineGap,
                                .scale = kLabelScale,
                                .colour = kMenuGrey,
                                .alignment = TextAlignment::Right,
                                .fontSlot = kTextFontSlot,
                                .wrapWidth = 0.0F});
    if (label.text() != title) {
        label.setText(title);
    }
    label.update(frame);
    label.render(m_labelCanvas);
    m_labelTexts.emplace_back(title);
}

} // namespace coney::gui
