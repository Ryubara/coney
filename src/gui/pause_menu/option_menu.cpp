// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/option_menu.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include "graphics/overlay_camera.h"
#include "gui/colour_table.h"

namespace coney::gui {

namespace {

// The level without a Split Screen Merge item (`OptionMenu_Open`).
constexpr int kNoMergeLevel = 102;
// The single-item pages' and the brightness bar's label size (`OptionItem_SetLabelScale`).
constexpr float kBigLabelSize = 1.3F;
// The brightness hint's offset under its label (`0x0050ed3c`, `0x0050ed40`).
constexpr float kHintGap = 0.06F;
// The brightness bar's y (`0x0050ed74`).
constexpr float kBrightnessBarY = 0.50F;
// The bars' back and fill are drawn from the bar's place moved by this (`StatBarItem_Setup`).
constexpr float kBarShiftX = -0.004F;
constexpr float kBarShiftY = 0.004F;
// Less than this from an end counts as at the end (the bar's 0.05 steps in floats).
constexpr float kBarEpsilon = 0.001F;

// A colour as the markup's colour tag.
std::string colourTag(graphics::Rgba colour) {
    return std::format("<COLOR {:02x}{:02x}{:02x}{:02x}>", colour.r, colour.g, colour.b, colour.a);
}

// The pad a per-pad setting belongs to: 1 for P2's, else 0.
int padOf(OptionSetting setting) {
    switch (setting) {
    case OptionSetting::InvertP2:
    case OptionSetting::AutoAdjustP2:
    case OptionSetting::VibrationP2:
        return 1;
    default:
        return 0;
    }
}

} // namespace

OptionValues OptionValues::defaults(bool wideVideo) {
    OptionValues values;
    values.wideVideo = wideVideo;
    return values;
}

OptionLayout OptionLayout::forMode(Language language, bool widescreen, bool flag02) {
    OptionLayout layout;
    if (widescreen) {
        // 16:9: everything starts higher.
        layout.titleY = 0.24F;
        layout.lightingLabelY = 0.24F;
        layout.rowY = 0.30F;
        layout.squaresY = 0.34F;
        layout.volumeRows = {0.31F, 0.345F, 0.395F, 0.43F};
    } else if (flag02) {
        // Flag 0x02 without 0x04: titles and rows closer together, the answers further right, smaller squares.
        layout.titleY = 0.32F;
        layout.titleStep = 0.045F;
        layout.rowY = 0.37F;
        layout.rowStep = 0.04F;
        layout.answersOffset += 0.12F;
        layout.squareScale = 0.7F;
    }
    if (language == Language::German) {
        layout.cameraAnswersExtra = 0.1F;
        layout.restoreAnswersOffset = 0.11F;
    }
    return layout;
}

OptionMenu::OptionMenu() {
    for (BaseWidget* widget : {&m_barFrame, &m_barBack, &m_barFill, &m_squares[0], &m_squares[1]}) {
        widget->setVisible(true);
    }
}

std::string_view OptionMenu::string(std::uint32_t id) const { return m_strings != nullptr ? m_strings->get(id) : ""; }

void OptionMenu::playCue(int cue) const {
    if (m_hooks.playCue) {
        m_hooks.playCue(cue);
    }
}

void OptionMenu::setSpriteBatch(graphics::SpriteBatch* batch) {
    for (BaseWidget* widget : {&m_barFrame, &m_barBack, &m_barFill, &m_squares[0], &m_squares[1]}) {
        widget->setBatch(batch);
    }
}

void OptionMenu::open(const GlobalStrings* strings, MenuInput& input, Hooks hooks, const OptionValues& values,
                      const OptionLayout& layout, int level, std::uint64_t nowMs) {
    m_strings = strings;
    m_hooks = std::move(hooks);
    m_values = values;
    m_layout = layout;
    m_level = level;
    m_title = 0;
    m_item = 0;
    m_pageOpen = false;
    m_open = true;
    input.focus(nowMs);

    // The seven titles, left-aligned one step apart in big_font.
    for (std::size_t i = 0; i < kOptionPages; ++i) {
        m_titles.at(i).setup(TextWidgetSetup{.x = kX,
                                             .y = layout.titleY + (layout.titleStep * static_cast<float>(i)),
                                             .scale = kTitleSize,
                                             .colour = kDimGrey,
                                             .fontSlot = kTitleFontSlot});
        m_titles.at(i).setText(string(option_strings::kTitles.at(i)));
    }
    // The pages' heading (OptionPage_SetDescription: size 1.0 x 1.3, font slot 3).
    m_heading.setup(TextWidgetSetup{
        .x = kX, .y = layout.titleY, .scale = kBigLabelSize, .colour = kMenuGrey, .fontSlot = kLabelFontSlot});
    // The brightness hint under the Lighting label.
    m_hint.setup(TextWidgetSetup{.x = kX,
                                 .y = layout.lightingLabelY + kHintGap,
                                 .scale = kLabelSize,
                                 .colour = kDimGrey,
                                 .fontSlot = kLabelFontSlot});
    m_hint.setText(string(option_strings::kLightingHint));
    buildPages();
    colour();
    setFade(m_fade);
}

void OptionMenu::addChoice(OptionPage pageId, OptionSetting setting, std::uint32_t label,
                           std::array<std::uint32_t, 2> answers, float x, float y, float ax, float ay,
                           float labelScale) {
    auto item = std::make_unique<OptionItem>();
    item->setting = setting;
    item->kind = OptionItemKind::Choice;
    item->label = label;
    item->answers = answers;
    item->labelText.setup(
        TextWidgetSetup{.x = x, .y = y, .scale = labelScale, .colour = kDimGrey, .fontSlot = kLabelFontSlot});
    item->labelText.setText(string(label));
    item->answersText.setup(
        TextWidgetSetup{.x = ax, .y = ay, .scale = kLabelSize, .colour = kMenuGrey, .fontSlot = kAnswersFontSlot});
    // The answer first shown is the current value; the first answer means on.
    const OptionValues& v = m_values;
    bool on = false;
    switch (setting) {
    case OptionSetting::InvertP1:
    case OptionSetting::InvertP2:
        on = v.invertCamera.at(static_cast<std::size_t>(padOf(setting)));
        break;
    case OptionSetting::AutoAdjustP1:
    case OptionSetting::AutoAdjustP2:
        on = v.autoAdjustCamera.at(static_cast<std::size_t>(padOf(setting)));
        break;
    case OptionSetting::VibrationP1:
    case OptionSetting::VibrationP2:
        on = v.vibration.at(static_cast<std::size_t>(padOf(setting)));
        break;
    case OptionSetting::Merge:
        on = v.splitScreenMerge;
        break;
    case OptionSetting::Subtitles:
        on = v.subtitles;
        break;
    case OptionSetting::ProLogic:
        on = v.proLogic;
        break;
    case OptionSetting::Video:
        // Normal (mode 0) is the first answer.
        on = !v.wideVideo;
        item->confirm = true;
        break;
    case OptionSetting::RestoreDefault:
        // Always starts on No (`+0x4c0` = 1).
        on = false;
        item->confirm = true;
        break;
    default:
        break;
    }
    item->choice = on ? 0 : 1;
    item->savedChoice = item->choice;
    refreshAnswers(*item);
    m_pages.at(static_cast<std::size_t>(pageId)).push_back(std::move(item));
}

void OptionMenu::addBar(OptionPage pageId, OptionSetting setting, std::uint32_t label, float labelY, float barY) {
    auto item = std::make_unique<OptionItem>();
    item->setting = setting;
    item->kind = setting == OptionSetting::Brightness ? OptionItemKind::Brightness : OptionItemKind::VolumeBar;
    item->label = label;
    const float scale = item->kind == OptionItemKind::Brightness ? kBigLabelSize : kLabelSize;
    item->labelText.setup(
        TextWidgetSetup{.x = kX, .y = labelY, .scale = scale, .colour = kDimGrey, .fontSlot = kLabelFontSlot});
    item->labelText.setText(string(label));
    item->barX = kX;
    item->barY = barY;
    switch (setting) {
    case OptionSetting::Brightness:
        item->bar = static_cast<float>(m_values.brightness) / static_cast<float>(kBrightnessMax);
        break;
    case OptionSetting::Music:
        item->bar = m_values.musicVolume;
        break;
    default:
        item->bar = m_values.soundFxVolume;
        break;
    }
    item->savedBar = item->bar;
    item->savedBrightness = m_values.brightness;
    m_pages.at(static_cast<std::size_t>(pageId)).push_back(std::move(item));
}

void OptionMenu::buildPages() {
    namespace s = option_strings;
    using enum OptionPage;
    for (auto& items : m_pages) {
        items.clear();
    }
    const OptionLayout& l = m_layout;
    const float answersX = kX + l.answersOffset;
    // Lighting: the brightness bar, labelled with the page's name at 1.3.
    addBar(Lighting, OptionSetting::Brightness, s::kTitles.at(0), l.lightingLabelY, kBrightnessBarY);
    // Camera: four or five on/off rows under the heading.
    const std::array<std::pair<OptionSetting, std::uint32_t>, 5> camera{{
        {OptionSetting::InvertP1, s::kInvertP1},
        {OptionSetting::InvertP2, s::kInvertP2},
        {OptionSetting::AutoAdjustP1, s::kAutoAdjustP1},
        {OptionSetting::AutoAdjustP2, s::kAutoAdjustP2},
        {OptionSetting::Merge, s::kMerge},
    }};
    for (std::size_t i = 0; i < camera.size(); ++i) {
        if (camera.at(i).first == OptionSetting::Merge && m_level == kNoMergeLevel) {
            continue;
        }
        const float y = l.rowY + (l.rowStep * static_cast<float>(i));
        addChoice(Camera, camera.at(i).first, camera.at(i).second, {s::kOn, s::kOff}, kX, y,
                  answersX + l.cameraAnswersExtra, y, kLabelSize);
    }
    // Audio: the two volume bars, then Pro Logic II.
    addBar(Audio, OptionSetting::Music, s::kMusic, l.volumeRows[0], l.volumeRows[1]);
    addBar(Audio, OptionSetting::SoundFx, s::kSoundFx, l.volumeRows[2], l.volumeRows[3]);
    addChoice(Audio, OptionSetting::ProLogic, s::kProLogic, {s::kOn, s::kOff}, kX, kProLogicY, answersX, kProLogicY,
              kLabelSize);
    // Video, Subtitles and Restore Default: one choice under the page's name.
    addChoice(Video, OptionSetting::Video, s::kTitles.at(3), {s::kNormal, s::kWide}, kX, l.titleY, kX,
              l.titleY + l.underAnswersOffset, kBigLabelSize);
    addChoice(Subtitles, OptionSetting::Subtitles, s::kTitles.at(4), {s::kOn, s::kOff}, kX, l.titleY, kX,
              l.titleY + l.underAnswersOffset, kBigLabelSize);
    // Vibration: a row per pad.
    addChoice(Vibration, OptionSetting::VibrationP1, s::kP1Controller, {s::kOn, s::kOff}, kX, l.rowY, answersX, l.rowY,
              kLabelSize);
    addChoice(Vibration, OptionSetting::VibrationP2, s::kP2Controller, {s::kOn, s::kOff}, kX, l.rowY + l.rowStep,
              answersX, l.rowY + l.rowStep, kLabelSize);
    addChoice(RestoreDefault, OptionSetting::RestoreDefault, s::kTitles.at(6), {s::kYes, s::kNo}, kX, l.titleY, kX,
              l.titleY + l.restoreAnswersOffset, kBigLabelSize);
}

void OptionMenu::refreshAnswers(OptionItem& item) const {
    // One marked-up line, "A / B", the chosen answer in 0x005fd310 and the other in 0x005fd320.
    const graphics::Rgba first = item.choice == 0 ? kMenuGrey : kDimGrey;
    const graphics::Rgba second = item.choice == 0 ? kDimGrey : kMenuGrey;
    item.answersText.setText(std::format("{}{}</COLOR> / {}{}</COLOR>", colourTag(first), string(item.answers[0]),
                                         colourTag(second), string(item.answers[1])));
}

void OptionMenu::colour() {
    for (std::size_t i = 0; i < kOptionPages; ++i) {
        m_titles.at(i).style().colour = i == m_title ? kMenuGrey : kDimGrey;
    }
    for (std::size_t p = 0; p < kOptionPages; ++p) {
        const std::vector<std::unique_ptr<OptionItem>>& items = m_pages.at(p);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const bool selected = m_pageOpen && p == m_title && i == m_item;
            items.at(i)->labelText.style().colour = selected ? kMenuGrey : kDimGrey;
        }
    }
}

