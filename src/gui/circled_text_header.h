// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/base_widget.h"
#include "gui/colour_table.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// The layout factors of the pause header for one video mode (`CircledTextHeader_ApplyVideoMode`, the globals
/// `0x0050f2fc`-`0x0050f310` and the circle scale `0x0050f2dc`): each multiplies one y of the layout.
struct CircledHeaderFactors {
    float ruleY = 0.9F;       ///< a: rule B's y.
    float subtitleY = 0.95F;  ///< b: the subtitle's y.
    float circleY = 0.78F;    ///< c: the circles' centres.
    float letterY = 0.68F;    ///< d: the circles' characters and the title.
    float labelY = 1.0F;      ///< e: the label's y.
    float childY = 1.0F;      ///< f: the child's y (Coney's header has no child).
    float circleScale = 1.2F; ///< s: the circles' size factor.

    /// The factors in the default mode (device flag `0x01`, and progressive 4:3), in 16:9 (`widescreen`, flag `0x04`
    /// without `0x02`) and with flag `0x02` (with or without `0x04`). Flag `0x08` is not modelled (Coney has no such
    /// device). The original writes s only in the `0x02` modes and never puts it back; Coney gives every mode its own.
    /// @orig 0x001e81c0 CircledTextHeader_ApplyVideoMode (unknown)
    [[nodiscard]] static CircledHeaderFactors forMode(bool widescreen, bool flag02);
};

/// Where the pause header sits: its position, box and text offset (the caller's, `+0x20`-`+0x30` and `+0x360`).
struct CircledTextHeaderSetup {
    float x = 0.5F;             ///< X: the header's centre x.
    float y = 0.22F;            ///< Y: its row.
    float boxWidth = 0.5F;      ///< W: the box the label sits in.
    float textOffsetX = 0.01F;  ///< ox.
    float textOffsetY = -0.01F; ///< oy.
    CircledHeaderFactors factors{};
};

/// The big header at the top of the story pause menu (our name `CircledTextHeader`, the original's one instance at
/// `0x006340c0`): the mission's title after up to three circled characters, the level's name under it, a rule between
/// them, and a label (`IconTextHeader`'s text) the pause menu hides on its grid.
///
/// - **The text** (setTitle()): the characters before the first `:` get a circle each; the `:` (when not the first
///   character) adds a circle whose character is `@` + the checkpoint (`A` for 1); what follows is the title. The
///   last circle is then red `0x005fd328`. The subtitle is the level's name.
/// - **Layout** (each frame, in render(): a circle's centre needs its character's width), from the position (X, Y),
///   the text offset oy, rule A's height h = 0.007, the title's line height t (h + lineGap of `Font_Size(2.23)`) and
///   the mode's factors: y0 = Y − h / 2 + t + oy − 0.196; rule B centred at (X + 0.035, (Y − 0.07) × a), 0.57 ×
///   0.007; the subtitle at (0.257, (y0 + 0.075) × b); the circles' characters from x 0.272, 0.051 apart, at y0 × d,
///   each circle centred on its character at y0 × c, 0.082 × s square; the title after the last circle (x 0.255
///   with none) at y0 × d; the label at (X − W / 2 + ox − 0.005, (Y + h / 2 − its height / 2 + oy + 0.1) × e).
/// - **Look**: circles `part_page0` rectangle 84, black until coloured; characters font slot 4 at `Font_Size(1.7)`,
///   opaque black every update (so setLetterColour() lasts one frame), no shadow; title `Font_Size(2.23)` and
///   subtitle `Font_Size(1.7)`, font slot 4, grey `0x005fd310`, shadow 0x80; rule B `part_page0` rectangle 10, grey.
/// - **Drawing** (`RenderColoured`): the label in the given colour (when visible), each used circle and its character,
///   rule B, the title and the subtitle, all at the menu's fade.
///
/// Coney's choices: rule A is not made (the original updates it but never draws it); the header has no child (the
/// pause menu's checklist; Coney draws the objective lists itself); more than three circles are dropped (nothing
/// bounds the count in the original, which has room for three); the characters and the subtitle take the update's
/// size 1.7 (the set-up's 1.561 is replaced every update).
///
/// Research: docs/research/gui.md#circled-text-header, docs/research/gui.md#circled-text-header-layout
/// @orig 0x001e7f50 CircledTextHeader_Construct (unknown)
class CircledTextHeader : public Widget {
  public:
    /// The most circles the header holds.
    static constexpr std::size_t kCircles = 3;
    /// `part_page0` rectangles: the circles (sprite word `0x54`) and rule B (sprite word 10).
    static constexpr std::size_t kCircleRect = 84;
    static constexpr std::size_t kRuleRect = 10;
    /// The depths of the circles (8,500) and rule B (8,000): the batch the header draws into is made at kCircleDepth.
    static constexpr float kCircleDepth = 8500.0F;
    /// The font slot and sizes of the title, the characters and the subtitle.
    static constexpr int kFontSlot = 4;
    static constexpr float kTitleSize = 2.23F;
    static constexpr float kLetterSize = 1.7F;
    static constexpr float kSubtitleSize = 1.7F;
    /// The label's size and font slot (`IconTextHeader`'s text).
    static constexpr float kLabelSize = 1.0F;
    static constexpr int kLabelFontSlot = 3;
    /// Rule A's height h, and the rules' width.
    static constexpr float kRuleHeight = 0.007F;
    static constexpr float kRuleWidth = 0.57F;
    /// The circles: the first character's x, the step between them, the title's x without circles, the size.
    static constexpr float kFirstCircleX = 0.272F;
    static constexpr float kCircleStep = 0.051F;
    static constexpr float kTitleXAlone = 0.255F;
    static constexpr float kCircleSize = 0.082F;
    /// The subtitle's x.
    static constexpr float kSubtitleX = 0.257F;
    /// The last circle's colour (`0x005fd328`).
    static constexpr graphics::Rgba kLastCircle = kMenuRed;

