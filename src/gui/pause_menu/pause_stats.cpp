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
graphics::Rgba faded(graphics::Rgba colour, float fade) { return {}; }

// The wrap width of `text`: the number after a leading `<AUTOINDENT `, else `fallback`.
// @orig 0x001bb390 MessageHUD_ReadAutoIndent (unknown)
float autoIndentWidth(std::string_view text, float fallback) { return {}; }

} // namespace

// The name, its two shadows and the face as sprite widgets with no batch yet, the four bars over the meter's body
// rectangle, and the texts.
PauseStats::PauseStats()
    : m_name(nullptr, 0), m_shadows{{{nullptr, 0}, {nullptr, 0}}}, m_face(nullptr, 0),
      m_bars{{{nullptr, kBarRect}, {nullptr, kBarRect}, {nullptr, kBarRect}, {nullptr, kBarRect}}} {}

// The kind's row: `Human_GetPortraitKind`'s character types and setup's switch on the kind.
std::optional<StatsPortrait> PauseStats::portraitOf(int type) { return {}; }

// The four fills as setup computes them, before the bars clamp them.
std::array<float, PauseStats::kBars> PauseStats::barValues(const WarriorStats& stats, int difficulty) { return {}; }

// Hands each part the batch over its sheet.
void PauseStats::setBatches(graphics::SpriteBatch* name, graphics::SpriteBatch* face, graphics::SpriteBatch* bars) {}

bool PauseStats::setup(const std::optional<WarriorStats>& stats, int difficulty, const GlobalStrings* strings,
                       bool widescreen) {
    return {};
}

// Plays `cue` through the sink, if any.
void PauseStats::playCue(int cue) const {}

bool PauseStats::handle(MenuCommand command) { return {}; }

void PauseStats::update(const GuiFrame& frame) {}

void PauseStats::render(const GuiCanvas& canvas) const {}

TextLayout PauseStats::descriptionLayout(const GuiCanvas& canvas) const { return {}; }

// Counts the description's line breaks.
void PauseStats::measure(const GuiCanvas& canvas) const {}

void PauseStats::setFade(float fade) {}

// The texts shown, for tests and the log.
std::vector<std::string> PauseStats::lines() const { return {}; }

} // namespace coney::gui