void OptionMenu::close() {
    if (m_open && m_pageOpen && static_cast<OptionPage>(m_title) == OptionPage::Audio && m_hooks.previewMusic) {
        m_hooks.previewMusic(false, m_values.musicVolume);
    }
    m_open = false;
    m_pageOpen = false;
}

bool OptionMenu::handle(MenuCommand command) {
    if (!m_open) {
        return false;
    }
    if (m_pageOpen) {
        if (pageCommand(command)) {
            m_pageOpen = false;
            colour();
        }
        return false;
    }
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down: {
        const bool up = command == MenuCommand::Up;
        if ((up && m_title == 0) || (!up && m_title + 1 >= kOptionPages)) {
            playCue(kRefusedCue);
            return false;
        }
        m_title = up ? m_title - 1 : m_title + 1;
        playCue(kMoveCue);
        colour();
        return false;
    }
    case MenuCommand::Left:
    case MenuCommand::Right:
        return false;
    case MenuCommand::Accept:
        // The page opens on its first item, remembering its values.
        m_pageOpen = true;
        m_item = 0;
        m_heading.setText(string(option_strings::kTitles.at(m_title)));
        beginEdit();
        playCue(kAcceptCue);
        colour();
        if (static_cast<OptionPage>(m_title) == OptionPage::Audio && m_hooks.previewMusic) {
            m_hooks.previewMusic(true, m_values.musicVolume);
        }
        return false;
    case MenuCommand::Back:
        playCue(kBackCue);
        close();
        return true;
    }
    return false;
}

