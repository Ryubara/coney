// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_new_game_screens.h"

#include <algorithm>
#include <utility>

#include "core/language.h"
#include "graphics/overlay_camera.h"
#include "gui/text_layout.h"

namespace coney::gui {

// NameKeyboard

void NameKeyboard::set(std::string_view characters, std::string_view ok, std::string_view del) {
    m_cells.clear();
    for (const char c : characters) {
        m_cells.push_back(Cell{std::string(1, c), Key::Character, c != ' '});
    }
    m_cells.push_back(Cell{std::string(ok), Key::Ok, true});
    m_cells.push_back(Cell{std::string(del), Key::Delete, true});

    // The original's rows when the cells fill them exactly, else rows of 12.
    std::size_t total = 0;
    for (const std::size_t row : kRows) {
        total += row;
    }
    m_rows.clear();
    if (m_cells.size() == total) {
        m_rows.assign(kRows.begin(), kRows.end());
    } else {
        for (std::size_t left = m_cells.size(); left > 0;) {
            const std::size_t row = std::min<std::size_t>(left, kRows.front());
            m_rows.push_back(row);
            left -= row;
        }
    }
    m_selected = 0;
    while (m_selected < m_cells.size() && !m_cells.at(m_selected).selectable) {
        ++m_selected;
    }
}

std::size_t NameKeyboard::rowStart(std::size_t row) const {
    std::size_t first = 0;
    for (std::size_t r = 0; r < row; ++r) {
        first += m_rows.at(r);
    }
    return first;
}

std::pair<std::size_t, std::size_t> NameKeyboard::cell(std::size_t index) const {
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        const std::size_t first = rowStart(row);
        if (index < first + m_rows.at(row)) {
            return {row, index - first};
        }
    }
    return {0, 0};
}

std::optional<std::size_t> NameKeyboard::nearest(std::size_t row, std::size_t column) const {
    const std::size_t first = rowStart(row);
    for (std::size_t c = std::min(column, m_rows.at(row) - 1) + 1; c > 0; --c) {
        if (m_cells.at(first + c - 1).selectable) {
            return first + c - 1;
        }
    }
    return std::nullopt;
}

bool NameKeyboard::move(MenuCommand command) {
    if (m_cells.empty()) {
        return false;
    }
    // The two jumps to OK the original's handler makes.
    if ((command == MenuCommand::Up && m_selected == kUpToOk) ||
        (command == MenuCommand::Down && m_selected == kDownToOk)) {
        m_selected = okIndex();
        return true;
    }
    const auto [row, column] = cell(m_selected);
    const std::size_t first = rowStart(row);
    std::optional<std::size_t> next;
    switch (command) {
    case MenuCommand::Left:
        for (std::size_t c = column; c > 0 && !next; --c) {
            if (m_cells.at(first + c - 1).selectable) {
                next = first + c - 1;
            }
        }
        break;
    case MenuCommand::Right:
        for (std::size_t c = column + 1; c < m_rows.at(row) && !next; ++c) {
            if (m_cells.at(first + c).selectable) {
                next = first + c;
            }
        }
        break;
    case MenuCommand::Up:
        if (row > 0) {
            next = nearest(row - 1, column);
        }
        break;
    case MenuCommand::Down:
        if (row + 1 < m_rows.size()) {
            next = nearest(row + 1, column);
        }
        break;
    default:
        break;
    }
    if (!next || *next == m_selected) {
        return false;
    }
    m_selected = *next;
    return true;
}

// PM_Create

void PmCreate::open() {
    // Init: the profile goes into the free slot.
    m_shared.session.slot = m_shared.profiles != nullptr ? m_shared.profiles->freeSlot() : std::nullopt;
    m_keyboard.set(m_shared.string(kCharactersString), m_shared.string(kOkString), m_shared.string(kDelString));
    m_nameUsed = false;
    m_keysPlaced = false;

    // The look: title, name, the keys in their rows, the hidden error and the usage line.
    placePmText(m_title, m_shared.string(kTitleString), pm_look::kX, kTitleY, pm_look::kTitleScale, pm_look::kRed,
                kBigFontSlot);
    placePmText(m_name, m_shared.session.name, pm_look::kX, kNameY, pm_look::kNameScale, pm_look::kGrey, kBigFontSlot);
    placePmText(m_error, m_shared.string(kNameUsedString), pm_look::kX, kErrorY, pm_look::kMessageScale, pm_look::kRed,
                kTextFontSlot);
    m_keys.clear();
    for (std::size_t i = 0; i < m_keyboard.cells().size(); ++i) {
        const auto [row, column] = m_keyboard.cell(i);
        auto key = std::make_unique<TextWidget>();
        placePmText(*key, m_keyboard.cells().at(i).label,
                    pm_look::kX - 0.005F + static_cast<float>(column) * kKeyColumnPitch,
                    kKeysY + static_cast<float>(row) * kKeyRowPitch, 1.0F, pm_look::kRed, kBigFontSlot);
        m_keys.push_back(std::move(key));
    }
    placePmUsage(m_usage, m_shared);
}

