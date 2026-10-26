// SPDX-License-Identifier: GPL-3.0-or-later
// The Options screen. Past the 500-line guide because one class stands for the original's OptionMenu, OptionPage and
// its three item classes (YesNoBox, StatBarItem, LightingItem), whose commands share one set of values and hooks; the
// layout table (option_layout.cpp) and the bars' drawing (option_bar.cpp) live apart.
#include "gui/pause_menu/option_menu.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gui/colour_table.h"
#include "gui/pause_menu/option_bar.h"

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
// Less than this from an end counts as at the end. Coney's choice: the original compares with 0 and 1 exactly, but
// its FPU rounds toward zero, so the same exact compare on another FPU could take one more step at an end.
constexpr float kBarEpsilon = 0.001F;

// A colour as the markup's colour tag.
// @coney-only Coney draws a choice's answers as one marked-up text; the original keeps three text widgets
std::string colourTag(graphics::Rgba colour) {
    return std::format("<COLOR {:02x}{:02x}{:02x}{:02x}>", colour.r, colour.g, colour.b, colour.a);
}

// The pad a per-pad setting belongs to: 1 for P2's, else 0.
// @coney-only Coney's settings name their pad; the original passes the pad to the setter as its owner (+0x240)
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
    OptionValues values; // Coney's choice: no field for W_GameState + 0x454, which the original also clears
    values.wideVideo = wideVideo;
    return values;
}

OptionMenu::OptionMenu() { // Coney's choice: no music handle (+0x45d4 = -1); the hook runs the loop
    for (BaseWidget& square : m_squares) {
        square.setVisible(true);
    }
}

std::string_view OptionMenu::string(std::uint32_t id) const { return m_strings != nullptr ? m_strings->get(id) : ""; }

void OptionMenu::playCue(int cue) const {
    if (m_hooks.playCue) {
        m_hooks.playCue(cue);
    }
}

void OptionMenu::setSpriteBatch(graphics::SpriteBatch* batch) {
    m_sheet = batch;
    for (BaseWidget& square : m_squares) {
        square.setBatch(batch);
    }
}

void OptionMenu::open(const GlobalStrings* strings, MenuInput& input, Hooks hooks, const OptionValues& values,
                      const OptionLayout& layout, int level, std::uint64_t nowMs) {
    namespace s = option_strings;
    using enum OptionPage;
    m_strings = strings;
    m_input = &input;
    m_openedMs = nowMs;
    m_hooks = std::move(hooks);
    m_values = values;
    m_layout = layout;
    m_level = level;
    input.focus(nowMs);

    // The seven titles, left-aligned one step apart in big_font.
    for (std::size_t i = 0; i < kOptionPages; ++i) {
        m_titles.at(i).setup(TextWidgetSetup{.x = kX,
                                             .y = layout.titleY + (layout.titleStep * static_cast<float>(i)),
                                             .scale = kTitleSize,
                                             .colour = kDimGrey,
                                             .fontSlot = kTitleFontSlot});
        m_titles.at(i).setText(string(s::kTitles.at(i)));
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
    m_hint.setText(string(s::kLightingHint));

    // Every page empty, on its first item (OptionPage_Init).
    for (auto& items : m_pages) {
        items.clear();
    }

    // Adds a choice (YesNoBox_Setup): label at (x, y), answers at (ax, ay), showing the current value.
    const auto addChoice = [this](OptionPage pageId, OptionSetting setting, std::uint32_t label,
                                  std::array<std::uint32_t, 2> answers, std::array<float, 4> place, float labelScale) {
        auto item = std::make_unique<OptionItem>();
        item->setting = setting;
        item->kind = OptionItemKind::Choice;
        item->label = label;
        item->answers = answers;
        item->labelText.setup(TextWidgetSetup{
            .x = place[0], .y = place[1], .scale = labelScale, .colour = kDimGrey, .fontSlot = kLabelFontSlot});
        item->labelText.setText(string(label));
        item->answersText.setup(TextWidgetSetup{
            .x = place[2], .y = place[3], .scale = kLabelSize, .colour = kMenuGrey, .fontSlot = kAnswersFontSlot});
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
            // Starts on No (`+0x4c0` = 1).
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
    };

    // Adds a bar (StatBarItem, or LightingItem for the brightness) at the value it edits.
    const auto addBar = [this](OptionPage pageId, OptionSetting setting, std::uint32_t label, float labelY,
                               float barY) {
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
    };

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
        addChoice(Camera, camera.at(i).first, camera.at(i).second, {s::kOn, s::kOff},
                  {kX, y, answersX + l.cameraAnswersExtra, y}, kLabelSize);
    }
    // Audio: the two volume bars, then Pro Logic II.
    addBar(Audio, OptionSetting::Music, s::kMusic, l.volumeRows[0], l.volumeRows[1]);
    addBar(Audio, OptionSetting::SoundFx, s::kSoundFx, l.volumeRows[2], l.volumeRows[3]);
    addChoice(Audio, OptionSetting::ProLogic, s::kProLogic, {s::kOn, s::kOff}, {kX, kProLogicY, answersX, kProLogicY},
              kLabelSize);
    // Video, Subtitles and Restore Default: one choice under the page's name.
    addChoice(Video, OptionSetting::Video, s::kTitles.at(3), {s::kNormal, s::kWide},
              {kX, l.titleY, kX, l.titleY + l.underAnswersOffset}, kBigLabelSize);
    addChoice(Subtitles, OptionSetting::Subtitles, s::kTitles.at(4), {s::kOn, s::kOff},
              {kX, l.titleY, kX, l.titleY + l.underAnswersOffset}, kBigLabelSize);
    // Vibration: a row per pad.
    addChoice(Vibration, OptionSetting::VibrationP1, s::kP1Controller, {s::kOn, s::kOff},
              {kX, l.rowY, answersX, l.rowY}, kLabelSize);
    addChoice(Vibration, OptionSetting::VibrationP2, s::kP2Controller, {s::kOn, s::kOff},
              {kX, l.rowY + l.rowStep, answersX, l.rowY + l.rowStep}, kLabelSize);
    addChoice(RestoreDefault, OptionSetting::RestoreDefault, s::kTitles.at(6), {s::kYes, s::kNo},
              {kX, l.titleY, kX, l.titleY + l.restoreAnswersOffset}, kBigLabelSize);

    // The title list on its first title, every page on its first item.
    m_title = 0;
    m_selected.fill(0);
    m_pageOpen = false;
    m_open = true;
    input.setDpadRepeat(true);
    colour();
    setFade(m_fade);
}

