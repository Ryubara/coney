// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/menu_model.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>
#include <utility>

#include "core/assert.h"

namespace coney::debug {

namespace {

// The separator of a pin's path.
constexpr char kPathSeparator = '/';

// `path` cut at its separators.
std::vector<std::string_view> splitPath(std::string_view path) {
    std::vector<std::string_view> parts;
    while (true) {
        const auto slash = path.find(kPathSeparator);
        parts.push_back(path.substr(0, slash));
        if (slash == std::string_view::npos) {
            return parts;
        }
        path.remove_prefix(slash + 1);
    }
}

// A number for display: whole numbers without decimals, others with up to 3, no trailing zeros.
std::string formatNumber(double value, bool integer) {
    if (integer) {
        return std::format("{}", static_cast<long long>(std::llround(value)));
    }
    std::string text = std::format("{:.3f}", value);
    while (text.ends_with('0')) {
        text.pop_back();
    }
    if (text.ends_with('.')) {
        text.pop_back();
    }
    return text == "-0" ? "0" : text;
}

} // namespace

std::string_view itemKindName(ItemKind kind) {
    static constexpr std::array<std::string_view, kItemKindCount> kNames{"action", "toggle",  "number", "choice",
                                                                         "text",   "submenu", "watch",  "log"};
    return kNames.at(static_cast<std::size_t>(kind));
}

MenuItem actionItem(std::string label, std::function<void()> run) {
    MenuItem item;
    item.kind = ItemKind::Action;
    item.label = std::move(label);
    item.run = std::move(run);
    return item;
}

MenuItem toggleItem(std::string label, std::function<bool()> get, std::function<void(bool)> set) {
    MenuItem item;
    item.kind = ItemKind::Toggle;
    item.label = std::move(label);
    item.getBool = std::move(get);
    item.setBool = std::move(set);
    return item;
}

MenuItem numberItem(std::string label, std::function<double()> get, std::function<void(double)> set, double min,
                    double max, double step, bool integer) {
    CONEY_ASSERT(min <= max && step > 0.0);
    MenuItem item;
    item.kind = ItemKind::Number;
    item.label = std::move(label);
    item.getNumber = std::move(get);
    item.setNumber = std::move(set);
    item.min = min;
    item.max = max;
    item.step = step;
    item.integer = integer;
    return item;
}

MenuItem choiceItem(std::string label, std::vector<std::string> choices, std::function<std::size_t()> get,
                    std::function<void(std::size_t)> set) {
    MenuItem item;
    item.kind = ItemKind::Choice;
    item.label = std::move(label);
    item.choices = std::move(choices);
    item.getChoice = std::move(get);
    item.setChoice = std::move(set);
    return item;
}

MenuItem textItem(std::string label, std::function<std::string()> get, std::function<void(const std::string&)> set,
                  bool numeric) {
    MenuItem item;
    item.kind = ItemKind::Text;
    item.label = std::move(label);
    item.getText = std::move(get);
    item.setText = std::move(set);
    item.numeric = numeric;
    return item;
}

MenuItem submenuItem(std::string label, std::function<std::shared_ptr<MenuPage>()> open) {
    MenuItem item;
    item.kind = ItemKind::Submenu;
    item.label = std::move(label);
    item.open = std::move(open);
    return item;
}

MenuItem watchItem(std::string label, std::function<std::string()> watch, std::string channel) {
    MenuItem item;
    item.kind = ItemKind::Watch;
    item.label = std::move(label);
    item.watch = std::move(watch);
    item.channel = std::move(channel);
    return item;
}

MenuItem logItem(std::string label, std::function<std::vector<std::string>()> lines) {
    MenuItem item;
    item.kind = ItemKind::Log;
    item.label = std::move(label);
    item.lines = std::move(lines);
    return item;
}

std::string valueText(const MenuItem& item) {
    switch (item.kind) {
    case ItemKind::Toggle:
        return item.getBool && item.getBool() ? "on" : "off";
    case ItemKind::Number: {
        const std::string number = item.getNumber ? formatNumber(item.getNumber(), item.integer) : "?";
        return item.units.empty() ? number : number + " " + item.units;
    }
    case ItemKind::Choice: {
        const std::size_t chosen = item.getChoice ? item.getChoice() : item.choices.size();
        return chosen < item.choices.size() ? item.choices[chosen] : "?";
    }
    case ItemKind::Text:
        return item.getText ? item.getText() : std::string{};
    case ItemKind::Submenu:
        return item.detail.empty() ? std::string(">") : item.detail;
    case ItemKind::Watch:
        return item.watch ? item.watch() : std::string{};
    case ItemKind::Action:
    case ItemKind::Log:
        return {};
    }
    return {};
}

bool adjustItem(const MenuItem& item, int steps, StepSize size) {
    if (steps == 0) {
        return false;
    }
    switch (item.kind) {
    case ItemKind::Toggle:
        if (!item.getBool || !item.setBool) {
            return false;
        }
        item.setBool(!item.getBool());
        return true;
    case ItemKind::Number: {
        if (!item.getNumber || !item.setNumber) {
            return false;
        }
        // Fine is a tenth of the step (but never below one for whole numbers), coarse ten steps.
        double step = item.step;
        if (size == StepSize::Fine) {
            step = item.integer ? std::max(1.0, item.step / 10.0) : item.step / 10.0;
        } else if (size == StepSize::Coarse) {
            step = item.step * 10.0;
        }
        const double before = item.getNumber();
        double after = std::clamp(before + step * steps, item.min, item.max);
        if (item.integer) {
            after = std::round(after);
        }
        if (after == before) {
            return false;
        }
        item.setNumber(after);
        return true;
    }
    case ItemKind::Choice: {
        if (!item.getChoice || !item.setChoice || item.choices.empty()) {
            return false;
        }
        const auto count = static_cast<long long>(item.choices.size());
        const long long moved = ((static_cast<long long>(item.getChoice()) + steps) % count + count) % count;
        item.setChoice(static_cast<std::size_t>(moved));
        return true;
    }
    case ItemKind::Action:
    case ItemKind::Text:
    case ItemKind::Submenu:
    case ItemKind::Watch:
    case ItemKind::Log:
        return false;
    }
    return false;
}

bool resetItem(const MenuItem& item) {
    if (item.kind != ItemKind::Number || !item.defaultValue || !item.setNumber) {
        return false;
    }
    item.setNumber(*item.defaultValue);
    return true;
}

std::optional<double> parseNumber(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '+')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ') {
        text.remove_suffix(1);
    }
    double value = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

