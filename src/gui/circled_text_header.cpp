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
graphics::Rgba faded(graphics::Rgba colour, float fade) {
    colour.a = static_cast<std::uint8_t>(static_cast<float>(colour.a) * fade);
    return colour;
}

} // namespace

CircledHeaderFactors CircledHeaderFactors::forMode(bool widescreen, bool flag02) {
    if (flag02) {
        // 0x02 without 0x04, and with it.
        return widescreen ? CircledHeaderFactors{1.15F, 1.25F, 1.4F, 1.3F, 1.1F, 1.1F, 0.84F}
                          : CircledHeaderFactors{1.49F, 1.63F, 2.4F, 2.3F, 1.1F, 1.1F, 0.84F};
    }
    if (widescreen) {
        return CircledHeaderFactors{0.6F, 0.67F, 0.1F, 0.0F, 1.0F, 1.0F, 1.2F};
    }
    return CircledHeaderFactors{};
}

CircledTextHeader::CircledTextHeader() {
    m_label.setVisible(false);
    setup(CircledTextHeaderSetup{});
}

void CircledTextHeader::setBatch(graphics::SpriteBatch* batch) {
    for (BaseWidget& circle : m_circles) {
        circle.setBatch(batch);
    }
    m_rule.setBatch(batch);
}

void CircledTextHeader::setup(const CircledTextHeaderSetup& setup) {
    m_setup = setup;
    // The texts: title, characters and subtitle in font slot 4, grey. The characters have no shadow: the original's
    // render clears it before each is drawn.
    m_title.setup(TextWidgetSetup{.scale = kTitleSize, .colour = kMenuGrey, .fontSlot = kFontSlot});
    m_subtitle.setup(TextWidgetSetup{.scale = kSubtitleSize, .colour = kMenuGrey, .fontSlot = kFontSlot});
    for (TextWidget& letter : m_letters) {
        letter.setup(TextWidgetSetup{.scale = kLetterSize, .colour = kMenuGrey, .fontSlot = kFontSlot});
        letter.style().shadowAlpha = 0;
    }
    m_label.setup(TextWidgetSetup{.scale = kLabelSize, .colour = kMenuGrey, .fontSlot = kLabelFontSlot});
    m_circleColours.fill(colourAt(0x005fd260));
}

void CircledTextHeader::setTitle(std::string_view text, int checkpoint, std::string_view levelName) {
    // Every character before the first ':' gets a circle; each ':' that is not the first character adds a circle
    // showing the checkpoint's letter in grey (black again at the next update); the other characters after the first
    // ':' are the title, without any later ':'.
    // Stand-in: more than three circles are dropped; the original has room for three and writes past them.
    m_used = 0;
    const auto addCircle = [this](char character) {
        if (m_used < kCircles) {
            m_letters.at(m_used).setText(std::string(1, character));
            ++m_used;
        }
    };
    std::string title;
    bool inTitle = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ':') {
            if (i > 0 && m_used < kCircles) {
                addCircle(static_cast<char>('@' + checkpoint));
                m_letters.at(m_used - 1).style().colour = kMenuGrey;
            }
            inTitle = true;
        } else if (inTitle) {
            title += text[i];
        } else {
            addCircle(text[i]);
        }
    }
    m_title.setText(title);
    m_subtitle.setText(levelName);
    // The last circle red.
    if (m_used > 0) {
        m_circleColours.at(m_used - 1) = kLastCircle;
    }
}

void CircledTextHeader::setCircleColour(graphics::Rgba colour) { m_circleColours.fill(colour); }

void CircledTextHeader::setLetterColour(graphics::Rgba colour) {
    for (TextWidget& letter : m_letters) {
        letter.style().colour = colour;
    }
}

void CircledTextHeader::setFade(float fade) {
    m_fade = fade;
    for (TextWidget& letter : m_letters) {
        letter.setFade(fade);
    }
    m_title.setFade(fade);
    m_subtitle.setFade(fade);
    m_label.setFade(fade);
}