void OptionMenu::refreshAnswers(OptionItem& item) const {
    // One marked-up line, "A/B" with no space (each part starts where the last ends), the chosen answer in
    // 0x005fd310 and the other in 0x005fd320.
    const graphics::Rgba first = item.choice == 0 ? kMenuGrey : kDimGrey;
    const graphics::Rgba second = item.choice == 0 ? kDimGrey : kMenuGrey;
    item.answersText.setText(std::format("{}{}</COLOR>/{}{}</COLOR>", colourTag(first), string(item.answers[0]),
                                         colourTag(second), string(item.answers[1])));
}

void OptionMenu::colour() {
    for (std::size_t i = 0; i < kOptionPages; ++i) {
        m_titles.at(i).style().colour = i == m_title ? kMenuGrey : kDimGrey;
    }
    for (std::size_t p = 0; p < kOptionPages; ++p) {
        const std::vector<std::unique_ptr<OptionItem>>& items = m_pages.at(p);
        for (std::size_t i = 0; i < items.size(); ++i) {
            const bool selected = m_pageOpen && p == m_title && i == m_selected.at(p);
            items.at(i)->labelText.style().colour = selected ? kMenuGrey : kDimGrey;
        }
    }
}

void OptionMenu::close() {
    // Coney's choice: the pages, titles and input stay until the next open() rebuilds them (OptionPage_Shutdown, the
    // titles' slot +0x6c, +0x40), and the title goes back to the first there.
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
        // A refused move stops the d-pad's auto-repeat until the next move (`+0x45cc`, read by OptionMenu_Update).
        const bool refused = (up && m_title == 0) || (!up && m_title + 1 >= kOptionPages);
        m_input->setDpadRepeat(!refused);
        if (refused) {
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
        playCue(kRefusedCue);
        return false;
    case MenuCommand::Accept:
        // The page opens on the item it was left on, remembering its values; Restore Default is on No again.
        m_pageOpen = true;
        if (static_cast<OptionPage>(m_title) == OptionPage::RestoreDefault && !page().empty()) {
            page().front()->choice = 1;
            refreshAnswers(*page().front());
        }
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
    // Coney's choice: no discard box (`+0x210`), which no code opens, so every command goes to the page.
    std::vector<std::unique_ptr<OptionItem>>& items = page();
    std::size_t& selected = m_selected.at(m_title);
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
        const bool refused = (up && selected == 0) || (!up && selected + 1 >= items.size());
        m_input->setDpadRepeat(!refused);
        if (refused) {
            playCue(kRefusedCue);
            return false;
        }
        selected = up ? selected - 1 : selected + 1;
        playCue(kMoveCue);
        colour();
        return false;
    }
    case MenuCommand::Left:
    case MenuCommand::Right:
        if (selected < items.size()) {
            itemCommand(*items.at(selected), command == MenuCommand::Right);
        }
        return false;
    case MenuCommand::Accept: {
        // The item's accept (confirm mode applies its answer), cue 8, and the page closes keeping every value.
        bool restored = false;
        if (selected < items.size() && items.at(selected)->confirm) {
            OptionItem& item = *items.at(selected);
            restored = item.setting == OptionSetting::RestoreDefault && item.choice == 0;
            applyChoice(item, true);
        }
        playCue(kAcceptCue);
        leavePage();
        if (restored) {
            // The menu reopens on the new values (`0x0050ed14`, OptionMenu_Update's close and open). Coney's
            // choice: at once, not on the next update; `items` and `selected` are not used after it.
            open(m_strings, *m_input, m_hooks, m_values, m_layout, m_level, m_openedMs);
        }
        return true;
    }
    case MenuCommand::Back:
        playCue(kBackCue);
        // Vibration stops its preview rumble first; every page but Restore Default puts back what it had when opened.
        if (id == OptionPage::Vibration && m_hooks.stopVibration) {
            m_hooks.stopVibration();
        }
        if (id != OptionPage::RestoreDefault) {
            revert();
        }
        leavePage();
        return true;
    }
    return false;
}