bool OptionMenu::pageCommand(MenuCommand command) {
    std::vector<std::unique_ptr<OptionItem>>& items = page();
    const OptionPage id = static_cast<OptionPage>(m_title);
    const auto leavePage = [&] {
        if (id == OptionPage::Audio && m_hooks.previewMusic) {
            m_hooks.previewMusic(false, m_values.musicVolume);
        }
    };
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down: {
        const bool up = command == MenuCommand::Up;
        if ((up && m_item == 0) || (!up && m_item + 1 >= items.size())) {
            playCue(kRefusedCue);
            return false;
        }
        m_item = up ? m_item - 1 : m_item + 1;
        playCue(kMoveCue);
        colour();
        return false;
    }
    case MenuCommand::Left:
    case MenuCommand::Right:
        if (m_item < items.size()) {
            itemCommand(*items.at(m_item), command == MenuCommand::Right);
        }
        return false;
    case MenuCommand::Accept: {
        // The item's accept (confirm mode applies its answer), cue 8, and the page closes keeping every value.
        bool restored = false;
        if (m_item < items.size() && items.at(m_item)->confirm) {
            OptionItem& item = *items.at(m_item);
            restored = item.setting == OptionSetting::RestoreDefault && item.choice == 0;
            applyChoice(item, true);
        }
        playCue(kAcceptCue);
        leavePage();
        if (restored) {
            // The menu reopens on the new values (`0x0050ed14`); Coney rebuilds the pages in place.
            buildPages();
        }
        return true;
    }
    case MenuCommand::Back:
        playCue(kBackCue);
        // Vibration and Restore Default keep their values; the others put back what the page had when opened.
        if (id != OptionPage::Vibration && id != OptionPage::RestoreDefault) {
            revert();
        }
        leavePage();
        return true;
    }
    return false;
}