int PmCreate::handle(MenuCommand command) {
    std::string& name = m_shared.session.name;
    switch (command) {
    case MenuCommand::Back:
        name.clear();
        playSound(pm_cue::kBack);
        return kBack;
    case MenuCommand::Accept:
        return press();
    default:
        playSound(m_keyboard.move(command) ? pm_cue::kKeyMove : pm_cue::kRefused);
        return kStay;
    }
}

int PmCreate::press() {
    std::string& name = m_shared.session.name;
    const NameKeyboard::Cell& cell = m_keyboard.cells().at(m_keyboard.selected());
    switch (cell.key) {
    case NameKeyboard::Key::Character:
        if (name.size() >= Profile::kNameLength) {
            playSound(pm_cue::kRefused);
            return kStay;
        }
        name += cell.label == "_" ? std::string(" ") : cell.label;
        playSound(pm_cue::kCharacter);
        if (name.size() >= Profile::kNameLength) {
            m_keyboard.selectOk();
        }
        return kStay;
    case NameKeyboard::Key::Delete:
        if (name.empty()) {
            playSound(pm_cue::kRefused);
            return kStay;
        }
        name.pop_back();
        playSound(pm_cue::kDelete);
        return kStay;
    case NameKeyboard::Key::Ok:
    default:
        break;
    }
    // OK: a name with something in it that no other slot uses.
    if (name.find_first_not_of(' ') == std::string::npos) {
        playSound(pm_cue::kRefused);
        return kStay;
    }
    playSound(pm_cue::kNameOk);
    m_nameUsed = m_shared.profiles != nullptr && m_shared.profiles->nameUsed(name);
    return m_nameUsed ? kStay : 0;
}

void PmCreate::close() {
    for (TextWidget* text : {&m_title, &m_name, &m_error, &m_usage}) {
        text->shutdown();
    }
    m_keys.clear();
}

void PmCreate::draw() {
    if (m_name.text() != m_shared.session.name) {
        m_name.setText(m_shared.session.name);
    }
    m_error.setVisible(m_nameUsed);
    // DEL follows OK's word rather than its own column, so the two labels do not overlap (Coney's; the page gives the
    // cells' pitch, not the words' places).
    if (m_keys.size() >= 2 && !m_keysPlaced && m_shared.canvas.fonts) {
        const TextWidget& ok = *m_keys.at(m_keyboard.okIndex());
        m_keys.back()->style().x = ok.style().x + ok.layout(m_shared.canvas).width + kKeyColumnPitch / 2.0F;
        m_keysPlaced = true;
    }
    for (std::size_t i = 0; i < m_keys.size(); ++i) {
        m_keys.at(i)->style().colour = i == m_keyboard.selected() ? pm_look::kGrey : pm_look::kRed;
    }
    for (TextWidget* text : {&m_title, &m_name, &m_error, &m_usage}) {
        text->update(m_shared.frame);
        text->render(m_shared.canvas);
    }
    for (const std::unique_ptr<TextWidget>& key : m_keys) {
        key->render(m_shared.canvas);
    }
}

// PM_Difficulty

void PmDifficulty::setUp() {
    m_title = m_shared.string(kTitleString);
    const bool four = m_shared.profiles != nullptr && m_shared.profiles->fourthDifficultyUnlocked();
    const int count = four ? 4 : 3;
    std::vector<PmChoices::Item> items;
    items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        items.push_back({std::string(m_shared.string(kFirstString + static_cast<std::uint32_t>(i))), i});
    }
    m_choices.set(std::move(items), {}, four ? 3 : 1);
}

int PmDifficulty::accept(int code) {
    if (m_shared.state != nullptr) {
        m_shared.state->profileDifficulty = code;
    }
    return 0;
}

// PM_Light

