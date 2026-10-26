// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/pause_stats.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/parse_number.h"
#include "gui/colour_table.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// One Warrior's kind: the character types `Human_GetPortraitKind` maps to it (-1 pads a short list), and his pictures
// and text.
struct KindEntry {
    std::array<int, 5> types;
    StatsPortrait portrait;
};

// The nine Warriors' kinds, 0-8 (docs/research/pause.md#stats-screen).
constexpr std::array<KindEntry, 9> kKinds{{
    {{5, 7, 8, 9, 10}, {0, 0x31, 0x197, 0xf2}},      // Swan
    {{15, 17, -1, -1, -1}, {1, 0x22, 0x192, 0xec}},  // Cochise
    {{18, 20, 188, -1, -1}, {2, 0x23, 0x193, 0xed}}, // Cowboy
    {{11, 13, 14, -1, -1}, {3, 0x1f, 0x190, 0xea}},  // Ajax
    {{30, 32, -1, -1, -1}, {4, 0x2f, 0x195, 0xf0}},  // Rembrandt
    {{21, 23, 24, 25, -1}, {5, 0x24, 0x194, 0xee}},  // Fox
    {{26, 28, 29, 191, -1}, {6, 0x32, 0x198, 0xef}}, // Vermin
    {{33, 35, 36, 37, 190}, {7, 0x30, 0x196, 0xf1}}, // Snow
    {{1, 3, 4, 189, -1}, {8, 0x21, 0x191, 0xeb}},    // Cleon
}};

// One difficulty's row of the table at `0x0050f230`: t0-t5 (t6-t9 are not read here).
struct DifficultyRow {
    float t0, t1, t2, t3, t4, t5;
};
constexpr std::array<DifficultyRow, 3> kDifficultyRows{{
    {110.0F, 228.0F, 460.0F, 715.0F, 25.0F, 160.0F},
    {75.0F, 192.0F, 260.0F, 600.0F, 25.0F, 160.0F},
    {60.0F, 168.0F, 360.0F, 660.0F, 25.0F, 160.0F},
}};

// A full Health bar is byte 100 and a full Rage bar a rage maximum of 150: the original multiplies by these
// reciprocals (0.01 and 0.006666667), and so does Coney, for the same roundings.
constexpr float kPerHealthByte = 0.01F;
constexpr float kPerRage = 0.006666667F;

// The text a leading `<AUTOINDENT ` starts.
constexpr std::string_view kAutoIndent = "<AUTOINDENT ";

// `colour` with its alpha × `fade`.
// @coney-only Coney's fade of a colour; the original writes the menu's alpha byte into each part's colour
graphics::Rgba faded(graphics::Rgba colour, float fade) {
    colour.a = static_cast<std::uint8_t>(static_cast<float>(colour.a) * fade);
    return colour;
}

// The wrap width of `text`: the number after a leading `<AUTOINDENT `, else `fallback`.
// @orig 0x001bb390 MessageHUD_ReadAutoIndent (unknown)
float autoIndentWidth(std::string_view text, float fallback) {
    if (!text.starts_with(kAutoIndent)) {
        return fallback;
    }
    // The number runs to the tag's `>`. Coney's choice: a number that does not read keeps the fallback, where the
    // original's scan would leave its value unset.
    std::string_view number = text.substr(kAutoIndent.size());
    number = number.substr(0, number.find('>'));
    const std::optional<double> width = parseDecimal(number);
    return width ? static_cast<float>(*width) : fallback;
}

} // namespace

// The name, its two shadows and the face as sprite widgets with no batch yet, the four bars over the meter's body
// rectangle, and the texts.
PauseStats::PauseStats()
    : m_name(nullptr, 0), m_shadows{{{nullptr, 0}, {nullptr, 0}}}, m_face(nullptr, 0),
      m_bars{{{nullptr, kBarRect}, {nullptr, kBarRect}, {nullptr, kBarRect}, {nullptr, kBarRect}}} {}

// The kind's row: `Human_GetPortraitKind`'s character types and setup's switch on the kind.
std::optional<StatsPortrait> PauseStats::portraitOf(int type) {
    for (const KindEntry& entry : kKinds) {
        if (std::ranges::find(entry.types, type) != entry.types.end()) {
            return entry.portrait;
        }
    }
    return std::nullopt;
}

