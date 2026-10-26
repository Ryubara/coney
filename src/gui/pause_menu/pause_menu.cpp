// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/pause_menu.h"

#include <algorithm>
#include <utility>

#include "core/pad.h"
#include "gui/colour_table.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// Coney's stand-in layout of the screens above the grid (docs/research/pause.md#open-questions): the headers' and
// lists' look is not researched.
constexpr float kCentreX = 0.5F;
constexpr float kHeaderScale = 1.15F;
constexpr float kChoiceY = 0.40F;                         // the Restart and Quit screens' first choice
constexpr float kChoicePitch = 0.06F;                     // between their choices
constexpr float kHelpY = 0.56F;                           // the selected choice's description
constexpr float kListGap = 0.05F;                         // from an objectives header to its first line
constexpr float kListPitch = 0.04F;                       // between objective lines
constexpr graphics::Rgba kCurrentGold{230, 180, 40, 255}; // the selected current-objectives header
constexpr graphics::Rgba kBonusPurple{150, 70, 190, 255}; // the selected bonus header
constexpr graphics::Rgba kChosenWhite = graphics::kWhite; // a selected choice (white at runtime)

// The colour `t` (0 to 1) of the way from `from` to `to`: each channel from × (1 − t) + to × t in floats, truncated.
// @orig 0x0017a258 Colour_LerpPacked (unknown)
graphics::Rgba blend(graphics::Rgba from, graphics::Rgba to, float t) {
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>((static_cast<float>(a) * (1.0F - t)) + (static_cast<float>(b) * t));
    };
    return graphics::Rgba{mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a)};
}

// The usage line of the Controls screen's Controller page.
constexpr std::uint32_t kUsageController = 0x1e;

// The top header's place (`PauseMenu_Open`): its row (0.18 in 16:9), box width, text offset.
constexpr float kTopHeaderY = 0.22F;
constexpr float kWideTopHeaderY = 0.18F;

// The level lists of pauseLevelColour(), in the order they are tested, and the colour of each: the original builds
// them on the stack (5, 5, 6 and 10 levels; 80 and 83 in the last are never reached, the third list holds them).
constexpr std::array kGreenLevels{92, 51, 31, 9, 55};
constexpr std::array kBlueLevels{20, 11, 34, 54, 3};
constexpr std::array kBrownLevels{80, 82, 83, 93, 7, 87};
constexpr std::array kGoldLevels{99, 80, 81, 83, 86, 84, 2, 52, 5, 14};
constexpr graphics::Rgba kGreen{37, 100, 60, 255};
constexpr graphics::Rgba kBlue{38, 61, 94, 255};
constexpr graphics::Rgba kBrown{76, 53, 44, 255};
constexpr graphics::Rgba kGold{186, 139, 53, 255};

} // namespace

graphics::Rgba pauseLevelColour(int level) {
    if (std::ranges::contains(kGreenLevels, level)) {
        return kGreen;
    }
    if (std::ranges::contains(kBlueLevels, level)) {
        return kBlue;
    }
    if (std::ranges::contains(kBrownLevels, level)) {
        return kBrown;
    }
    if (std::ranges::contains(kGoldLevels, level)) {
        return kGold;
    }
    return graphics::kBlack;
}

void PauseMenu::setSoundSink(std::function<void(int cue)> playCue) {
    m_playCue = std::move(playCue);
    m_yesNo.setSoundSink(m_playCue);
    m_stats.setSoundSink(m_playCue);
}

std::string_view PauseMenu::string(std::uint32_t id) const { return m_strings != nullptr ? m_strings->get(id) : ""; }

void PauseMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void PauseMenu::open(const PauseMenuSetup& setup, std::uint64_t nowMs) {
    m_setup = setup;
    m_open = true;
    m_closing = false;
    m_closed = false;
    m_outcome = PauseOutcome::Resume;
    m_pending = PauseOutcome::Resume;
    m_openMs = nowMs;
    m_cycleMs = nowMs;
    m_cycleLeg = 0;
    m_fadeOutMs = nowMs;
    m_lastFadeMs = nowMs;
    m_alpha = 255.0F;
    m_ready = false;
    m_yesNo.close();
    m_yesNo.place(YesNoPlacement{});

    // The background: centred, colour-cycled by animate() and drawn at the menu's alpha.
    m_background.setup(BaseWidgetSetup{.x = 0.5F, .y = 0.5F, .height = kBackgroundSize, .colour = kCycleColours[0]});

    // The top header: the mission's title, its circles in the level's colour (the last red), the level's name.
    m_topHeader.setup(CircledTextHeaderSetup{.y = setup.widescreen ? kWideTopHeaderY : kTopHeaderY,
                                             .factors = CircledHeaderFactors::forMode(setup.widescreen, setup.flag02)});
    m_topHeader.setCircleColour(pauseLevelColour(setup.level));
    m_topHeader.setTitle(setup.missionTitle, setup.checkpoint, setup.levelName);
    // The letters are made black only in a gold level (until the first update makes them black anyway).
    if (pauseLevelColour(setup.level) == kGold) {
        m_topHeader.setLetterColour(graphics::kBlack);
    }
    m_topHeader.setLabelColour(kDimGrey);

    // The Stats screen for the pausing player (`PauseStats_Setup`, every pause).
    (void)m_stats.setup(setup.stats, setup.difficulty, m_strings, setup.widescreen);

    // Coney's stand-in headers for the screens' names, centred on their rows.
    for (std::size_t i = 0; i < m_headers.size(); ++i) {
        m_headers.at(i).setup(TextWidgetSetup{.x = kCentreX,
                                              .y = kHeaderY.at(i),
                                              .scale = kHeaderScale,
                                              .colour = kMenuGrey,
                                              .alignment = TextAlignment::Centre,
                                              .fontSlot = kBigFontSlot});
    }

    // The grid, its first item selected, in the menus' dim grey.
    const PauseGridLayout layout = pauseGrid(setup.level);
    m_grid.setup(OptionGridSetup{
        .y = kGridY, .rows = layout.rows, .centreX = 0.5F, .moveCue = kMoveCue, .playCue = [this](int cue) {
            playCue(cue);
        }});
    for (const PauseItemEntry& entry : layout.items) {
        (void)m_grid.addItem(OptionGridItem{.text = std::string(string(entry.text)),
                                            .code = static_cast<int>(entry.code),
                                            .separator = entry.separator,
                                            .enabled = true,
                                            .colour = kDimGrey,
                                            .scale = kGridScale,
                                            .fontSlot = kGridFontSlot});
    }
    m_grid.select(0);
    m_grid.takeFocus(m_input, nowMs);

    m_usage.place(kUsageX, kUsageY, false);
    setUsage(UsageInfo::kInitialString);

    // The quit destinations, when Quit offers two (`+0x1bb0`).
    m_quitChoices.clear();
    if (isRumbleLevel(setup.level)) {
        m_quitChoices = {QuitChoice::RumbleMode, setup.rumbleFromHangout ? QuitChoice::Hangout : QuitChoice::MainMenu};
    } else if (offersHangout(setup.level)) {
        m_quitChoices = {QuitChoice::Hangout, QuitChoice::MainMenu};
    }

    m_screen = PauseScreen::Grid;
    m_selection = 0;
    if (setup.openOnOptions) {
        openOptions(nowMs);
    } else {
        refreshScreen();
    }
}

