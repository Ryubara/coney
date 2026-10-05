// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>

#include <imgui.h>

#include "debug/menu_model.h"

// Dear ImGui's settings-file hook, declared in its internal header.
struct ImGuiSettingsHandler;

namespace coney::platform {

/// The debug menus' model drawn with Dear ImGui: a generic renderer, with one widget per item kind and no window of
/// its own for any feature (docs/guides/debug-menu.md#adding-a-feature).
///
/// A menu bar lists the top-level pages; each opens as a window, with a filter box over its items. A submenu is a tree
/// node, an action a button, a toggle a checkbox, a number a drag box with its range (Ctrl+click types a value), a
/// choice a combo, a text a text box (Enter commits it; Up and Down walk its history), a watch its live value (with a
/// plot of its channel on request) and a log a scrolling box. Hovering shows an item's help; right-clicking pins it
/// to Favourites or resets it. Pins use the pad menu's paths, so both front ends share them.
///
/// It needs a current ImGui context and a frame (between ImGui::NewFrame() and ImGui::Render()); it does no drawing
/// of its own, so the tests run it with a bare context.
class ImGuiModelView {
  public:
    /// A view of `model`, which must outlive it.
    explicit ImGuiModelView(debug::MenuModel& model) : m_model(model) {}

    /// Draws the menu bar and the open page windows; `hideLabel` is the menu bar's reminder of how to hide the
    /// overlay.
    void draw(std::string_view hideLabel);

    /// Keeps which page windows are open in ImGui's settings file (`[ConeyPages][Open]`), so they open again in the
    /// next run. Call once, before the context's first frame; the view must outlive the context.
    void keepOpenPagesInSettings();

    /// Opens or closes the window of the top-level page `title` (Favourites too).
    void setPageOpen(const std::string& title, bool open) { m_open[title] = open; }
    /// Whether the window of `title` is open.
    [[nodiscard]] bool pageOpen(const std::string& title) const;

    /// Draws one item as its kind's widget, inside the current window; `path` is its pin path (`Time/Paused`).
    /// Returns whether the kind has a renderer: true for every ItemKind, which the coverage test checks.
    bool drawItem(const debug::MenuItem& item, const std::string& path);

  private:
    // The settings file's hooks: an entry, one `Title=1` line of it, and writing every open state.
    static void* openSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, const char* name);
    static void readSettingsLine(ImGuiContext* context, ImGuiSettingsHandler* handler, void* entry, const char* line);
    static void writeSettings(ImGuiContext* context, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out);

    // Draws one item as drawItem() does; a submenu keeps to `filter` (null: everything) under it.
    bool drawEntry(const debug::MenuItem& item, const std::string& path, const ImGuiTextFilter* filter);
    // Draws the items of `page`, whose items' paths start with `prefix` (empty for Favourites, whose labels are
    // paths), keeping the items `filter` passes; a null filter keeps everything.
    void drawPage(const debug::MenuPage& page, const std::string& prefix, const ImGuiTextFilter* filter);
    // Draws a submenu as a tree node with its page's items under it.
    void drawSubmenu(const debug::MenuItem& item, const std::string& path, const ImGuiTextFilter* filter);
    // The page a submenu at `path` opens, made once and kept until its node closes or its window refreshes.
    const debug::MenuPage* pageAt(const debug::MenuItem& item, const std::string& path);
    // Forgets the pages kept under `prefix`, so they are made again (a refresh, a closed node).
    void forgetPages(const std::string& prefix);
    // The right-click menu of the item just drawn: pin or unpin, reset.
    void contextMenu(const debug::MenuItem& item, const std::string& path);
    // A Text item's box; Enter commits it.
    void drawText(const debug::MenuItem& item, const std::string& path);
    // A Watch item's value, and its channel's plot when asked for.
    void drawWatch(const debug::MenuItem& item, const std::string& path);

    debug::MenuModel& m_model;
    std::shared_ptr<debug::MenuPage> m_root;                         // the top-level pages, made on the first draw
    std::map<std::string, bool> m_open;                              // page windows open, by title
    std::map<std::string, std::shared_ptr<debug::MenuPage>> m_pages; // pages made, by path
    std::map<std::string, ImGuiTextFilter> m_filters;                // each window's filter box, by title
    std::map<std::string, std::string> m_drafts;                     // text boxes' contents, by path
    std::set<std::string> m_editing;                                 // text boxes being typed in, by path
    std::map<std::string, int> m_historyAt;                          // text boxes' place in their history (-1: none)
    std::set<std::string> m_plotted;                                 // watches whose channel is plotted, by path
    std::set<std::string> m_favouritePins;                           // the pins the kept Favourites page was made for
};

} // namespace coney::platform