void OptionMenu::itemCommand(OptionItem& item, bool right) {
    switch (item.kind) {
    case OptionItemKind::Choice: {
        const std::size_t wanted = right ? 1 : 0;
        if (item.choice == wanted) {
            playCue(kRefusedCue);
            return;
        }
        item.choice = wanted;
        playCue(kMoveCue);
        refreshAnswers(item);
        if (!item.confirm) {
            applyChoice(item, false);
        }
        return;
    }
    case OptionItemKind::VolumeBar:
    case OptionItemKind::Brightness: {
        if ((!right && item.bar <= kBarEpsilon) || (right && item.bar >= 1.0F - kBarEpsilon)) {
            playCue(kRefusedCue);
            return;
        }
        item.bar = std::clamp(item.bar + (right ? kBarStep : -kBarStep), 0.0F, 1.0F);
        playCue(kMoveCue);
        if (item.kind == OptionItemKind::Brightness) {
            // OptionMenu_AddBrightness: the brightness moves by 5 within 0-100.
            m_values.brightness =
                std::clamp(m_values.brightness + (right ? kBrightnessStep : -kBrightnessStep), 0, kBrightnessMax);
            changed();
        } else {
            applyBar(item);
        }
        return;
    }
    }
}

void OptionMenu::applyChoice(OptionItem& item, bool confirmed) {
    const bool on = item.choice == 0;
    OptionValues& v = m_values;
    const auto pad = static_cast<std::size_t>(padOf(item.setting));
    switch (item.setting) {
    case OptionSetting::InvertP1:
    case OptionSetting::InvertP2:
        v.invertCamera.at(pad) = on;
        break;
    case OptionSetting::AutoAdjustP1:
    case OptionSetting::AutoAdjustP2:
        v.autoAdjustCamera.at(pad) = on;
        break;
    case OptionSetting::VibrationP1:
    case OptionSetting::VibrationP2:
        v.vibration.at(pad) = on;
        // Turning a pad's vibration on rumbles it briefly.
        if (on && m_hooks.vibrate) {
            m_hooks.vibrate(static_cast<int>(pad), kVibrationStrength, kVibrationMs);
        }
        break;
    case OptionSetting::Merge:
        v.splitScreenMerge = on;
        break;
    case OptionSetting::Subtitles:
        v.subtitles = on;
        break;
    case OptionSetting::ProLogic:
        v.proLogic = on;
        break;
    case OptionSetting::Video:
        // Only accept (2 Normal, 3 Wide) selects the mode.
        if (!confirmed) {
            return;
        }
        v.wideVideo = !on;
        break;
    case OptionSetting::RestoreDefault:
        if (!confirmed || !on) {
            return;
        }
        v = OptionValues::defaults(v.wideVideo);
        break;
    default:
        return;
    }
    changed();
}

