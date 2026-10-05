// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "debug/time_series.h"

namespace coney::debug {

/// The kinds of item a debug menu page holds. Every front end (the pad menu, the developer overlay) has one renderer
/// per kind, and nothing else: a feature is a page of these items, defined once in src/debug/
/// (docs/guides/debug-menu.md#adding-a-feature).
enum class ItemKind : std::uint8_t {
    Action,  ///< Runs a callback when chosen.
    Toggle,  ///< On or off.
    Number,  ///< A number with a range and a step: an int or a float slider.
    Choice,  ///< One of a list of named options.
    Text,    ///< A line of text or a number typed in: by pad spinners in the pad menu, a text box in the overlay.
    Submenu, ///< Opens another page.
    Watch,   ///< A read-only value updated live, optionally with a plotted history (a channel).
    Log,     ///< Read-only lines: results, a console's output.
};

/// How many kinds there are; ItemKind values are 0 to kItemKindCount - 1.
inline constexpr std::size_t kItemKindCount = 8;

/// The name of a kind, for logs and tests.
[[nodiscard]] std::string_view itemKindName(ItemKind kind);

/// The size of a change to a number: a front end's modifier picks it.
enum class StepSize : std::uint8_t {
    Fine,   ///< A tenth of the step.
    Normal, ///< The step.
    Coarse, ///< Ten steps.
};

class MenuPage;

/// One entry of a page: its kind, label and help, and the callbacks the front ends call to read and change what it
/// stands for. It is data: a front end reads the fields of its kind and calls the callbacks, and holds no feature
/// logic of its own. Build one with the factory functions below, which fill the fields each kind needs.
struct MenuItem {
    ItemKind kind = ItemKind::Action;
    std::string label;  ///< What the item is called; unique within its page (pins find an item by it).
    std::string help;   ///< One line of help, shown for the item under the cursor or on hover.
    std::string detail; ///< A short tag shown beside a Submenu in place of `>` (a binding's status); may be empty.

    /// Action: what choosing it does.
    std::function<void()> run;

    /// Toggle: the state, and how to change it.
    std::function<bool()> getBool;
    std::function<void(bool)> setBool;

    /// Number: the value and how to change it; the range, step and units; whether it is whole numbers; the default
    /// a reset returns to (none: no reset).
    std::function<double()> getNumber;
    std::function<void(double)> setNumber;
    double min = 0.0;
    double max = 1.0;
    double step = 1.0;
    bool integer = false;
    std::string units;
    std::optional<double> defaultValue;

    /// Choice: the options, the chosen one and how to choose.
    std::vector<std::string> choices;
    std::function<std::size_t()> getChoice;
    std::function<void(std::size_t)> setChoice;

    /// Text: the text, what entering a new one does, whether only a number is accepted, the longest text, and the
    /// lines entered before, newest last, when the item keeps a history (a console).
    std::function<std::string()> getText;
    std::function<void(const std::string&)> setText;
    bool numeric = false;
    std::size_t maxLength = 64;
    std::shared_ptr<std::vector<std::string>> history;

    /// Submenu: makes the page it opens (called each time it opens, so the page is current).
    std::function<std::shared_ptr<MenuPage>()> open;

    /// Watch: the value as text; and the channel holding its history, when it has one (MenuModel::channel()).
    std::function<std::string()> watch;
    std::string channel;

    /// Log: the lines, oldest first.
    std::function<std::vector<std::string>()> lines;

    /// Whatever the callbacks need kept alive beyond their page: a pinned copy keeps the page it came from.
    std::shared_ptr<const void> keepAlive;

    /// Sets the help line; for chaining after a factory.
    MenuItem& withHelp(std::string text) {
        help = std::move(text);
        return *this;
    }
};

/// An Action item.
[[nodiscard]] MenuItem actionItem(std::string label, std::function<void()> run);
/// A Toggle item.
[[nodiscard]] MenuItem toggleItem(std::string label, std::function<bool()> get, std::function<void(bool)> set);
/// A Number item over [min, max] in steps of `step`; `integer` for whole numbers.
[[nodiscard]] MenuItem numberItem(std::string label, std::function<double()> get, std::function<void(double)> set,
                                  double min, double max, double step, bool integer);
/// A Choice item.
[[nodiscard]] MenuItem choiceItem(std::string label, std::vector<std::string> choices, std::function<std::size_t()> get,
                                  std::function<void(std::size_t)> set);
/// A Text item; `numeric` accepts only a number.
[[nodiscard]] MenuItem textItem(std::string label, std::function<std::string()> get,
                                std::function<void(const std::string&)> set, bool numeric);
/// A Submenu item.
[[nodiscard]] MenuItem submenuItem(std::string label, std::function<std::shared_ptr<MenuPage>()> open);
/// A Watch item; `channel` names a MenuModel channel to plot, or is empty.
[[nodiscard]] MenuItem watchItem(std::string label, std::function<std::string()> watch, std::string channel = {});
/// A Log item.
[[nodiscard]] MenuItem logItem(std::string label, std::function<std::vector<std::string>()> lines);

/// The text a front end shows beside an item's label: on/off, the number with its units, the chosen option, the text,
/// the watched value, `>` for a submenu; empty for an action or a log.
[[nodiscard]] std::string valueText(const MenuItem& item);

/// Changes a Number by `steps` steps of `size` (clamped, whole numbers rounded), a Toggle by flipping it (any non-zero
/// `steps`), or a Choice by moving `steps` options (wrapping). Does nothing for other kinds. Returns whether it
/// changed something.
bool adjustItem(const MenuItem& item, int steps, StepSize size);
/// Returns a Number to its default; returns false when it has none or is another kind.
bool resetItem(const MenuItem& item);
/// The number `text` reads as (C notation, spaces around allowed), or nothing.
[[nodiscard]] std::optional<double> parseNumber(std::string_view text);

/// A page of items, the unit both front ends show: a list in the pad menu, a window or a collapsing section in the
/// overlay. A page with a `rebuild` callback refills its items when it is opened and when refresh() is called, for
/// lists that change (the bindings of a category, the call log).
class MenuPage {
  public:
    /// An empty page called `title`.
    explicit MenuPage(std::string title) : m_title(std::move(title)) {}

