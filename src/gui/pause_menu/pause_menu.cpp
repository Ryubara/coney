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

graphics::Rgba pauseLevelColour(int level) { return {}; }

void PauseMenu::setSoundSink(std::function<void(int cue)> playCue) {}

std::string_view PauseMenu::string(std::uint32_t id) const { return m_strings != nullptr ? m_strings->get(id) : ""; }

void PauseMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void PauseMenu::open(const PauseMenuSetup& setup, std::uint64_t nowMs) {}

void PauseMenu::animate(std::uint64_t nowMs) {}

void PauseMenu::fadeOut(std::uint64_t nowMs) {}

float PauseMenu::worldTint(std::uint64_t nowMs) const { return {}; }

void PauseMenu::update(const GuiFrame& frame) {}

void PauseMenu::onGridCommand(MenuCommand command, std::uint64_t nowMs) {}

void PauseMenu::onScreenCommand(MenuCommand command, std::uint64_t nowMs) {}

void PauseMenu::onBoxCommand(MenuCommand command, std::uint64_t nowMs) {}

void PauseMenu::selectItem(PauseItem item, std::uint64_t nowMs) {}

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

void PauseMenu::closeScreen() {}

void PauseMenu::ask(std::uint32_t question) {
    m_yesNo.open(string(question), string(pause_strings::kYes), string(pause_strings::kNo));
    refreshScreen();
}

void PauseMenu::setUsage(std::uint32_t id) {}

std::size_t PauseMenu::screenEntries() const { return {}; }

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
void PauseMenu::refreshScreen() {}

std::vector<std::string> PauseMenu::screenLines() const { return {}; }

void PauseMenu::render(const GuiCanvas& canvas) const {}

} // namespace coney::gui