void PmLight::open() {
    m_value = kStart;
    graphics::SpriteBatch* batch = m_shared.menuSprites;
    // The square: its width from the rectangle's shape, its left edge at x.
    float squareWidth = kSquareHeight / graphics::OverlayCamera::guiWidthToOverlay(1.0F);
    if (batch != nullptr && kSquareRect < batch->sheet().page.rects.size() && batch->sheet().texture != nullptr) {
        const graphics::UvRect& uv = batch->sheet().page.rect(kSquareRect);
        const float w = (uv.u1 - uv.u0) * static_cast<float>(batch->sheet().texture->width());
        const float h = (uv.v1 - uv.v0) * static_cast<float>(batch->sheet().texture->height());
        if (h > 0.0F) {
            squareWidth = kSquareHeight * w / h / graphics::OverlayCamera::guiWidthToOverlay(1.0F);
        }
    }
    for (BaseWidget* widget : {&m_square, &m_barBack, &m_barFill}) {
        widget->init();
        widget->setBatch(batch);
    }
    // Left edges at x; the sizes are GUI units here, overlay units in the widgets (a GUI height is one overlay unit).
    m_square.setup(BaseWidgetSetup{.x = pm_look::kX,
                                   .y = kSquareY,
                                   .height = kSquareHeight,
                                   .width = graphics::OverlayCamera::guiWidthToOverlay(squareWidth),
                                   .anchor = SpriteAnchor::Left});
    m_barBack.setup(BaseWidgetSetup{.x = pm_look::kX,
                                    .y = kBarY,
                                    .height = kBarHeight,
                                    .width = graphics::OverlayCamera::guiWidthToOverlay(kBarWidth),
                                    .colour = graphics::Rgba{64, 64, 64, 255},
                                    .anchor = SpriteAnchor::Left});
    placePmText(m_hint, m_shared.string(kHintString), kHintX, kHintY, pm_look::kMessageScale, pm_look::kRed,
                kTextFontSlot);
    placePmUsage(m_usage, m_shared);
}

int PmLight::handle(MenuCommand command) {
    switch (command) {
    case MenuCommand::Back:
        playSound(pm_cue::kBack);
        return kBack;
    case MenuCommand::Accept:
        playSound(pm_cue::kAccept);
        if (m_shared.profiles != nullptr) {
            m_shared.profiles->setInUse(true);
        }
        return 0;
    case MenuCommand::Left:
    case MenuCommand::Right: {
        const int next = std::clamp(m_value + (command == MenuCommand::Left ? -kStep : kStep), 0, kMax);
        if (next != m_value) {
            m_value = next;
            playSound(pm_cue::kBrightness);
            if (m_shared.state != nullptr) {
                m_shared.state->brightness = m_value;
            }
        }
        return kStay;
    }
    default:
        return kStay;
    }
}

void PmLight::close() {
    for (BaseWidget* widget : {&m_square, &m_barBack, &m_barFill}) {
        widget->shutdown();
    }
    m_hint.shutdown();
    m_usage.shutdown();
}

void PmLight::draw() {
    const auto v = static_cast<std::uint8_t>(m_value);
    m_square.setColour(graphics::Rgba{v, v, v, 255});
    const float fill = kBarWidth * static_cast<float>(m_value) / static_cast<float>(kMax);
    m_barFill.setup(BaseWidgetSetup{.x = pm_look::kX,
                                    .y = kBarY,
                                    .height = kBarHeight,
                                    .width = graphics::OverlayCamera::guiWidthToOverlay(fill),
                                    .colour = pm_look::kRed,
                                    .anchor = SpriteAnchor::Left});
    m_barFill.setVisible(m_value > 0);
    for (const BaseWidget* widget : {&m_square, &m_barBack, &m_barFill}) {
        if (widget->visible()) {
            widget->render(m_shared.canvas);
        }
    }
    for (TextWidget* text : {&m_hint, &m_usage}) {
        text->update(m_shared.frame);
        text->render(m_shared.canvas);
    }
}

// PM_Subtitles

void PmSubtitles::setUp() {
    m_title = m_shared.string(kTitleString);
    const bool english = m_shared.state == nullptr || m_shared.state->language == Language::English;
    m_choices.set({{std::string(m_shared.string(kOnString)), 1}, {std::string(m_shared.string(kOffString)), 0}}, {2},
                  english ? 1 : 0);
}

int PmSubtitles::accept(int code) {
    if (m_shared.state != nullptr) {
        m_shared.state->subtitles = code == 1;
    }
    PmSession& session = m_shared.session;
    session.newGame = true;
    session.createOnExit = true;
    session.done = true;
    return kStay;
}

} // namespace coney::gui
