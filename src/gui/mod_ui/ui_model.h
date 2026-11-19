// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the UI widget model for mods (docs/guides/mod-ui.md); the original's scripts build no UI
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/pad.h"
#include "gui/menu_input.h"
#include "gui/mod_ui/ui_screen.h"
#include "gui/mod_ui/ui_types.h"

namespace coney::gui {

/// The UI widget model for mods: the retained tree of the screens and widgets projects build, by id, with their focus,
/// the game's menu commands and cues, the trusted layer and the limits (docs/guides/studio/ui.md#widgets). C++ only:
/// the natives that let a project's scripts call it come later (the design's build issue 4).
///
/// - **Projects** are registered by Coney's own code with their title, which is what the frame label shows; a project
///   names only its own screens and widgets, and a call naming another's is refused as unknown.
/// - **Screens and widgets** are made, changed and removed by id, within the limits of ui_limit, each refused with a
///   UiRefusal. A screen is shown by openScreen(): a `Menu` screen for one player slot, on that slot's flow (the
///   project's next screen goes over it, back returns to it), a `Hud` screen among the shown HUD elements.
/// - **One project per slot.** While a slot's flow holds a project's screens, another project's screen is refused for
///   that slot.
/// - **Input**: step() reads one slot's pad with the game's MenuInput (its 110 ms gap, 400 ms held stick, auto-repeat
///   and the focus block) and hands the command to the top screen of the slot's flow.
/// - **Back always closes**: back on a screen queues Back and then Closed, plays the back cue and pops the screen from
///   its flow, whatever the project does with the event; the screen under it takes the focus again. A dialog takes back
///   first.
/// - **The trusted layer**: while a trusted screen of Coney's is open (openTrusted()), no mod screen opens and no
///   slot's pad reaches a mod screen; the mod screens already open stay open. Drawing the trusted screen above them is
///   its renderer's.
///
/// Presentation state, each machine's own: owned by the game mode or session that hosts the screens, never global;
/// screens are kept in id order, so every iteration is stable.
///
/// Research: docs/guides/studio/ui.md#widgets, docs/guides/studio/ui.md#no-spoofing
/// @coney-only the UI widget model for mods; the original's scripts build no UI
class UiModel {
  public:
    /// A model sending the screens' front-end cues to `playCue` (may be empty).
    /// @coney-only the UI widget model for mods
    explicit UiModel(std::function<void(int cue)> playCue = {});
    ~UiModel() = default;
    // Not copied or moved: the screens' cue callbacks hold `this`, the widgets point into the inputs.
    UiModel(const UiModel&) = delete;
    UiModel& operator=(const UiModel&) = delete;
    UiModel(UiModel&&) = delete;
    UiModel& operator=(UiModel&&) = delete;

    /// Registers `project` with the title its frame label shows. Coney's own code calls it (the loader); a second call
    /// changes the title.
    /// @coney-only the UI widget model for mods
    void registerProject(std::string project, std::string title);
    /// The title of `project`, empty when it is not registered.
    [[nodiscard]] std::string_view projectTitle(std::string_view project) const;
    /// The keyboard's labels from now on (the front end's global strings 0x97, 0x99 and 0x9a).
    void setKeyboardStrings(UiScreen::KeyboardStrings keys) { m_keys = std::move(keys); }