void CircledTextHeader::update(const GuiFrame& frame) {
    for (std::size_t i = 0; i < m_used; ++i) {
        m_letters.at(i).style().colour = graphics::kBlack;
    }
    for (TextWidget& letter : m_letters) {
        letter.update(frame);
    }
    m_title.update(frame);
    m_subtitle.update(frame);
    m_label.update(frame);
}

void CircledTextHeader::place(const GuiCanvas& canvas) const {
    const CircledTextHeaderSetup& s = m_setup;
    const CircledHeaderFactors& k = s.factors;

    // Rule B over the title. Its size is set after the widget's update, through the device's conversion, so the GUI
    // width 0.57 is drawn stretched by W / H into overlay units (about 0.51 of the screen) and not refitted to the
    // rectangle's shape.
    m_rule.setup(BaseWidgetSetup{.x = s.x + kRuleOffsetX,
                                 .y = (s.y + kRuleBOffsetY) * k.ruleY,
                                 .height = kRuleHeight,
                                 .width = kRuleWidth * graphics::kLogicalWidth / graphics::kLogicalHeight,
                                 .colour = faded(kMenuGrey, m_fade)});

    // y0 from the position, rule A's height, the title's line height and the text offset.
    // Stand-in: the original's line height comes from Font_Size (0x00179808), which scales by the live view aspect
    // 0x0050b208 (1.45 in interlaced 4:3: 0.099 for the title), where Coney's font metrics use 1.3333 (0.091), so the
    // header sits about 3 px higher than the original's until the font metrics take the view aspect.
    const graphics::FontMetrics titleMetrics = graphics::fontMetrics(kTitleSize);
    const float lineHeight = titleMetrics.height + titleMetrics.lineGap;
    const float y0 = s.y - (kRuleHeight / 2.0F) + lineHeight + s.textOffsetY - kTopLift;

    m_subtitle.style().x = kSubtitleX;
    m_subtitle.style().y = (y0 + kSubtitleGap) * k.subtitleY;

    // The circles from x 0.272, each centred on its character; the title after the last.
    float x = kFirstCircleX;
    const float size = kCircleSize * k.circleScale;
    for (std::size_t i = 0; i < m_used; ++i) {
        TextWidget& letter = m_letters.at(i);
        letter.style().x = x;
        letter.style().y = y0 * k.letterY;
        // The size is set without the device's conversion before the widget's update, which keeps the height and
        // fits the width to the rectangle's shape: so only the height is given. Stand-in: the character's width is
        // measured now at size 1.7; the original measured it when the text was set, at the size it had then (1.561
        // before the header's first update).
        m_circles.at(i).setup(BaseWidgetSetup{.x = x + (letter.width(canvas) / 2.0F),
                                              .y = y0 * k.circleY,
                                              .height = size,
                                              .colour = faded(m_circleColours.at(i), m_fade)});
        x += kCircleStep;
    }
    m_title.style().x = m_used > 0 ? x : kTitleXAlone;
    m_title.style().y = y0 * k.letterY;

    // Stand-in: rule A is not made (the original places it here but never draws it), and the header has no child
    // to place under it (the pause menu draws its objective lists itself).
    // The label in its box, centred on its own height.
    const float labelHeight = m_label.layout(canvas).height;
    m_label.style().x = s.x - (s.boxWidth / 2.0F) + s.textOffsetX + kLabelOffsetX;
    m_label.style().y = (s.y + (kRuleHeight / 2.0F) - (labelHeight / 2.0F) + s.textOffsetY + kLabelOffsetY) * k.labelY;
}

void CircledTextHeader::render(const GuiCanvas& canvas) const {
    if (!visible()) {
        return;
    }
    place(canvas);
    m_label.render(canvas);
    for (std::size_t i = 0; i < m_used; ++i) {
        m_circles.at(i).render(canvas);
        m_letters.at(i).render(canvas);
    }
    m_rule.render(canvas);
    m_title.render(canvas);
    m_subtitle.render(canvas);
}

} // namespace coney::gui