    /// The title, also the page's step in a breadcrumb and a pin's path.
    [[nodiscard]] const std::string& title() const { return m_title; }
    /// The items, in order.
    [[nodiscard]] const std::vector<MenuItem>& items() const { return m_items; }
    /// Appends an item and returns it, for chaining (`.withHelp(...)`). Labels must be unique on a page (CONEY_ASSERT).
    MenuItem& add(MenuItem item);
    /// The item labelled `label`, or null.
    [[nodiscard]] const MenuItem* find(std::string_view label) const;

    /// Sets what refills the page; refresh() runs it at once.
    void setRebuild(std::function<void(MenuPage&)> rebuild);
    /// Empties the page and runs its rebuild callback, if any.
    void refresh();
    /// Removes every item.
    void clear() { m_items.clear(); }

  private:
    std::string m_title;
    std::vector<MenuItem> m_items;
    std::function<void(MenuPage&)> m_rebuild;
};

/// The whole debug menu as data: the top-level pages, the pinned items, and the channels (sampled values a front end
/// can plot). It is the one place features are defined; the pad menu (src/debug/menu_navigator.h and
/// src/gui/debug_menu_view.h) and the developer overlay (src/platform/imgui_overlay.h) only render it.
///
/// Coney's own tool: the original has no debug menu (docs/research/debug.md#not-present).
class MenuModel {
  public:
    /// The title of the root page.
    static constexpr std::string_view kRootTitle = "Debug";
    /// The title of the favourites page, always first on the root page.
    static constexpr std::string_view kFavouritesTitle = "Favourites";
    /// Samples a channel keeps.
    static constexpr std::size_t kChannelSamples = 300;

    MenuModel();

    /// Adds a top-level page: one line per feature. `build` fills a fresh page each time it is opened.
    void addPage(std::string title, std::function<void(MenuPage&)> build, std::string help = {});
    /// The root page: Favourites, then the top-level pages in the order they were added. Made fresh on each call.
    [[nodiscard]] std::shared_ptr<MenuPage> root() const;
    /// The top-level pages' titles, in order (without Favourites).
    [[nodiscard]] std::vector<std::string> pageTitles() const;
    /// A fresh copy of the top-level page `title`, or null.
    [[nodiscard]] std::shared_ptr<MenuPage> openPage(std::string_view title) const;

    /// Pins or unpins the item at `path`: page titles from below the root, then the item's label, joined by `/`
    /// (`Time/Paused`). Returns whether it is pinned afterwards.
    bool togglePin(const std::string& path);
    /// Whether `path` is pinned.
    [[nodiscard]] bool pinned(std::string_view path) const { return m_pins.contains(std::string(path)); }
    /// The pinned paths, sorted.
    [[nodiscard]] const std::set<std::string>& pins() const { return m_pins; }
    /// The favourites page: one item per pin that still resolves, standing for the pinned item (same callbacks). The
    /// pages the items come from are kept alive by the returned page's items.
    [[nodiscard]] std::shared_ptr<MenuPage> favourites() const;
    /// The item at `path` and the page holding it (which keeps the item alive), or nulls when it does not resolve.
    [[nodiscard]] std::pair<std::shared_ptr<MenuPage>, const MenuItem*> resolve(std::string_view path) const;

    /// Adds a channel: a value sampled once per simulation step by sampleChannels(), kept for the last
    /// kChannelSamples steps, which a Watch item can name to be plotted. Re-adding a name replaces its sampler.
    void addChannel(std::string name, std::function<float()> sampler);
    /// The history of the channel `name`, or null.
    [[nodiscard]] const TimeSeries* channel(std::string_view name) const;
    /// Samples every channel once. The input gate calls it once per step (src/debug/input_gate.h).
    void sampleChannels();

  private:
    // One top-level page.
    struct PageEntry {
        std::string title;
        std::string help;
        std::function<void(MenuPage&)> build;
    };
    // One channel and its history.
    struct Channel {
        std::function<float()> sampler;
        TimeSeries series{kChannelSamples};
    };

    std::vector<PageEntry> m_pages;
    std::set<std::string> m_pins;
    std::map<std::string, Channel, std::less<>> m_channels;
};

} // namespace coney::debug