void OptionMenu::applyBar(const OptionItem& item) {
    if (item.setting == OptionSetting::Music) {
        m_values.musicVolume = item.bar;
    } else {
        m_values.soundFxVolume = item.bar;
    }
    changed();
}

void OptionMenu::changed() {
    if (m_hooks.apply) {
        m_hooks.apply(m_values);
    }
    if (m_pageOpen && static_cast<OptionPage>(m_title) == OptionPage::Audio && m_hooks.previewMusic) {
        m_hooks.previewMusic(true, m_values.musicVolume);
    }
}

void OptionMenu::beginEdit() {
    for (const std::unique_ptr<OptionItem>& item : page()) {
        item->savedChoice = item->choice;
        item->savedBar = item->bar;
        item->savedBrightness = m_values.brightness;
    }
}

void OptionMenu::revert() {
    for (const std::unique_ptr<OptionItem>& item : page()) {
        switch (item->kind) {
        case OptionItemKind::Choice:
            // Confirm-mode items were never applied by a move: only their answer goes back.
            item->choice = item->savedChoice;
            refreshAnswers(*item);
            if (!item->confirm) {
                applyChoice(*item, false);
            }
            break;
        case OptionItemKind::VolumeBar:
            item->bar = item->savedBar;
            applyBar(*item);
            break;
        case OptionItemKind::Brightness:
            item->bar = item->savedBar;
            m_values.brightness = item->savedBrightness;
            changed();
            break;
        }
    }
}

void OptionMenu::update(const GuiFrame& frame) {
    for (TextWidget& title : m_titles) {
        title.update(frame);
    }
    m_heading.update(frame);
    m_hint.update(frame);
    if (m_pageOpen) {
        for (const std::unique_ptr<OptionItem>& item : page()) {
            item->labelText.update(frame);
            item->answersText.update(frame);
        }
    }
}