void PauseMenu::animate(std::uint64_t nowMs) {
    // The closing fade; while open, the time a fade would start from is this update's.
    if (m_closing) {
        fadeOut(nowMs);
    } else {
        m_fadeOutMs = nowMs;
    }
    // Stand-in: while closing, the original also turns both views' blur effects off (their `+0x214`); Coney's
    // paused frame draws none.

    // The background's colour: the first update more than kCycleLegMs after the leg began moves on to the next
    // colour and starts that leg there, at 0; otherwise the leg's share of the time blends to the next colour.
    float t = 0.0F;
    const std::uint64_t inLeg = nowMs - m_cycleMs;
    if (inLeg > kCycleLegMs) {
        m_cycleLeg = (m_cycleLeg + 1) % kCycleColours.size();
        m_cycleMs = nowMs;
    } else {
        t = static_cast<float>(inLeg) / static_cast<float>(kCycleLegMs);
    }
    graphics::Rgba colour =
        blend(kCycleColours.at(m_cycleLeg), kCycleColours.at((m_cycleLeg + 1) % kCycleColours.size()), t);

    // The background's alpha: the original also fades it in over 3 s here, but its render replaces the alpha with
    // the menu's before every draw (`BaseWidget_SetAlpha`, `0x001a2638`), so only the menu's alpha ever shows.
    colour.a = static_cast<std::uint8_t>(m_alpha);
    m_background.setColour(colour);

    // The menu's alpha on every widget (the render's alpha calls).
    const float fade = m_alpha / 255.0F;
    m_topHeader.setFade(fade);
    m_stats.setFade(fade);
    for (TextWidget& header : m_headers) {
        header.setFade(fade);
    }
    for (const std::unique_ptr<TextWidget>& line : m_lines) {
        line->setFade(fade);
    }
    m_usage.setFade(fade);
    m_yesNo.setFade(fade);
    m_controls.setFade(fade);
    m_options.setFade(fade);
    m_grid.setFade(fade);
}

void PauseMenu::fadeOut(std::uint64_t nowMs) {
    const std::uint64_t fading = nowMs - m_fadeOutMs;
    if (fading < kFadeOutMs) {
        // Inside the fade: the alpha falls linearly from 255, cut to a whole byte.
        m_lastFadeMs = nowMs;
        m_alpha = static_cast<float>(
            static_cast<int>((1.0F - (static_cast<float>(fading) / static_cast<float>(kFadeOutMs))) * 255.0F));
    } else if (m_alpha == 0.0F) {
        // The hold counts from the last update inside the fade.
        if (nowMs - m_lastFadeMs > kHoldMs) {
            m_closing = false;
            m_closed = true;
        }
    } else {
        // The first update past the fade only clears what is left of the alpha.
        m_alpha = 0.0F;
    }
}

float PauseMenu::worldTint(std::uint64_t nowMs) const {
    if (!m_open) {
        return 0.0F;
    }
    // Stand-in: the original blends both views' tint from the level's current tint to black; Coney's tint is the
    // share of the way to black, from none.
    return std::min(1.0F, static_cast<float>(nowMs - m_openMs) / static_cast<float>(kTintMs));
}

void PauseMenu::update(const GuiFrame& frame) {
    if (!m_open) {
        return;
    }
    const std::uint64_t nowMs = frame.timeMs;
    // Stand-in: the original waits for its headers and background to be ready (or 10 s) before it draws; Coney's
    // widgets are ready at once, so the menu draws from the first update on.
    m_ready = true;
    animate(nowMs);
    for (TextWidget& header : m_headers) {
        header.update(frame);
    }
    for (const std::unique_ptr<TextWidget>& line : m_lines) {
        line->update(frame);
    }
    m_topHeader.update(frame);
    m_stats.update(frame);
    m_grid.update(frame);
    m_usage.update(frame);
    if (m_screen == PauseScreen::Controls) {
        m_controls.update(frame);
    }
    if (m_screen == PauseScreen::Options) {
        m_options.update(frame);
    }
    m_yesNo.update(frame);

    // Nothing is taken while closing or once closed. START closes from anywhere once more than the delay has passed;
    // the other commands go to the box, the open screen or the grid (the command handler's dispatch).
    if (!m_closing && !m_closed && frame.pad != nullptr) {
        if (frame.pad->pressed(pad::kStart) && nowMs - m_openMs > kStartDelayMs) {
            m_closing = true;
        } else if (const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, nowMs)) {
            if (m_yesNo.isOpen()) {
                onBoxCommand(*command, nowMs);
            } else if (m_screen != PauseScreen::Grid) {
                onScreenCommand(*command, nowMs);
            } else {
                onGridCommand(*command, nowMs);
            }
        }
    }

    // The Controls screen's usage line follows its page, as the render sets it: the Tutorial's 0x19, the
    // Controller's 0x1e, the two entries' 0x1a.
    if (m_screen == PauseScreen::Controls) {
        switch (m_controls.page()) {
        case ControlPage::Tutorial:
            setUsage(pause_strings::kUsageObjectives);
            break;
        case ControlPage::Controller:
            setUsage(kUsageController);
            break;
        case ControlPage::None:
            setUsage(UsageInfo::kInitialString);
            break;
        }
    }
}

