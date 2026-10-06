// SPDX-License-Identifier: GPL-3.0-or-later
// The HUD page: the in-game HUD's parts switched on and off, the player panel's values set, and an objective, a hint
// and an announcement fired (docs/guides/debug-menu.md#pages, docs/research/hud.md).
#include <array>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/play_controls.h"
#include "hud/hud.h"

namespace coney::debug {

namespace {

// What the page shows when no mode draws a HUD.
constexpr std::string_view kNoHud = "no HUD: play a level (--play-level, or the story)";

// The HUD of the mode playing now, or null.
hud::Hud* hudOf(DebugSession& session) {
    PlayControls* play = session.play();
    return play != nullptr ? play->hud() : nullptr;
}

// A toggle over the HUD; off, and inert, when there is none.
MenuItem hudToggle(DebugSession& session, std::string label, std::function<bool(const hud::Hud&)> get,
                   std::function<void(hud::Hud&, bool)> set) {
    return toggleItem(
        std::move(label),
        [&session, get = std::move(get)] {
            const hud::Hud* hud = hudOf(session);
            return hud != nullptr && get(*hud);
        },
        [&session, set = std::move(set)](bool on) {
            if (hud::Hud* hud = hudOf(session); hud != nullptr) {
                set(*hud, on);
            }
        });
}

// An action on the HUD, or a log line saying there is none.
MenuItem hudAction(DebugSession& session, std::string label, std::function<void(hud::Hud&)> act) {
    return actionItem(std::move(label), [&session, act = std::move(act)] {
        hud::Hud* hud = hudOf(session);
        if (hud == nullptr) {
            session.print(std::string(kNoHud));
            return;
        }
        act(*hud);
    });
}

// A number that overrides one of player 0's panel values; -1 gives the value back to the game.
MenuItem overrideItem(DebugSession& session, std::string label, std::optional<int> hud::PanelOverrides::* field,
                      double max) {
    MenuItem item = numberItem(
        std::move(label),
        [&session, field] {
            const hud::Hud* hud = hudOf(session);
            if (hud == nullptr) {
                return -1.0;
            }
            const std::optional<int>& value = hud->overrides(0).*field;
            return value ? static_cast<double>(*value) : -1.0;
        },
        [&session, field](double v) {
            if (hud::Hud* hud = hudOf(session); hud != nullptr) {
                hud->overrides(0).*field = v < 0.0 ? std::nullopt : std::optional<int>(static_cast<int>(v));
            }
        },
        -1.0, max, 1.0, true);
    item.defaultValue = -1.0;
    return item;
}

// The menu callbacks copy strings and call through std::function; all they can throw is a failed allocation.
// NOLINTBEGIN(bugprone-exception-escape)

// Fills the page: the parts' switches, player 0's values, and the messages to fire.
void fillHudPage(MenuPage& page, DebugSession& session) {
    if (hudOf(session) == nullptr) {
        page.add(watchItem("HUD", [] { return std::string(kNoHud); }));
        return;
    }
    page.add(watchItem("State", [&session] {
        const hud::Hud* hud = hudOf(session);
        if (hud == nullptr) {
            return std::string("-");
        }
        const hud::PlayerPanel& panel = hud->panel(0);
        return std::format("{}; panel {} fade {:.2f}; hint {}; messages {}; callback '{}'",
                           hud->visible() ? "shown" : "hidden", panel.shown() ? "shown" : "hidden", panel.fade(),
                           hud->hints().showing() ? "showing" : "none", hud->scrollIn().messages().size(),
                           hud->tutorialCallback());
    }));
    page.add(hudToggle(
                 session, "HUD shown", [](const hud::Hud& h) { return h.visible(); },
                 [](hud::Hud& h, bool on) { on ? h.showAll() : h.hideAll(); })
                 .withHelp("RestoreHud / HideHud."));
    page.add(hudToggle(
                 session, "Player panel", [](const hud::Hud& h) { return h.panel(0).mayShow(); },
                 [](hud::Hud& h, bool on) { on ? h.showPlayers() : h.hidePlayers(); })
                 .withHelp("ShowPlayerHud / HidePlayerHud."));
    page.add(hudToggle(
                 session, "Force show panel", [](const hud::Hud& h) { return h.panel(0).forceShow(); },
                 [](hud::Hud& h, bool on) { h.panel(0).setForceShow(on); })
                 .withHelp("ForceShowPlayerHud(0, on): the panel stays up instead of fading 3 s after a change."));
    page.add(hudToggle(
                 session, "Flash rage bar", [](const hud::Hud& h) { return h.panel(0).flashFrames() != 0; },
                 [](hud::Hud& h, bool on) { h.panel(0).setFlashFrames(on ? 5 : 0); })
                 .withHelp("FlashRageBar(0, 5): the meter on and off every 5 frames, as the tutorial does."));
    page.add(hudToggle(
        session, "Radar", [](const hud::Hud& h) { return h.radar().on[0]; },
        [](hud::Hud& h, bool on) { on ? h.radarOn(2) : h.radarOff(2); }));
    page.add(hudToggle(
                 session, "Instruction arrow", [](const hud::Hud& h) { return h.arrow().on; },
                 [](hud::Hud& h, bool on) { h.enableArrow(on, 0.12F, 0.2F, 3.14F); })
                 .withHelp("HUDEnableInstArrow(on, 0.12, 0.2, 3.14): pointing down at the rage meter."));
    page.add(hudToggle(
                 session, "Action prompt", [](const hud::Hud& h) { return !h.actionPrompt(0).empty(); },
                 [](hud::Hud& h, bool on) { h.setActionPrompt(0, on ? "Debug action prompt" : ""); })
                 .withHelp("Player 0's prompt at (0.5, 0.86), raised over the hint box or message below."));
    page.add(overrideItem(session, "Rage", &hud::PanelOverrides::rage, 150.0)
                 .withHelp("Player 0's rage as the panel shows it; -1 for the player's own."));
    page.add(overrideItem(session, "Score", &hud::PanelOverrides::score, 9999999.0)
                 .withHelp("The score; -1 for the game's."));
    page.add(
        overrideItem(session, "Money", &hud::PanelOverrides::money, 9999.0).withHelp("The money; -1 for the game's."));
    page.add(hudAction(session, "Give one of each item", [](hud::Hud& h) {
                 std::array<int, 4> items = h.overrides(0).items.value_or(std::array<int, 4>{});
                 for (int& count : items) {
                     ++count;
                 }
                 h.overrides(0).items = items;
             }).withHelp("A flash, a spray can, handcuffs and a key more: the counters and their slots."));
    page.add(hudAction(session, "Clear items", [](hud::Hud& h) { h.overrides(0).items.reset(); }));
    page.add(hudAction(session, "Fire an objective", [](hud::Hud& h) {
                 h.setObjective(0, "Debug objective from the HUD page", 0, false, 8000);
             }).withHelp("HUDSetObjective(0, text, 0, false, 8000)."));
    page.add(hudAction(session, "Fire a hint", [](hud::Hud& h) {
                 h.hints().queue("A hint from the debug menu: press <X> to attack, <O> to grab.", 1);
             }).withHelp("HUDSetTutorialText(text, 1)."));
    page.add(hudAction(session, "Fire an announcement", [](hud::Hud& h) {
                 h.setAnnouncement(5, "<DISPLAYTIME 4000>Announcement from the debug menu", false);
             }).withHelp("HUDSetAnnounceMsg(5, text): centred, for the 4 s its markup gives."));
    page.add(hudAction(session, "Flush hints", [](hud::Hud& h) { h.hints().flush(hud::kHintFlushAll); }));
}

// NOLINTEND(bugprone-exception-escape)

} // namespace

void addHudPage(DebugSession& session) {
    session.model().addPage("HUD", [&session](MenuPage& page) { fillHudPage(page, session); });
}

} // namespace coney::debug