MenuItem& MenuPage::add(MenuItem item) {
    CONEY_ASSERT(find(item.label) == nullptr);
    m_items.push_back(std::move(item));
    return m_items.back();
}

const MenuItem* MenuPage::find(std::string_view label) const {
    const auto found = std::ranges::find(m_items, label, &MenuItem::label);
    return found == m_items.end() ? nullptr : &*found;
}

void MenuPage::setRebuild(std::function<void(MenuPage&)> rebuild) {
    m_rebuild = std::move(rebuild);
    refresh();
}

void MenuPage::refresh() {
    if (!m_rebuild) {
        return;
    }
    m_items.clear();
    m_rebuild(*this);
}

MenuModel::MenuModel() = default;

void MenuModel::addPage(std::string title, std::function<void(MenuPage&)> build, std::string help) {
    CONEY_ASSERT(title != kFavouritesTitle && std::ranges::find(m_pages, title, &PageEntry::title) == m_pages.end());
    m_pages.push_back(PageEntry{std::move(title), std::move(help), std::move(build)});
}

std::shared_ptr<MenuPage> MenuModel::root() const {
    auto page = std::make_shared<MenuPage>(std::string(kRootTitle));
    page->add(submenuItem(std::string(kFavouritesTitle), [this] { return favourites(); }))
        .withHelp("Pinned items. Square on any item pins it here.");
    for (const PageEntry& entry : m_pages) {
        page->add(submenuItem(entry.title, [this, title = entry.title] { return openPage(title); }))
            .withHelp(entry.help);
    }
    return page;
}

std::vector<std::string> MenuModel::pageTitles() const {
    std::vector<std::string> titles;
    titles.reserve(m_pages.size());
    for (const PageEntry& entry : m_pages) {
        titles.push_back(entry.title);
    }
    return titles;
}

std::shared_ptr<MenuPage> MenuModel::openPage(std::string_view title) const {
    const auto found = std::ranges::find(m_pages, title, &PageEntry::title);
    if (found == m_pages.end()) {
        return nullptr;
    }
    auto page = std::make_shared<MenuPage>(found->title);
    page->setRebuild(found->build);
    return page;
}

bool MenuModel::togglePin(const std::string& path) {
    if (m_pins.erase(path) != 0) {
        return false;
    }
    m_pins.insert(path);
    return true;
}

std::pair<std::shared_ptr<MenuPage>, const MenuItem*> MenuModel::resolve(std::string_view path) const {
    // Follow the submenus named by every part but the last, from the root, then find the item.
    std::shared_ptr<MenuPage> page = root();
    const std::vector<std::string_view> parts = splitPath(path);
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const MenuItem* step = page->find(parts[i]);
        // Favourites never leads to an item of its own: resolving through it would recurse.
        if (step == nullptr || step->kind != ItemKind::Submenu || !step->open || step->label == kFavouritesTitle) {
            return {nullptr, nullptr};
        }
        page = step->open();
        if (page == nullptr) {
            return {nullptr, nullptr};
        }
    }
    const MenuItem* item = page->find(parts.back());
    return item == nullptr ? std::pair<std::shared_ptr<MenuPage>, const MenuItem*>{nullptr, nullptr}
                           : std::pair{page, item};
}

std::shared_ptr<MenuPage> MenuModel::favourites() const {
    auto page = std::make_shared<MenuPage>(std::string(kFavouritesTitle));
    for (const std::string& path : m_pins) {
        auto [holder, item] = resolve(path);
        if (item == nullptr) {
            continue;
        }
        // A copy of the pinned item under its full path, keeping the page that owns its state alive.
        MenuItem copy = *item;
        copy.label = path;
        copy.keepAlive = holder;
        page->add(std::move(copy));
    }
    if (page->items().empty()) {
        page->add(watchItem("No pins yet", [] { return std::string("Square pins the item under the cursor"); }));
    }
    return page;
}

void MenuModel::addChannel(std::string name, std::function<float()> sampler) {
    auto [entry, inserted] = m_channels.try_emplace(std::move(name));
    entry->second.sampler = std::move(sampler);
}

const TimeSeries* MenuModel::channel(std::string_view name) const {
    const auto found = m_channels.find(name);
    return found == m_channels.end() ? nullptr : &found->second.series;
}

void MenuModel::sampleChannels() {
    for (auto& [name, entry] : m_channels) {
        if (entry.sampler) {
            entry.series.push(entry.sampler());
        }
    }
}

} // namespace coney::debug