    CircledTextHeader();

    /// Draws the circles and rule B into `batch` (a batch over `part_page0` at kCircleDepth; not owned, null: none).
    void setBatch(graphics::SpriteBatch* batch);
    /// Places the header and gives its parts their look (`CircledTextHeader_Setup`, slot `+0xa8`).
    /// @orig 0x001e8368 CircledTextHeader_Setup (unknown)
    void setup(const CircledTextHeaderSetup& setup);
    /// Splits `text` into circles and title (above), the `:` circle showing `@` + `checkpoint`, and shows `levelName`
    /// under it; the last circle turns red.
    /// @orig 0x001e8ee0 CircledTextHeader_SetTitle (unknown)
    void setTitle(std::string_view text, int checkpoint, std::string_view levelName);
    /// Colours all three circles.
    /// @orig 0x001e85e8 CircledTextHeader_SetCircleColour (unknown)
    void setCircleColour(graphics::Rgba colour);
    /// Colours all three characters, until the next update makes them black again.
    /// @orig 0x001e8658 CircledTextHeader_SetLetterColour (unknown)
    void setLetterColour(graphics::Rgba colour);
    /// The label's text (`IconTextHeader_SetText`).
    /// @orig 0x001e7e58 IconTextHeader_SetText (unknown)
    void setLabel(std::string_view text) { m_label.setText(text); }
    /// Shows or hides the label (`IconTextHeader_SetVisible`; the pause grid hides it).
    /// @orig 0x001e7ea8 IconTextHeader_SetVisible (unknown)
    void setLabelVisible(bool visible) { m_label.setVisible(visible); }
    /// The colour `RenderColoured` draws the label in.
    void setLabelColour(graphics::Rgba colour) { m_label.style().colour = colour; }
    /// The menu's fade, 0 to 1, on every part.
    void setFade(float fade);

    /// Makes the used characters opaque black again and advances the texts' time.
    /// @orig 0x001e86c8 CircledTextHeader_Update (unknown)
    void update(const GuiFrame& frame) override;
    /// Places every part (the layout above) and adds it to the batches. Nothing while hidden.
    /// @orig 0x001e8d00 CircledTextHeader_Render (unknown)
    /// @orig 0x001e8dd0 CircledTextHeader_RenderColoured (unknown)
    void render(const GuiCanvas& canvas) const override;

    /// How many circles are in use.
    [[nodiscard]] std::size_t circles() const { return m_used; }
    /// Circle `index` and its character (placed by the last render()).
    [[nodiscard]] const BaseWidget& circle(std::size_t index) const { return m_circles.at(index); }
    [[nodiscard]] const TextWidget& letter(std::size_t index) const { return m_letters.at(index); }
    /// The title, the subtitle, rule B and the label (placed by the last render()).
    [[nodiscard]] const TextWidget& title() const { return m_title; }
    [[nodiscard]] const TextWidget& subtitle() const { return m_subtitle; }
    [[nodiscard]] const BaseWidget& rule() const { return m_rule; }
    [[nodiscard]] const TextWidget& label() const { return m_label; }
    /// The set-up.
    [[nodiscard]] const CircledTextHeaderSetup& setupValues() const { return m_setup; }

  private:
    // Places every part for `canvas`'s fonts (the update's layout; the circles need their characters' widths).
    void place(const GuiCanvas& canvas) const;

    CircledTextHeaderSetup m_setup;
    // Placed by render(), which needs the fonts.
    mutable std::array<BaseWidget, kCircles> m_circles{
        {{nullptr, kCircleRect}, {nullptr, kCircleRect}, {nullptr, kCircleRect}}};
    std::array<graphics::Rgba, kCircles> m_circleColours{}; // before the fade
    mutable std::array<TextWidget, kCircles> m_letters;
    mutable TextWidget m_title;
    mutable TextWidget m_subtitle;
    mutable TextWidget m_label;
    mutable BaseWidget m_rule{nullptr, kRuleRect};
    std::size_t m_used = 0;
    float m_fade = 1.0F;
};

} // namespace coney::gui