void PauseMenu::onGridCommand(MenuCommand command, std::uint64_t nowMs) {
    switch (command) {
    case MenuCommand::Accept:
        playCue(kAcceptCue);
        if (const std::optional<int> code = m_grid.handle(command)) {
            selectItem(static_cast<PauseItem>(*code), nowMs);
        }
        return;
    case MenuCommand::Back:
        // Triangle resumes (confirmed at runtime).
        playCue(kBackCue);
        m_closing = true;
        return;
    default:
        (void)m_grid.handle(command);
        return;
    }
}

void PauseMenu::onScreenCommand(MenuCommand command, std::uint64_t nowMs) {
    // The Controls screen takes every command; leaving it gives the grid its input back.
    if (m_screen == PauseScreen::Controls) {
        if (m_controls.handle(command, nowMs)) {
            closeScreen();
            m_grid.takeFocus(m_input, nowMs);
        }
        return;
    }
    // So does the Stats screen; back closes it.
    if (m_screen == PauseScreen::Stats) {
        if (m_stats.handle(command)) {
            closeScreen();
        }
        return;
    }
    // So does the Options screen, which plays its own cues (the menu plays none while it is open).
    if (m_screen == PauseScreen::Options) {
        if (m_options.handle(command)) {
            closeScreen();
            m_grid.takeFocus(m_input, nowMs);
        }
        return;
    }
    switch (command) {
    case MenuCommand::Back:
        playCue(kBackCue);
        closeScreen();
        return;
    case MenuCommand::Up:
    case MenuCommand::Down: {
        // The screen's selection, without wrapping: an end refuses, and so does a Rumble level's objectives screen,
        // which stays on the overview.
        const std::size_t entries = screenEntries();
        const bool up = command == MenuCommand::Up;
        const bool fixed = m_screen == PauseScreen::Objectives && isRumbleLevel(m_setup.level);
        if (fixed || entries == 0 || (up && m_selection == 0) || (!up && m_selection + 1 >= entries)) {
            playCue(kRefusedCue);
            return;
        }
        m_selection = up ? m_selection - 1 : m_selection + 1;
        playCue(kMoveCue);
        refreshScreen();
        return;
    }
    case MenuCommand::Accept:
        if (m_screen == PauseScreen::Restart) {
            playCue(kAcceptCue);
            // The choice is set when the box opens; Yes then only closes.
            m_pending = m_selection == 0 ? PauseOutcome::RestartLevel : PauseOutcome::RestartCheckpoint;
            ask(m_selection == 0 ? pause_strings::kReplayQuestion : pause_strings::kCheckpointQuestion);
        } else if (m_screen == PauseScreen::Quit) {
            playCue(kAcceptCue);
            switch (m_quitChoices.at(m_selection)) {
            case QuitChoice::Hangout:
                m_pending = PauseOutcome::QuitToHangout;
                break;
            case QuitChoice::MainMenu:
                m_pending = PauseOutcome::QuitToMainMenu;
                break;
            case QuitChoice::RumbleMode:
                m_pending =
                    m_setup.rumbleFromHangout ? PauseOutcome::QuitToRumbleHangout : PauseOutcome::QuitToRumbleQuick;
                break;
            }
            ask(pause_strings::kQuitQuestion);
        }
        // The Objectives screen does nothing on accept.
        (void)nowMs;
        return;
    case MenuCommand::Left:
    case MenuCommand::Right:
        // A screen takes no sideways move.
        playCue(kRefusedCue);
        return;
    }
}

