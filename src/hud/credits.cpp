// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/credits.h"

#include <algorithm>

#include "graphics/font.h"
#include "gui/colour_table.h"

namespace coney::hud {

namespace {

// `CfgCredits` style 4 is stored as 2.
constexpr std::uint32_t kStyleAlias = 4;

// Whether a style is drawn as one centred text (every style but the role-and-name one).
bool centred(std::uint32_t style) { return style != static_cast<std::uint32_t>(CreditStyle::RoleName); }

// The text a centred line shows: its first text, or its second when the first is empty (Coney's choice).
const std::string& centredText(const CreditLine& line) { return line.role.empty() ? line.name : line.role; }

} // namespace

Credits::Credits() {
    for (gui::TextWidget& widget : m_pool) {
        widget.setVisible(false);
    }
}

void Credits::addLine(std::uint32_t style, std::string_view role, std::string_view name) {
    CreditLine line;
    line.style = style == kStyleAlias ? static_cast<std::uint32_t>(CreditStyle::Line) : style;
    line.role = std::string(role);
    line.name = std::string(name);
    line.index = m_lines.size();
    m_lines.push_back(std::move(line));
}

void Credits::load(const std::function<void()>& runScript) {
    // A fresh list, filled by the script's CfgCredits calls.
    m_lines.clear();
    if (runScript) {
        runScript();
    }
    // The pool: hidden widgets at the start line, scale 1.8, in the menus' grey.
    for (std::size_t i = 0; i < kCreditWidgets; ++i) {
        m_pool.at(i).setup(gui::TextWidgetSetup{.x = kCreditsCentreX,
                                                .y = kCreditsStartY,
                                                .scale = kCreditHeadingScale,
                                                .colour = gui::kMenuGrey,
                                                .alignment = gui::TextAlignment::Left});
        m_pool.at(i).setText({});
        m_pool.at(i).setVisible(false);
        m_heights.at(i) = 0.0F;
    }
    m_firstLive = 0;
    m_lastRole.clear();
    m_hasLastRole = false;
    m_sharedRole = 1;
    m_loaded = true;
}

void Credits::start(std::uint64_t nowMs) {
    if (!m_loaded || m_running) {
        return;
    }
    m_running = true;
    m_firstLive = 0;
    m_startMs = nowMs;
}

void Credits::shutdown() {
    if (!m_running) {
        return;
    }
    for (gui::TextWidget& widget : m_pool) {
        widget.setVisible(false);
    }
    m_lines.clear();
    m_running = false;
    m_loaded = false;
}

std::size_t Credits::freeWidgets() const {
    return static_cast<std::size_t>(
        std::ranges::count_if(m_pool, [](const gui::TextWidget& widget) { return !widget.visible(); }));
}

// @orig 0x001a9678 Credits_FindFreeWidget (Credits.cpp)
std::optional<std::size_t> Credits::findFreeWidget() const {
    for (std::size_t i = 0; i < kCreditWidgets; ++i) {
        if (!m_pool.at(i).visible()) {
            return i;
        }
    }
    return std::nullopt;
}

float Credits::lineHeight(std::uint32_t style) {
    const float scale =
        style == static_cast<std::uint32_t>(CreditStyle::Heading) ? kCreditHeadingScale : kCreditLineScale;
    return graphics::fontMetrics(scale).height;
}

// Lays out the line's widgets: a centred text (flags 6) at its style's size, or a role ending half the gutter left of
// the centre (flags 5) and a name starting half the gutter right of it (flags 4), both at 0.9.
// @orig 0x001a9960 Credits_AssignWidgets (Credits.cpp)
// @orig 0x001a96f0 Credits_LayoutCentred (Credits.cpp)
// @orig 0x001a97c0 Credits_LayoutRole (Credits.cpp)
// @orig 0x001a9890 Credits_LayoutName (Credits.cpp)
void Credits::assignWidgets(CreditLine& line) {
    const std::optional<std::size_t> main = findFreeWidget();
    if (!main) {
        return;
    }
    line.started = true;
    line.widget = main;
    m_pool.at(*main).setVisible(true);
    const float height = lineHeight(line.style);
    if (centred(line.style)) {
        const float scale =
            line.style == static_cast<std::uint32_t>(CreditStyle::Heading) ? kCreditHeadingScale : kCreditLineScale;
        m_pool.at(*main).setup(gui::TextWidgetSetup{.x = kCreditsCentreX,
                                                    .y = kCreditsStartY,
                                                    .scale = scale,
                                                    .colour = gui::kMenuGrey,
                                                    .alignment = gui::TextAlignment::Centre});
        m_pool.at(*main).setText(centredText(line));
        m_heights.at(*main) = height;
        return;
    }
    // The name, left-aligned from the gutter's right edge.
    m_pool.at(*main).setup(gui::TextWidgetSetup{.x = kCreditsCentreX + (kCreditsGutter / 2.0F),
                                                .y = kCreditsStartY,
                                                .scale = kCreditLineScale,
                                                .colour = gui::kMenuGrey,
                                                .alignment = gui::TextAlignment::Left});
    m_pool.at(*main).setText(line.name);
    m_heights.at(*main) = height;
    // The role only when it differs from the last one shown; otherwise one more name shares it.
    if (m_hasLastRole && line.role == m_lastRole) {
        ++m_sharedRole;
        return;
    }
    m_lastRole = line.role;
    m_hasLastRole = true;
    m_sharedRole = 1;
    const std::optional<std::size_t> role = findFreeWidget();
    if (!role) {
        return;
    }
    line.roleWidget = role;
    m_pool.at(*role).setVisible(true);
    m_pool.at(*role).setup(gui::TextWidgetSetup{.x = kCreditsCentreX - (kCreditsGutter / 2.0F),
                                                .y = kCreditsStartY,
                                                .scale = kCreditLineScale,
                                                .colour = gui::kMenuGrey,
                                                .alignment = gui::TextAlignment::Right});
    m_pool.at(*role).setText(line.role);
    m_heights.at(*role) = height;
}

// @orig 0x001a9a78 Credits_PlaceNewLine (Credits.cpp)
void Credits::placeLine(const CreditLine& line, float y) {
    for (const std::optional<std::size_t>& index : {line.widget, line.roleWidget}) {
        if (index) {
            m_pool.at(*index).style().y = y;
        }
    }
}

// @orig 0x001a9c48 Credits_LineHeight (Credits.cpp)
float Credits::heightOf(const CreditLine& line) const {
    float height = 0.0F;
    for (const std::optional<std::size_t>& index : {line.widget, line.roleWidget}) {
        if (index) {
            height = std::max(height, m_heights.at(*index));
        }
    }
    return height;
}

// @orig 0x001a9d78 Credits_GapAfter (Credits.cpp)
float Credits::gapAfter(std::size_t index) const {
    const std::uint32_t style = m_lines.at(index).style;
    if (style == static_cast<std::uint32_t>(CreditStyle::Heading)) {
        return kCreditGapAfterHeading;
    }
    if (index + 1 >= m_lines.size()) {
        return 0.0F;
    }
    const CreditLine& next = m_lines.at(index + 1);
    const bool roleOrLine = style == static_cast<std::uint32_t>(CreditStyle::Line) ||
                            style == static_cast<std::uint32_t>(CreditStyle::RoleName);
    const bool nextHeading = next.style == static_cast<std::uint32_t>(CreditStyle::Heading) ||
                             next.style == static_cast<std::uint32_t>(CreditStyle::Section);
    if (roleOrLine && nextHeading) {
        return kCreditGapBeforeHeading;
    }
    // A role line whose role two or more names shared, before a new role.
    if (style == static_cast<std::uint32_t>(CreditStyle::RoleName) && next.role != m_lastRole && m_sharedRole >= 2) {
        return kCreditGapAfterSharedRole;
    }
    return 0.0F;
}

// @orig 0x001a9c88 Credits_UpdateLine (Credits.cpp)
void Credits::updateLine(CreditLine& line) {
    if (!line.widget) {
        return;
    }
    const float y = m_pool.at(*line.widget).style().y;
    if (y + heightOf(line) >= kCreditsTopLimit) {
        return;
    }
    for (const std::optional<std::size_t>& index : {line.widget, line.roleWidget}) {
        if (index) {
            m_pool.at(*index).setVisible(false);
        }
    }
    line.widget.reset();
    line.roleWidget.reset();
    line.finished = true;
    ++m_firstLive;
}

// @orig 0x001a9b70 Credits_ScrollLine (Credits.cpp)
void Credits::scrollLine(const CreditLine& line) {
    for (const std::optional<std::size_t>& index : {line.widget, line.roleWidget}) {
        if (index) {
            m_pool.at(*index).style().y -= kCreditsStep;
        }
    }
}

bool Credits::anyShown() const {
    return std::ranges::any_of(m_pool, [](const gui::TextWidget& widget) { return widget.visible(); });
}

void Credits::update() {
    if (!m_running) {
        return;
    }
    // Walk the live lines from the first: start new ones below the last while the pool allows, move the others up.
    float pen = kCreditsStartY;
    bool first = true;
    for (std::size_t i = m_firstLive; i < m_lines.size(); ++i) {
        CreditLine& line = m_lines.at(i);
        if (line.finished) {
            continue;
        }
        if (!line.started) {
            if (freeWidgets() < 2) {
                break;
            }
            assignWidgets(line);
            const float height = heightOf(line);
            placeLine(line, pen + (height / 2.0F));
            line.gap = gapAfter(i);
            pen += height + line.gap;
            first = false;
            continue;
        }
        updateLine(line);
        if (line.finished) {
            continue;
        }
        scrollLine(line);
        const float height = heightOf(line);
        // The first live line's centre is the pen's start: half its height takes the pen to its bottom.
        pen = first ? m_pool.at(*line.widget).style().y + (height / 2.0F) : pen + height;
        pen += line.gap;
        first = false;
    }
    // Every widget given back: the end.
    if (!anyShown()) {
        shutdown();
        m_ended = true;
    }
}

void Credits::render(const gui::GuiCanvas& canvas) const {
    if (!m_running) {
        return;
    }
    for (std::size_t i = 0; i < kCreditWidgets; ++i) {
        const gui::TextWidget& widget = m_pool.at(i);
        if (widget.visible() && widget.style().y - (m_heights.at(i) / 2.0F) < kCreditsStartY) {
            widget.render(canvas);
        }
    }
}

bool Credits::takeEnded() {
    const bool ended = m_ended;
    m_ended = false;
    return ended;
}

} // namespace coney::hud