void OptionMenu::itemCommand(OptionItem& item, bool right) {
    // Coney's choice: the page's changed flag (+0x214) and the brightness item's 0x0050ed08 / 0x0050ed0c are not
    // kept: nothing found reads them but the discard box no code opens.
    switch (item.kind) {
    case OptionItemKind::Choice: {
        const std::size_t wanted = right ? 1 : 0;
        playCue(item.choice == wanted ? kRefusedCue : kMoveCue);
        m_input->setDpadRepeat(item.choice != wanted);
        item.choice = wanted;
        refreshAnswers(item);
        if (!item.confirm) {
            applyChoice(item, false);
        }
        return;
    }
    case OptionItemKind::VolumeBar:
    case OptionItemKind::Brightness: {
        const bool atEnd = right ? item.bar >= 1.0F - kBarEpsilon : item.bar <= kBarEpsilon;
        playCue(atEnd ? kRefusedCue : kMoveCue);
        m_input->setDpadRepeat(!atEnd);
        item.bar = std::clamp(item.bar + (right ? kBarStep : -kBarStep), 0.0F, 1.0F);
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
        // Turning a pad's vibration on rumbles it briefly; a later preview replaces this one (Hooks::vibrate).
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
        // Only accept (2 Normal, 3 Wide) selects the mode. Coney's choice: only the value is set. The original then
        // switches the device's mode and closes and reopens the pause and Options menus for the new layout
        // (OptionMenu_OnVideoChoice), so the player sees the menus fade in again on Options with Lighting selected;
        // Coney has no video mode to switch, so nothing is reopened.
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
    // Coney's choice: no wait for the sheets to load (+0x10, OptionMenu_IsLoaded); the pause mode loads them first.
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
    // Coney's choice: drawn once open, with no load wait (+0x10), and with no discard box (+0x210).
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
    graphics::SpriteBatch* parts = canvas.textBatch ? canvas.textBatch(kLabelFontSlot) : nullptr;
    const std::size_t selected = m_selected.at(m_title);
    const std::vector<std::unique_ptr<OptionItem>>& items = page();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const OptionItem& item = *items.at(i);
        item.labelText.render(canvas);
        if (item.kind == OptionItemKind::Choice) {
            item.answersText.render(canvas);
            continue;
        }
        // A bar: no frame, its back, then its fill in the label's colour (the selected item's 0x005fd310, the
        // others' 0x005fd320) at the menu's alpha, where its first update put them.
        if (const std::optional<OptionBarPlace> place =
                optionBarPlace(m_sheet, item.barX, item.barY, m_layout.barHeightCut)) {
            const graphics::Rgba label = i == selected ? kMenuGrey : kDimGrey;
            drawOptionBar(parts, *place, item.bar, graphics::Rgba{label.r, label.g, label.b, alpha});
        }
        if (item.kind == OptionItemKind::Brightness) {
            // The squares: the brightness's grey, then black; and the hint.
            const auto grey = static_cast<std::uint8_t>(std::clamp(m_values.brightness, 0, 255));
            const std::array<graphics::Rgba, 2> colours{graphics::Rgba{grey, grey, grey, alpha},
                                                        graphics::Rgba{0, 0, 0, alpha}};
            for (std::size_t s = 0; s < m_squares.size(); ++s) {
                BaseWidget& square = m_squares.at(s);
                square.setup(BaseWidgetSetup{.x = kSquareX.at(s),
                                             .y = m_layout.squaresY,
                                             .height = kSquareHeight * m_layout.squareScale,
                                             .width = kSquareWidth * m_layout.squareScale,
                                             .colour = colours.at(s)});
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
