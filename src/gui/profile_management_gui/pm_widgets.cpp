// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_widgets.h"

#include <algorithm>
#include <utility>

#include "gui/colour_table.h"

namespace coney::gui {

float PmLayout::gridFor(std::size_t rows) const { return gridY.at(std::clamp<std::size_t>(rows, 1, 4) - 1); }

float PmLayout::titleFor(std::size_t rows) const { return titleY.at(std::clamp<std::size_t>(rows, 1, 4) - 1); }

PmLayout PmLayout::forFlags(const PmVideoFlags& flags) {
    // The five columns of the table: default, 0x20, 0x04, 0x02, 0x02 + 0x04.
    if (flags.flag20) {
        return PmLayout{-0.05F, {0.815F, 0.765F, 0.71F, 0.665F}, {0.745F, 0.70F, 0.64F, 0.60F}, 0.885F};
    }
    if (flags.flag02) {
        if (flags.widescreen) {
            return PmLayout{-0.14F, {0.74F, 0.695F, 0.655F, 0.61F}, {0.687F, 0.65F, 0.61F, 0.56F}, 0.79F};
        }
        return PmLayout{0.02F, {0.734F, 0.70F, 0.667F, 0.63F}, {0.687F, 0.65F, 0.62F, 0.585F}, 0.78F};
    }
    if (flags.widescreen) {
        return PmLayout{-0.11F, {0.80F, 0.74F, 0.68F, 0.625F}, {0.73F, 0.67F, 0.605F, 0.545F}, 0.87F};
    }
    return PmLayout{};
}

namespace pm {

void setupTitle(TextWidget& title, const PmShared& shared, std::string_view text, float y) {
    title.init();
    title.setText(text);
    title.setup(TextWidgetSetup{.x = shared.layout.x,
                                .y = y,
                                .scale = kTitleScale,
                                .colour = kMenuRed,
                                .alignment = TextAlignment::Left,
                                .fontSlot = kBigFontSlot});
}

void setupGrid(OptionGrid& grid, PmShared& shared, float y, std::vector<std::size_t> rows) {
    grid.init();
    grid.setup(OptionGridSetup{
        .y = y, .rows = std::move(rows), .leftX = shared.layout.x, .moveCue = kMoveCue, .playCue = [&shared](int cue) {
            shared.cue(cue);
        }});
}

OptionGridItem item(std::string_view text, int code, bool separator) {
    return OptionGridItem{.text = std::string(text),
                          .code = code,
                          .separator = separator,
                          .enabled = true,
                          .colour = kMenuRed,
                          .scale = kItemScale,
                          .fontSlot = kBigFontSlot};
}

void setupUsage(UsageInfo& usage, const PmShared& shared) {
    usage.init();
    usage.place(shared.layout.x, shared.layout.usageY, true);
    usage.setLegend(shared.string(UsageInfo::kMenuUsageString));
}

} // namespace pm

} // namespace coney::gui