// The four fills as setup computes them, before the bars clamp them.
std::array<float, PauseStats::kBars> PauseStats::barValues(const WarriorStats& stats, int difficulty) {
    const DifficultyRow& t = kDifficultyRows.at(static_cast<std::size_t>(std::clamp(difficulty, 0, 2)));
    const float strength = (static_cast<float>(stats.damageScale) - t.t0) / (t.t1 - t.t0);
    const float stamina = (static_cast<float>(stats.powerMax) + static_cast<float>(stats.staminaMax) - t.t2 + t.t4) /
                          (t.t3 + t.t5 - t.t2 + t.t4);
    const float health = static_cast<float>(stats.powerClassByte40) * kPerHealthByte;
    const float rage = static_cast<float>(stats.rageMax) * kPerRage;
    return {strength, std::min(stamina, 1.0F), std::min(health, 1.0F), std::min(rage, 1.0F)};
}

// Hands each part the batch over its sheet.
void PauseStats::setBatches(graphics::SpriteBatch* name, graphics::SpriteBatch* face, graphics::SpriteBatch* bars) {
    for (BaseWidget& shadow : m_shadows) {
        shadow.setBatch(name);
    }
    m_name.setBatch(name);
    m_face.setBatch(face);
    for (Bar& bar : m_bars) {
        bar.setBatch(bars);
        bar.setFillRect(kBarFillRect);
        bar.setCaps(kBarLeftCapRect, kBarRightCapRect);
    }
}

bool PauseStats::setup(const std::optional<WarriorStats>& stats, int difficulty, const GlobalStrings* strings,
                       bool widescreen) {
    m_portrait = stats ? portraitOf(stats->characterType) : std::nullopt;
    // Stand-in: of the video modes' layouts only 16:9's (flag 0x04 alone: B's y 0.45) is modelled; the 0x08 and 0x02
    // modes' offsets and size factor are not (docs/research/pause.md#stats-screen).
    m_baseY = widescreen ? kWideBaseY : kBaseY;
    if (!m_portrait || !stats) {
        return false;
    }
    m_values = barValues(*stats, difficulty);
    for (std::size_t i = 0; i < kBars; ++i) {
        m_bars.at(i).setFill(m_values.at(i));
    }
    // The texts: the labels and the description, all grey `0x005fd310` in font slot 3.
    const auto text = [strings](std::uint32_t id) { return strings != nullptr ? strings->get(id) : ""; };
    static constexpr std::array<std::uint32_t, kBars> kLabels{stats_strings::kStrength, stats_strings::kStamina,
                                                              stats_strings::kHealth, stats_strings::kRage};
    for (std::size_t i = 0; i < kBars; ++i) {
        m_labels.at(i).setup(TextWidgetSetup{.colour = kMenuGrey, .fontSlot = kTextFontSlot});
        m_labels.at(i).setText(text(kLabels.at(i)));
    }
    // The description wraps at its leading `<AUTOINDENT>` width, else 0.55; its line window starts at the top: 0-5.
    const std::string description{text(m_portrait->description)};
    m_description.setup(TextWidgetSetup{
        .colour = kMenuGrey, .fontSlot = kTextFontSlot, .wrapWidth = autoIndentWidth(description, kDescriptionWrap)});
    m_description.setText(description);
    m_description.style().paged = true;
    m_description.style().firstLine = 0;
    m_description.style().lastLine = kDescriptionLastLine;
    m_descriptionLines = 0;
    // The menu's alpha back to full.
    setFade(1.0F);
    return true;
}

// Plays `cue` through the sink, if any.
void PauseStats::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

bool PauseStats::handle(MenuCommand command) {
    switch (command) {
    case MenuCommand::Up: {
        // `MessageHUD_PageUp`: both ends up one line while the first is above 0.
        TextStyle& style = m_description.style();
        if (style.firstLine == 0) {
            playCue(kRefusedCue);
            return false;
        }
        --style.firstLine;
        --style.lastLine;
        playCue(kMoveCue);
        return false;
    }
    case MenuCommand::Down: {
        // `MessageHUD_PageDown`: both ends down one line while the last is above the text's last line.
        TextStyle& style = m_description.style();
        if (m_descriptionLines == 0 || style.lastLine >= m_descriptionLines - 1) {
            playCue(kRefusedCue);
            return false;
        }
        ++style.firstLine;
        ++style.lastLine;
        playCue(kMoveCue);
        return false;
    }
    case MenuCommand::Left:
    case MenuCommand::Right:
        playCue(kRefusedCue);
        return false;
    case MenuCommand::Back:
        playCue(kBackCue);
        return true;
    case MenuCommand::Accept:
        return false;
    }
    return false;
}