void PauseMenu::onBoxCommand(MenuCommand command, std::uint64_t nowMs) {
    const std::optional<YesNoAnswer> answer = m_yesNo.handle(command);
    if (!answer) {
        return;
    }
    if (*answer == YesNoAnswer::Yes) {
        // `PauseMenu_OnConfirm(2)`: the confirmed choice closes the menu. It was picked when the box opened (the
        // original picks the handler from the same flags now). Stand-in: the co-op quit's handler is never chosen.
        m_outcome = m_pending;
        m_closing = true;
    }
    (void)nowMs;
    // No and back forget the quit and restart choices; the screen behind the box stays.
    m_pending = PauseOutcome::Resume;
    refreshScreen();
}

void PauseMenu::selectItem(PauseItem item, std::uint64_t nowMs) {
    // Stand-in: the original shows the top header's label here and zeroes the lists' reveal timer
    // (`0x0050ee20`-`0x0050ee2c`); Coney's headers and lists are refreshScreen()'s and are not animated.
    switch (item) {
    case PauseItem::Objectives:
        setUsage(pause_strings::kUsageObjectives);
        openScreen(PauseScreen::Objectives, isRumbleLevel(m_setup.level) ? 2 : 0);
        return;
    case PauseItem::Stats:
        // Only a Warrior has a Stats screen; for anyone else nothing opens, but the usage line is changed all the same.
        setUsage(pause_strings::kUsageObjectives);
        if (m_stats.available()) {
            openScreen(PauseScreen::Stats, 0);
        }
        return;
    case PauseItem::Options:
        // Stand-in: the screen's name is Coney's header line (refreshScreen()).
        openOptions(nowMs);
        return;
    case PauseItem::Controls:
        // `ControlMenuHUD_Open`, with the pausing player's input. Stand-in: the original opens it once, when the menu
        // opens, and Coney here each time; the screen's name is Coney's header line.
        m_controls.open(
            m_strings, m_input, [this](int cue) { playCue(cue); }, nowMs,
            ControlLayout::forMode(m_setup.language, m_setup.widescreen, m_setup.flag02));
        openScreen(PauseScreen::Controls, 0);
        return;
    case PauseItem::Restart:
        // Stand-in: the Restart screen's two choices and their help are Coney's centred texts (refreshScreen()).
        if (isRumbleLevel(m_setup.level)) {
            m_pending = PauseOutcome::RestartLevel;
            ask(pause_strings::kReplayQuestion);
        } else {
            openScreen(PauseScreen::Restart, 1);
        }
        return;
    case PauseItem::Resume:
        m_closing = true;
        return;
    case PauseItem::Quit:
        // Stand-in: the co-op quit (Player 2 / All) is not offered, as Coney has one player; the two destinations
        // are Coney's centred texts (refreshScreen()).
        if (m_quitChoices.empty()) {
            m_pending = PauseOutcome::QuitToMainMenu;
            ask(pause_strings::kQuitQuestion);
        } else {
            openScreen(PauseScreen::Quit, 0);
        }
        return;
    }
}

void PauseMenu::openOptions(std::uint64_t nowMs) {
    OptionMenu::Hooks hooks = m_optionHooks;
    hooks.playCue = [this](int cue) { playCue(cue); };
    m_options.open(m_strings, m_input, std::move(hooks), m_setup.options,
                   OptionLayout::forMode(m_setup.language, m_setup.widescreen, m_setup.flag02), m_setup.level, nowMs);
    m_options.setFade(m_alpha / 255.0F);
    openScreen(PauseScreen::Options, 0);
}

