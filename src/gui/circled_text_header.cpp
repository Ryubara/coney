// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/circled_text_header.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "graphics/font.h"
#include "graphics/screen.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// The rules' x offset from X, rule A's y offset from Y and rule B's from Y (before its factor).
constexpr float kRuleOffsetX = 0.035F;
constexpr float kRuleBOffsetY = -0.07F;
// The constants y0 takes off: 0.045 and 0.151.
constexpr float kTopLift = 0.045F + 0.151F;
// The subtitle's offset below y0.
constexpr float kSubtitleGap = 0.075F;
// The label's offsets: x from the box's left edge, y below Y.
constexpr float kLabelOffsetX = -0.005F;
constexpr float kLabelOffsetY = 0.1F;

// `colour` with its alpha × `fade`: the menu's fade, which RenderColoured writes into each part's alpha.
// @coney-only a helper; the original writes the colour's alpha byte into each part
graphics::Rgba faded(graphics::Rgba colour, float fade) { return {}; }

} // namespace

CircledHeaderFactors CircledHeaderFactors::forMode(bool widescreen, bool flag02) { return {}; }

CircledTextHeader::CircledTextHeader() {}

void CircledTextHeader::setBatch(graphics::SpriteBatch* batch) {}

void CircledTextHeader::setup(const CircledTextHeaderSetup& setup) {}

void CircledTextHeader::setTitle(std::string_view text, int checkpoint, std::string_view levelName) {}

void CircledTextHeader::setCircleColour(graphics::Rgba colour) {}

void CircledTextHeader::setLetterColour(graphics::Rgba colour) {}

void CircledTextHeader::setFade(float fade) {}

void CircledTextHeader::update(const GuiFrame& frame) {}

void CircledTextHeader::place(const GuiCanvas& canvas) const {}

void CircledTextHeader::render(const GuiCanvas& canvas) const {}

} // namespace coney::gui