void PauseStats::update(const GuiFrame& frame) {
    // Only an available screen is placed. Coney's choice: it is placed at once, where the original first waits for
    // its pictures (PauseStats_IsReady), since Coney loads them when the pause starts.
    if (!available()) {
        return;
    }
    const float x = kBaseX;
    const float y = m_baseY;

    // The name's left edge and its two black shadows. Setup gives the shadows alpha 128, but each draw sets every
    // part's alpha to the menu's (BaseWidget_SetAlpha), so they are drawn opaque, at the menu's alpha.
    const float portraitX = x + kPortraitDx;
    const float portraitY = y + kPortraitDy;
    m_name.setup(BaseWidgetSetup{.x = portraitX,
                                 .y = portraitY,
                                 .height = kPortraitSize,
                                 .colour = faded(kPortraitTint, m_fade),
                                 .anchor = SpriteAnchor::Left});
    for (std::size_t i = 0; i < m_shadows.size(); ++i) {
        m_shadows.at(i).setup(BaseWidgetSetup{.x = portraitX + kShadowOffsets.at(i).at(0),
                                              .y = portraitY + kShadowOffsets.at(i).at(1),
                                              .height = kPortraitSize,
                                              .colour = faded(kShadowColour, m_fade),
                                              .anchor = SpriteAnchor::Left});
    }
    // The name and face show the Warrior's own pictures, rectangle 0 of their records.
    m_face.setup(BaseWidgetSetup{
        .x = x + kBannerDx, .y = y + kBannerDy, .height = kBannerSize, .colour = faded(graphics::kWhite, m_fade)});

    // The bars and their labels, one row 0.05 apart.
    for (std::size_t i = 0; i < kBars; ++i) {
        const float step = kBarStep * static_cast<float>(i);
        m_bars.at(i).setup(BarSetup{.x = x + kBarDx,
                                    .y = y + kBarDy + step,
                                    .width = kBarWidth,
                                    .height = kBarHeight,
                                    .backColour = faded(kBarBack, m_fade),
                                    .fillColour = faded(kBarFill, m_fade)});
        m_labels.at(i).style().x = kLabelX + kLabelOffsetX;
        m_labels.at(i).style().y = y + kLabelDy + step + kLabelOffsetY;
        m_labels.at(i).update(frame);
    }
    m_description.style().x = x + kDescriptionDx;
    m_description.style().y = y + kDescriptionDy;
    m_description.update(frame);
}

void PauseStats::render(const GuiCanvas& canvas) const {
    // Only an available screen is drawn; Coney's choice: there is no ready wait to pass (see update()).
    if (!available()) {
        return;
    }
    for (const BaseWidget& shadow : m_shadows) {
        shadow.render(canvas);
    }
    m_name.render(canvas);
    m_face.render(canvas);
    for (const Bar& bar : m_bars) {
        bar.render(canvas);
    }
    // The bars' labels, which the original's bars draw with themselves.
    for (const TextWidget& label : m_labels) {
        label.render(canvas);
    }
    m_description.render(canvas);
    measure(canvas);
}

// Counts the description's lines.
void PauseStats::measure(const GuiCanvas& canvas) const {
    // The line count the down move stops at, as the original's draw counts it (`MessageHUD_WordWrap`).
    if (canvas.fonts) {
        m_descriptionLines = m_description.layout(canvas).lines;
    }
}

void PauseStats::setFade(float fade) {
    m_fade = fade;
    for (TextWidget& label : m_labels) {
        label.setFade(fade);
    }
    m_description.setFade(fade);
}

// The texts shown, for tests and the log.
std::vector<std::string> PauseStats::lines() const {
    std::vector<std::string> lines;
    if (!available()) {
        return lines;
    }
    for (const TextWidget& label : m_labels) {
        lines.push_back(label.text());
    }
    lines.push_back(m_description.text());
    return lines;
}

} // namespace coney::gui