void PauseMenu::openScreen(PauseScreen screen, std::size_t selection) {
    m_screen = screen;
    m_selection = selection;
    refreshScreen();
}

void PauseMenu::closeScreen() {
    // Stand-in: the original puts each screen's header texts back here (Coney's are refreshScreen()'s), and an
    // Options screen opened by ShowOptionMenu (the game state's `+0x11c`) closes the whole menu; Coney's pause never
    // opens on Options (PauseMode), so that is not modelled.
    m_screen = PauseScreen::Grid;
    m_selection = 0;
    setUsage(UsageInfo::kInitialString);
    refreshScreen();
}

void PauseMenu::ask(std::uint32_t question) {
    m_yesNo.open(string(question), string(pause_strings::kYes), string(pause_strings::kNo));
    refreshScreen();
}

void PauseMenu::setUsage(std::uint32_t id) { m_usage.setLegend(string(id)); }

std::size_t PauseMenu::screenEntries() const {
    switch (m_screen) {
    case PauseScreen::Grid:
        return 0;
    case PauseScreen::Objectives:
        return m_headers.size();
    case PauseScreen::Options:
    case PauseScreen::Stats:
    case PauseScreen::Controls:
        // These screens take the commands themselves.
        return 0;
    case PauseScreen::Restart:
        return 2;
    case PauseScreen::Quit:
        return m_quitChoices.size();
    }
    return 0;
}

void PauseMenu::addLine(std::string_view text, float y, graphics::Rgba colour, int fontSlot, float scale) {
    auto line = std::make_unique<TextWidget>();
    line->setup(TextWidgetSetup{.x = kCentreX,
                                .y = y,
                                .scale = scale,
                                .colour = colour,
                                .alignment = TextAlignment::Centre,
                                .fontSlot = fontSlot});
    line->setText(text);
    line->setFade(m_alpha / 255.0F);
    m_lines.push_back(std::move(line));
}