void OptionMenu::setFade(float fade) {
    m_fade = fade;
    for (TextWidget& title : m_titles) {
        title.setFade(fade);
    }
    m_heading.setFade(fade);
    m_hint.setFade(fade);
    for (auto& items : m_pages) {
        for (const std::unique_ptr<OptionItem>& item : items) {
            item->labelText.setFade(fade);
            item->answersText.setFade(fade);
        }
    }
}

void OptionMenu::render(const GuiCanvas& canvas) const {
    if (!m_open) {
        return;
    }
    if (!m_pageOpen) {
        for (const TextWidget& title : m_titles) {
            title.render(canvas);
        }
        return;
    }
    const OptionPage id = static_cast<OptionPage>(m_title);
    if (id == OptionPage::Camera || id == OptionPage::Audio || id == OptionPage::Vibration) {
        m_heading.render(canvas);
    }
    const auto alpha = static_cast<std::uint8_t>(std::lround(255.0F * m_fade));
    for (const std::unique_ptr<OptionItem>& item : page()) {
        item->labelText.render(canvas);
        if (item->kind == OptionItemKind::Choice) {
            item->answersText.render(canvas);
            continue;
        }
        // A bar: its frame centred on its middle, then the back and the fill from its left edge.
        const float width = graphics::OverlayCamera::guiWidthToOverlay(kBarWidth);
        m_barFrame.setup(BaseWidgetSetup{.x = item->barX + (kBarWidth / 2.0F),
                                         .y = item->barY,
                                         .height = kBarHeight,
                                         .width = width,
                                         .colour = graphics::Rgba{255, 255, 255, alpha}});
        m_barFrame.render(canvas);
        m_barBack.setup(BaseWidgetSetup{.x = item->barX + kBarShiftX,
                                        .y = item->barY + kBarShiftY,
                                        .height = kBarHeight,
                                        .width = width,
                                        .colour = graphics::Rgba{kBarBack.r, kBarBack.g, kBarBack.b, alpha},
                                        .anchor = SpriteAnchor::Left});
        m_barBack.render(canvas);
        if (item->bar > 0.0F) {
            m_barFill.setup(BaseWidgetSetup{.x = item->barX + kBarShiftX,
                                            .y = item->barY + kBarShiftY,
                                            .height = kBarHeight,
                                            .width = width * item->bar,
                                            .colour = graphics::Rgba{255, 255, 255, alpha},
                                            .anchor = SpriteAnchor::Left});
            m_barFill.render(canvas);
        }
        if (item->kind == OptionItemKind::Brightness) {
            // The squares: the brightness's grey, then black; and the hint.
            const auto grey = static_cast<std::uint8_t>(std::clamp(m_values.brightness, 0, 255));
            const std::array<graphics::Rgba, 2> colours{graphics::Rgba{grey, grey, grey, alpha},
                                                        graphics::Rgba{0, 0, 0, alpha}};
            for (std::size_t i = 0; i < m_squares.size(); ++i) {
                BaseWidget& square = m_squares.at(i);
                square.setup(BaseWidgetSetup{.x = kSquareX.at(i),
                                             .y = m_layout.squaresY,
                                             .height = kSquareHeight * m_layout.squareScale,
                                             .width = kSquareWidth * m_layout.squareScale,
                                             .colour = colours.at(i)});
                square.render(canvas);
            }
            m_hint.render(canvas);
        }
    }
}

std::vector<std::string> OptionMenu::lines() const {
    std::vector<std::string> out;
    if (!m_open) {
        return out;
    }
    if (!m_pageOpen) {
        for (const TextWidget& title : m_titles) {
            out.push_back(title.text());
        }
        return out;
    }
    const OptionPage id = static_cast<OptionPage>(m_title);
    if (id == OptionPage::Camera || id == OptionPage::Audio || id == OptionPage::Vibration) {
        out.push_back(m_heading.text());
    }
    for (const std::unique_ptr<OptionItem>& item : page()) {
        out.push_back(item->labelText.text());
        if (item->kind == OptionItemKind::Choice) {
            out.push_back(item->answersText.text());
        } else if (item->kind == OptionItemKind::Brightness) {
            out.push_back(m_hint.text());
        }
    }
    return out;
}

} // namespace coney::gui