    /// Makes a screen of `project`.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<UiScreenId, UiRefusal> createScreen(std::string_view project, UiScreenDef def);
    /// Closes and removes screen `screen` with its widgets.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> destroyScreen(std::string_view project, UiScreenId screen,
                                                               std::uint64_t nowMs);
    /// Adds a widget to `screen`.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<UiWidgetId, UiRefusal> addWidget(std::string_view project, UiScreenId screen,
                                                                 UiWidgetDef def);
    /// Appends `item` to grid or list `widget`.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> addItem(std::string_view project, UiWidgetId widget, UiItem item);
    /// Removes `widget`.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> removeWidget(std::string_view project, UiWidgetId widget,
                                                              std::uint64_t nowMs);
    /// Replaces the text of `widget` (text, usage line, header, message).
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> setText(std::string_view project, UiWidgetId widget, std::string text);
    /// Shows or hides `widget` at `nowMs`; a hidden widget cannot have the focus, which passes to the first focusable
    /// widget left, and a widget shown on a screen with no focus takes it.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> setVisible(std::string_view project, UiWidgetId widget, bool visible,
                                                            std::uint64_t nowMs);
    /// Moves `widget` to GUI point (`x`, `y`).
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> setPosition(std::string_view project, UiWidgetId widget, float x,
                                                             float y);
    /// Fills bar `widget` to `fill`, clamped to 0 to 1.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> setFill(std::string_view project, UiWidgetId widget, float fill);
    /// Gives `widget` the focus of its screen.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> focus(std::string_view project, UiWidgetId widget,
                                                       std::uint64_t nowMs);
    /// Opens a choice dialog on menu screen `screen`.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> askChoice(std::string_view project, UiScreenId screen,
                                                           std::string text, const std::vector<std::string>& choices,
                                                           std::size_t defaultChoice, std::uint64_t nowMs);

    /// Shows `screen` for player `slot` (a menu screen goes on top of the slot's flow and takes the focus; a HUD screen
    /// joins the shown ones and `slot` names whose view it draws in).
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> openScreen(std::string_view project, UiScreenId screen,
                                                            std::size_t slot, std::uint64_t nowMs);
    /// Hides `screen`: off its flow (the screens over it go too) or off the shown HUD elements; queues Closed.
    /// @coney-only the UI widget model for mods
    [[nodiscard]] std::expected<void, UiRefusal> closeScreen(std::string_view project, UiScreenId screen,
                                                             std::uint64_t nowMs);

    /// A trusted screen of Coney's opened (the Mods screen, a mod's details, the profile, save and quit screens). Only
    /// Coney's code calls it; nesting counts.
    /// @coney-only the trusted layer (docs/guides/studio/ui.md#no-spoofing)
    void openTrusted();
    /// A trusted screen closed; with none left, each slot's top mod screen takes its focus again at `nowMs`.
    /// @coney-only the trusted layer
    void closeTrusted(std::uint64_t nowMs);
    /// Whether a trusted screen is open.
    [[nodiscard]] bool trustedOpen() const { return m_trusted > 0; }

    /// One frame of player `slot`'s input: the menu command `pad` gives at `nowMs` to the top screen of the slot's
    /// flow, back closing it. Nothing while a trusted screen is open, or with no screen open for the slot.
    /// @coney-only the UI widget model for mods
    void step(std::size_t slot, const Pad& pad, std::uint64_t nowMs);

    /// The events queued since the last call, in order.
    std::vector<UiEvent> takeEvents();

    /// Whether a screen on player `slot`'s flow pauses play (UiScreenDef::pause): the host then runs the slot's screens
    /// as a game mode over gameplay (UiScreenMode) instead of over live play.
    [[nodiscard]] bool pausesPlay(std::size_t slot) const;
    /// Whether a mod screen has player `slot`'s pad: a menu screen is open for the slot. Gameplay then sees the port
    /// empty (UiInputMask).
    [[nodiscard]] bool takesPad(std::size_t slot) const;
    /// The screens on player `slot`'s flow, bottom first.
    [[nodiscard]] const std::vector<UiScreenId>& flow(std::size_t slot) const;
    /// The top screen of player `slot`'s flow, if any.
    [[nodiscard]] std::optional<UiScreenId> top(std::size_t slot) const;
    /// The HUD screens shown, in the order they were opened, with the slot whose view each draws in.
    [[nodiscard]] const std::vector<std::pair<UiScreenId, std::size_t>>& hud() const { return m_hud; }
    /// Screen `screen`, or null.
    [[nodiscard]] const UiScreen* screen(UiScreenId screen) const;
    /// Every screen, in id order.
    [[nodiscard]] const std::map<UiScreenId, std::unique_ptr<UiScreen>>& screens() const { return m_screens; }
    /// The screen widget `widget` is on, or none.
    [[nodiscard]] std::optional<UiScreenId> screenOf(UiWidgetId widget) const;
    /// The input of player `slot`.
    [[nodiscard]] const MenuInput& input(std::size_t slot) const { return m_inputs.at(slot); }

  private:
    // Screen `screen` if it is `project`'s, or null.
    [[nodiscard]] UiScreen* owned(std::string_view project, UiScreenId screen);
    // Widget `widget` and its screen if they are `project`'s.
    [[nodiscard]] std::pair<UiScreen*, UiWidget*> ownedWidget(std::string_view project, UiWidgetId widget);
    // How many screens `project` has.
    [[nodiscard]] std::size_t screensOf(std::string_view project) const;
    // How many widgets `project` has over all its screens.
    [[nodiscard]] std::size_t widgetsOf(std::string_view project) const;
    // The slot whose flow holds `screen`, if any.
    [[nodiscard]] std::optional<std::size_t> slotOf(UiScreenId screen) const;
    // Pops `slot`'s flow down to below `screen`, queueing Closed for each, and focuses the new top.
    void popTo(std::size_t slot, UiScreenId screen, std::uint64_t nowMs);
    // The input of the slot whose flow has `screen` on top; a screen on no flow or covered on its flow gets one no slot
    // reads, so a change to it leaves every slot's timings alone (the screen takes the focus afresh when it opens or
    // comes back on top).
    [[nodiscard]] MenuInput& inputOf(UiScreenId screen);
    // Plays `cue`, if the model has a way to.
    void playCue(int cue) const;

    std::function<void(int cue)> m_playCue;
    std::map<std::string, std::string, std::less<>> m_projects; // project -> title
    std::map<UiScreenId, std::unique_ptr<UiScreen>> m_screens;
    std::map<UiWidgetId, UiScreenId> m_widgetScreens;
    std::array<std::vector<UiScreenId>, kUiSlots> m_flows;
    std::array<MenuInput, kUiSlots> m_inputs;
    MenuInput m_unreadInput; // for the screens on no flow or covered on theirs
    std::vector<std::pair<UiScreenId, std::size_t>> m_hud;
    std::vector<UiEvent> m_events;
    UiScreen::KeyboardStrings m_keys;
    std::uint32_t m_nextScreen = 1;
    std::uint32_t m_nextWidget = 1;
    int m_trusted = 0;
};

} // namespace coney::gui