// Stand-in for the original's three IconTextHeaders and checklists (docs/research/pause.md#open-questions).
void PauseMenu::refreshScreen() {
    namespace s = pause_strings;
    m_lines.clear();

    // The top header's label: string 0xe7 (PauseMenu_Open picks 0xe8 for a checklist holding `<ROBJ_N>` but then sets
    // 0xe7 whatever it picked, 0x001dc984). The grid hides it; Coney's stand-in hides it everywhere (where the label
    // sits on the other screens is an open question) and names the Options and Controls screens with its own header
    // line.
    m_topHeader.setLabel(string(s::kTitleA));
    m_topHeader.setLabelVisible(false);
    std::uint32_t top = 0;
    if (m_screen == PauseScreen::Options) {
        top = s::kOptionsHeader;
    } else if (m_screen == PauseScreen::Controls) {
        top = s::kControlsHeader;
    }
    m_headers.at(0).setVisible(top != 0);
    m_headers.at(0).setText(top != 0 ? string(top) : std::string_view());
    m_headers.at(0).style().colour = kMenuGrey;
    m_headers.at(1).setVisible(false);
    m_headers.at(2).setVisible(false);

    // The screen behind an open Yes/No box is hidden (Coney's choice: the question sits where its lines are).
    if (m_yesNo.isOpen()) {
        return;
    }
    switch (m_screen) {
    case PauseScreen::Grid:
        return;
    case PauseScreen::Objectives: {
        // The three headers stacked, the selected one coloured and expanded to its list.
        static constexpr std::array<std::uint32_t, 3> kNames{s::kCurrentObjectives, s::kBonusObjectives, s::kOverview};
        for (std::size_t i = 0; i < m_headers.size(); ++i) {
            TextWidget& header = m_headers.at(i);
            header.setVisible(true);
            header.setText(string(kNames.at(i)));
            graphics::Rgba colour = kMenuGrey;
            if (i == m_selection) {
                colour = i == 1 ? kBonusPurple : kCurrentGold;
            }
            header.style().colour = colour;
        }
        // An empty current or bonus list shows None (the update adds it); the overview shows nothing.
        const std::vector<std::string>& list = m_setup.objectives.at(m_selection);
        float y = kHeaderY.at(m_selection) + kListGap;
        if (list.empty() && m_selection < 2) {
            addLine(string(s::kNone), y, kMenuGrey, kTextFontSlot, 1.0F);
        }
        for (const std::string& line : list) {
            addLine(line, y, kMenuGrey, kTextFontSlot, 1.0F);
            y += kListPitch;
        }
        return;
    }
    case PauseScreen::Controls:
    case PauseScreen::Options:
    case PauseScreen::Stats:
        // ControlMenu, OptionMenu and PauseStats draw themselves.
        return;
    case PauseScreen::Restart:
    case PauseScreen::Quit: {
        // Two choices, the selected one white with its description under them.
        std::vector<std::pair<std::uint32_t, std::uint32_t>> choices; // text, description (0: none)
        if (m_screen == PauseScreen::Restart) {
            choices = {{s::kRestartLevel, s::kRestartLevelHelp}, {s::kRestartCheckpoint, s::kRestartCheckpointHelp}};
        } else {
            for (const QuitChoice choice : m_quitChoices) {
                switch (choice) {
                case QuitChoice::Hangout:
                    choices.emplace_back(s::kToHangout, s::kHangoutHelp);
                    break;
                case QuitChoice::MainMenu:
                    choices.emplace_back(s::kMainMenu, s::kMainMenuHelp);
                    break;
                case QuitChoice::RumbleMode:
                    choices.emplace_back(s::kToRumbleMode, 0);
                    break;
                }
            }
        }
        for (std::size_t i = 0; i < choices.size(); ++i) {
            addLine(string(choices.at(i).first), kChoiceY + (kChoicePitch * static_cast<float>(i)),
                    i == m_selection ? kChosenWhite : kDimGrey, kBigFontSlot, kHeaderScale);
        }
        if (const std::uint32_t help = choices.at(m_selection).second; help != 0) {
            addLine(string(help), kHelpY, kMenuGrey, kTextFontSlot, 1.0F);
        }
        return;
    }
    }
}

std::vector<std::string> PauseMenu::screenLines() const {
    if (m_screen == PauseScreen::Controls) {
        return m_controls.lines();
    }
    if (m_screen == PauseScreen::Options) {
        return m_options.lines();
    }
    if (m_screen == PauseScreen::Stats) {
        return m_stats.lines();
    }
    std::vector<std::string> lines;
    lines.reserve(m_lines.size());
    for (const std::unique_ptr<TextWidget>& line : m_lines) {
        lines.push_back(line->text());
    }
    return lines;
}

void PauseMenu::render(const GuiCanvas& canvas) const {
    // Nothing before the first update, or once the menu is closed.
    if (!m_open || !m_ready || m_closed || !visible()) {
        return;
    }
    m_background.render(canvas);
    // Stand-in: where the original draws the top header (a global widget) is not traced; Coney draws it here.
    m_topHeader.render(canvas);
    // The open screen: Options, then Controls or Stats.
    if (m_screen == PauseScreen::Options) {
        m_options.render(canvas);
    }
    if (m_screen == PauseScreen::Controls) {
        m_controls.render(canvas);
    } else if (m_screen == PauseScreen::Stats) {
        m_stats.render(canvas);
    }
    // Stand-in: the three headers and their lists are Coney's centred texts (refreshScreen()).
    for (const TextWidget& header : m_headers) {
        header.render(canvas);
    }
    for (const std::unique_ptr<TextWidget>& line : m_lines) {
        line->render(canvas);
    }
    // The usage line, the grid and the box, except over an Options screen the menu was opened on.
    if (m_screen == PauseScreen::Options && m_setup.openOnOptions) {
        return;
    }
    m_usage.render(canvas);
    m_grid.render(canvas);
    m_yesNo.render(canvas);
}

} // namespace coney::gui
