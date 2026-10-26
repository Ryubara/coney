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

// `colour` with its alpha × `fade`.
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
    // The texts: title, characters and subtitle in font slot 4; the characters black without a shadow.
    m_title.setup(TextWidgetSetup{.scale = kTitleSize, .colour = kMenuGrey, .fontSlot = kFontSlot});
    m_subtitle.setup(TextWidgetSetup{.scale = kSubtitleSize, .colour = kMenuGrey, .fontSlot = kFontSlot});
    for (TextWidget& letter : m_letters) {
        letter.setup(TextWidgetSetup{.scale = kLetterSize, .colour = graphics::kBlack, .fontSlot = kFontSlot});
        letter.style().shadowAlpha = 0;
    }
    m_label.setup(TextWidgetSetup{.scale = kLabelSize, .colour = kMenuGrey, .fontSlot = kLabelFontSlot});
    m_circleColours.fill(colourAt(0x005fd260));
}

void CircledTextHeader::setTitle(std::string_view text, int checkpoint, std::string_view levelName) {
    // Every character before the first ':' gets a circle; a ':' that is not the first character adds the
    // checkpoint's letter; the rest is the title.
    std::string characters;
    std::string_view title = text;
    if (const std::size_t colon = text.find(':'); colon != std::string_view::npos) {
        characters = std::string(text.substr(0, colon));
        if (colon > 0) {
            characters += static_cast<char>('@' + checkpoint);
        }
        title = text.substr(colon + 1);
    }
    m_used = std::min(characters.size(), kCircles);
    for (std::size_t i = 0; i < kCircles; ++i) {
        m_letters.at(i).setText(i < m_used ? characters.substr(i, 1) : std::string());
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
        // fits the width to the rectangle's shape: so only the height is given.
        m_circles.at(i).setup(BaseWidgetSetup{.x = x + (letter.width(canvas) / 2.0F),
                                              .y = y0 * k.circleY,
                                              .height = size,
                                              .colour = faded(m_circleColours.at(i), m_fade)});
        x += kCircleStep;
    }
    m_title.style().x = m_used > 0 ? x : kTitleXAlone;
    m_title.style().y = y0 * k.letterY;

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
