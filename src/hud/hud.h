// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core/pad.h"
#include "hud/counter_panels.h"
#include "hud/hint_box.h"
#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"
#include "hud/messages.h"
#include "hud/player_panel.h"

namespace coney::hud {

/// What one HUD step is given: the game time, both players' pads and values, whether a screen fade runs, and the
/// level's number.
struct HudFrame {
    std::uint64_t nowMs = 0;
    std::array<const Pad*, kPlayers> pads{};
    std::array<PanelValues, kPlayers> players{};
    bool screenFading = false; ///< A screen fade turns both radars off.
    bool letterbox = false;    ///< Player 1's letterbox is in or moving: nothing is drawn and the radars go off.
    int levelNumber = 0;       ///< The level record's `+0x04` (99 for the first mission).
};

/// Values the debug menus put in place of a player's own (Coney's tool, not the original's): each set field replaces
/// the value the step is given.
struct PanelOverrides {
    std::optional<int> rage;
    std::optional<int> score;
    std::optional<int> money;
    std::optional<std::array<int, 4>> items;
};

/// What the HUD reads from the rest of the game: the UI strings, the HUD colours scripts configured, and the sound.
/// Every field may be empty (null), which reads as an empty string or plays nothing.
struct HudServices {
    std::function<std::string(std::uint32_t id)> hudString;      ///< `GSTRING.HUD[id]`.
    std::function<std::string(std::uint32_t id)> tutorialString; ///< `TSTRING[id]`.
    std::function<std::string(std::uint32_t id)> announceString; ///< `GSTRING.ANNOUNCE[id]`.
    std::function<std::string(int slot)> hudColour;              ///< `CfgHUDColor(slot, colour)`'s colour markup.
    HudSound sound;                                              ///< The sound output and the interface cue table.
    std::function<std::int32_t()> stopWatchTime; ///< The mission stopwatch's time, ms (`W_GetStopWatchTime`).
};

/// The instruction arrow (`HUDEnableInstArrow`, HUD `+0x134a0`): a sprite pointing at something on screen, bobbing
/// along its direction.
struct InstructionArrow {
    bool on = false;
    GuiPoint place;
    float angle = 0.0F; ///< Radians.
    float step = 0.0F;  ///< The bob's step, 0 to kArrowStepMax.
    bool rising = true;
    std::uint32_t speed = 0; ///< `HUDSetInstArrowAnimSpeed`'s value, kept (`+0x100`).

    /// The bob's offset now: step × (sin angle, −cos angle) / 200.
    [[nodiscard]] GuiPoint offset() const;
};

/// One radar blip a script asked for, by the object's handle.
struct RadarBlip {
    int type = 10;      ///< 10 mission objective, 1 secondary objective, 7 a human (Coney: its class is not read).
    int icon = 69;      ///< `part_page0` rectangle; 69 the plain dot.
    float scale = 1.0F; ///< `HUDSetRadarItemTexture`'s scale.
    bool flashing = false;
};

/// The radars' state: on or off per player (each radar's `+0x04`), whether they come back on their own after a
/// letterbox or fade (`+0x177ac`, the scripts' last radar call), and the blips.
struct RadarState {
    std::array<bool, kPlayers> on{true, true};
    bool scriptOn = true; ///< `+0x177ac`: set by the level's set-up and `HUDTurnOnRadar`, cleared by `HUDTurnOffRadar`.
    std::map<double, RadarBlip> blips;
};

/// A number indicator (`HUDSetNumIndicator`): the remaining members of a gang, as Rumble brawls show. **Coney
/// stand-in**: its place and look are not on the page, so it is kept and not drawn.
struct NumIndicator {
    bool on = false;
    int gang = -1; ///< The gang whose count it shows; -1 for none.
};

/// One row of the text scoreboard (`HUDEnableTextProgress`, widgets at `0x00615320`): a label, its score and the
/// text's colour.
struct TextProgressRow {
    bool active = false;                      ///< `+0x434`.
    std::string label;                        ///< `+0x540`.
    std::uint32_t score = 0;                  ///< `+0x558`.
    graphics::Rgba colour = graphics::kWhite; ///< **Coney choice** until set (not on the page): white.
};

/// The text scoreboard's rows.
inline constexpr std::size_t kTextProgressRows = 6;

/// The stopwatch's display (`W_ShowStopWatch`): shown or not, and the text before the time.
struct StopWatchDisplay {
    bool shown = false; ///< Stopwatch `+0x18`.
    std::string label;  ///< `+0x1c`.
};

/// A player's action-prompt icon cycle (`HUDTurnOnActionCycleAnim`, prompt `+0x440`-`+0x45c`): two button sprites, each
/// shown `framesPerIcon` HUD updates, blinking every `blinkFrames` updates (0: no blink).
struct ActionCycle {
    bool on = false;                 ///< `+0x45c`.
    std::uint32_t framesPerIcon = 0; ///< `+0x448`.
    std::uint32_t blinkFrames = 0;   ///< `+0x458`.
    std::uint32_t iconA = 0;         ///< `+0x440`.
    std::uint32_t iconB = 0;         ///< `+0x444`.
};

/// The whole in-game HUD: the original's one static object at `0x00600840`. It holds the two player panels, the hint
/// box, the objective checklist and its scroll-in messages, the announcement, the counter panels, the instruction
/// arrow and the radars' state, and takes the HUD bindings' calls.
///
/// update() is the HUD's step (`HUD_Update`, once per mode-1 frame, on the fixed step with the game time); render()
/// adds the newest step's sprites (`HUD_Render`, the overlay pass). A mode that does not update the HUD while drawing
/// it (mission complete, mode 0xb) shows it as it was left.
///
/// A level starts with the HUD hidden (levelSetUp()); a script's `RestoreHud`, a letterbox going out or play resuming
/// shows it. Coney's choices: a HUD no level has set up yet (the debug pages, the tests) starts shown; with one player
/// only player 0's panel and radar draw.
///
/// Research: docs/research/hud.md
/// @orig 0x001acee0 HUD::HUD (unknown)
class Hud {
  public:
    /// The stand-in sheet rectangle of the radar disc: `big_font`'s circle (the map texture is not researched).
    static constexpr std::size_t kRadarDiscRect = 256;
    /// The player's own blip: `part_page0` icon 362 in grey `0x787878ff`.
    static constexpr std::size_t kRadarPlayerIcon = 362;

    explicit Hud(HudServices services = {});

    /// Replaces the services (strings, colours, sound).
    void setServices(HudServices services) { m_services = std::move(services); }
    /// The services.
    [[nodiscard]] const HudServices& services() const { return m_services; }
    /// Connects the sound output: `play` plays a sound by name (audio::SoundPlayer::play); empty for silence.
    void setSoundOutput(std::function<void(std::string_view name)> play) { m_services.sound.play = std::move(play); }

    /// `HUD_AttachPlayer(slot, type)`: attaches panel `slot` (0 or 1) to a player of character type `type` once,
    /// hidden again while the HUD is hidden. Returns the slot, or -1 for a bad or taken slot.
    /// @orig 0x001b2200 HUD_AttachPlayer (unknown)
    int attachPlayer(int slot, int type);
    /// Panel `player` (0 or 1).
    [[nodiscard]] PlayerPanel& panel(std::size_t player) { return m_panels.at(player); }
    [[nodiscard]] const PlayerPanel& panel(std::size_t player) const { return m_panels.at(player); }

    /// The HUD's per-level set-up, before the level's script runs: the radars come back on their own again
    /// (RadarState::scriptOn), and it ends with hideAll(), so a level starts with the HUD hidden.
    /// @orig 0x001ad588 HUD_LevelSetUp (HUDInterface.cpp)
    void levelSetUp();
    /// `HideHud`: hides both panels and the HUD (`+0x177a0` = 0). Nothing is saved: each part keeps its own flags (a
    /// radar keeps its "on" flag, and is simply not drawn while the HUD is hidden).
    /// @orig 0x001b1f38 HUD_HideAll (unknown)
    void hideAll();
    /// `RestoreHud`: shows the HUD and each part its own flags allow: a panel when attached and allowed to show
    /// (HidePlayerHud), a radar only when it is on. Called by the scripts and by the letterbox going out.
    /// @orig 0x001b20f8 HUD_ShowAll (unknown)
    void showAll();
    /// Whether the HUD is shown (`+0x177a0`).
    [[nodiscard]] bool visible() const { return m_visible; }
    /// `HidePlayerHud` / `ShowPlayerHud`: both panels' "may show" flag, and hide or show them.
    /// @orig 0x001b2030 HUD_HidePlayers (unknown)
    void hidePlayers();
    /// @orig 0x001b2088 HUD_ShowPlayers (unknown)
    void showPlayers();

    /// `HUDSetObjective(slot, text, mode, silent, ms)`: sets (0), clears (1), marks (2) or sets and marks (3) a line of
    /// the checklist, with the messages and the first-objective hint the page describes.
    /// @orig 0x001dad88 HUD_SetObjective (unknown)
    void setObjective(int slot, std::string_view text, int mode, bool silent, std::uint32_t ms);
    /// `HUDRemoveAllGoalText`: clears objective slot 0.
    void removeGoalText();
    /// The checklist.
    [[nodiscard]] const Checklist& checklist() const { return m_checklist; }
    /// The objectives' messages.
    [[nodiscard]] ScrollInQueue& scrollIn() { return m_scrollIn; }
    [[nodiscard]] const ScrollInQueue& scrollIn() const { return m_scrollIn; }

    /// The hint box.
    [[nodiscard]] HintBox& hints() { return m_hints; }
    [[nodiscard]] const HintBox& hints() const { return m_hints; }
    /// `HUDSetTutorialCallback(name)`: the Lua function the combat tutorial wants called, with the attacker's anim id,
    /// for every hit player 1 lands or has blocked (`Tutorial_CallCallback`, `0x001ce9a8`, called from combat, which
    /// reads it here); empty for none. Only nil (and unloading the level) clears it.
    /// @orig 0x001b5e90 Tutorial_SetCallback (unknown)
    void setTutorialCallback(std::string name) { m_tutorialCallback = std::move(name); }
    [[nodiscard]] const std::string& tutorialCallback() const { return m_tutorialCallback; }
    /// `HUDEnableGameTutorialText(on)`: the game's tutorial hints (`W_GameState + 0x56e2`).
    void setGameTutorialText(bool on) { m_gameTutorialText = on; }
    [[nodiscard]] bool gameTutorialText() const { return m_gameTutorialText; }

    /// `HUDSetAnnounceMsg(kind, text, flag)`: kind 5 shows `text` centred at (0.5, 0.25), any other the built-in
    /// announcement `kind` at the bottom left with cue 0x14. Each call replaces that widget's text at once; it lasts
    /// as its `<DISPLAYTIME>` says, else until replaced.
    /// @orig 0x001b5b08 HUD_SetAnnounceMessage (unknown)
    void setAnnouncement(int kind, std::string_view text, bool flag);
    /// The bottom-left announcement showing, if any (it hides the texts below it).
    [[nodiscard]] const std::optional<Announcement>& announcement() const { return m_announcement; }
    /// The centred custom announcement showing, if any (it hides nothing).
    [[nodiscard]] const std::optional<Announcement>& centredAnnouncement() const { return m_centred; }

    /// Sets player `player`'s action prompt (`ActionPrompt_SetText`); empty for none. In the original `HUD_Update`
    /// picks it each frame from what the player can act on; Coney's game code (or the debug menu) sets it.
    /// @orig 0x0019f1b0 ActionPrompt_SetText (unknown)
    void setActionPrompt(std::size_t player, std::string text) { m_prompts.at(player) = std::move(text); }
    [[nodiscard]] const std::string& actionPrompt(std::size_t player) const { return m_prompts.at(player); }
    /// `HUDEnableClubActionText(on)`: the prompt's text near the top of the screen (kClubPromptY, the clubhouse's
    /// place) or back at its normal place (kPromptPlace).
    /// @orig 0x001b5ea8 HUD_SetActionTextHigh (unknown)
    void setClubActionText(bool on) { m_clubActionText = on; }
    [[nodiscard]] bool clubActionText() const { return m_clubActionText; }
    /// `HUDTurnOnActionCycleAnim`: player `player`'s prompt icon cycles between two button sprites as `cycle` says,
    /// until stopActionCycle(). **Coney stand-in**: Coney's prompt draws its text only, so the cycle is kept, not
    /// drawn.
    /// @orig 0x0019f270 ActionPrompt_StartCycle (unknown)
    void startActionCycle(std::size_t player, const ActionCycle& cycle) { m_cycles.at(player) = cycle; }
    /// `HUDTurnOffActionCycleAnim`: only the cycle flag is cleared.
    /// @orig 0x0019f320 ActionPrompt_StopCycle (unknown)
    void stopActionCycle(std::size_t player) { m_cycles.at(player).on = false; }
    [[nodiscard]] const ActionCycle& actionCycle(std::size_t player) const { return m_cycles.at(player); }
    /// How far player 0's prompt is raised from its base y, from the hint box or scroll-in message showing below it,
    /// with `fonts` to measure them.
    /// @orig 0x0019f430 ActionPrompt_SetRaise (unknown)
    [[nodiscard]] float promptRaise(const gui::FontLookup& fonts) const;

    /// The counter panels.
    [[nodiscard]] CounterPanels& counterPanels() { return m_counters; }
    [[nodiscard]] const CounterPanels& counterPanels() const { return m_counters; }

    /// `HUDEnableInstArrow(on, x, y, angle)`: the place and angle apply only when turning it on.
    /// @orig 0x001b5c30 HUD_EnableInstructionArrow (unknown)
    void enableArrow(bool on, float x, float y, float angle);
    [[nodiscard]] InstructionArrow& arrow() { return m_arrow; }
    [[nodiscard]] const InstructionArrow& arrow() const { return m_arrow; }

    /// `HUDTurnOnRadar(player)` / `HUDTurnOffRadar(player)`: 0 or 1 one player's radar, 2 both.
    /// @orig 0x001b4328 HUD_RadarOn (unknown)
    void radarOn(int player);
    /// @orig 0x001b43a8 HUD_RadarOff (unknown)
    void radarOff(int player);
    [[nodiscard]] RadarState& radar() { return m_radar; }
    [[nodiscard]] const RadarState& radar() const { return m_radar; }

    /// `HUDEnableTextProgress(on, labels, count, slot)`: on, rows 0 to `count` - 1 (at most 6) not shown yet show
    /// label i with score 0; off, every row goes. Either way `count` is recorded as row count `slot` (1 the main, 0
    /// the second).
    /// @orig 0x001b5590 HUD_EnableTextProgress (unknown)
    void enableTextProgress(bool on, std::span<const std::string> labels, std::uint32_t count, std::uint32_t slot);
    /// `HUDSetTextProgress(label, value, colour, slot)`: the shown row labelled `label` takes the score and colour,
    /// then the first rows (row count `slot`) are sorted by score, highest first, ties keeping their order. A label no
    /// shown row has does nothing.
    /// @orig 0x001b57c8 HUD_SetTextProgress (unknown)
    void setTextProgress(std::string_view label, std::uint32_t value, graphics::Rgba colour, std::uint32_t slot);
    /// The scoreboard's rows, top first.
    [[nodiscard]] const std::array<TextProgressRow, kTextProgressRows>& textProgress() const { return m_progress; }

    /// `W_ShowStopWatch(show, label, ...)`: the stopwatch's time shown after `label`, as minutes:seconds.
    /// @orig 0x004235e0 StopWatch_SetDisplay (unknown)
    void showStopWatch(bool show, std::string label) {
        m_stopWatch = StopWatchDisplay{.shown = show, .label = std::move(label)};
    }
    /// The stopwatch's display.
    [[nodiscard]] const StopWatchDisplay& stopWatch() const { return m_stopWatch; }

    /// The number indicators: player 0's, player 1's and the shared one.
    static constexpr std::size_t kNumIndicators = 3;
    /// `HUDSetNumIndicator(player, on, gang)`: indicator `player` (0-2) shows gang `gang`'s count; gang -1 turns it
    /// off. A bad index does nothing.
    /// @orig 0x001b4438 HUD_SetNumIndicator (unknown)
    void setNumIndicator(int player, bool on, int gang);
    [[nodiscard]] const NumIndicator& numIndicator(std::size_t index) const { return m_indicators.at(index); }

    /// The debug menus' values for player `player`'s panel.
    [[nodiscard]] PanelOverrides& overrides(std::size_t player) { return m_overrides.at(player); }
    [[nodiscard]] const PanelOverrides& overrides(std::size_t player) const { return m_overrides.at(player); }

    /// The HUD's step at `frame.nowMs`. First the letterbox's own step: a letterbox move arms a restore, and the
    /// second step with the bars out shows the HUD again (RestoreHud), so a cinematic scene's end shows it.
    /// While the letterbox is in or moving both radars are turned off and nothing else steps; otherwise the panels
    /// with their values and pads, the hint box, the messages, the counter panels, the arrow's bob and the radars'
    /// rule (off during a fade, back on after it when `RadarState::scriptOn`).
    /// **Coney's placement**: the letterbox step is the view's screen effects' (`0x0018d910`), run here from
    /// `HudFrame::letterbox`.
    /// @orig 0x001af010 HUD_Update (HUDInterface.cpp)
    void update(const HudFrame& frame);
    /// The game time of the last step.
    [[nodiscard]] std::uint64_t nowMs() const { return m_nowMs; }
    /// The level number of the last step.
    [[nodiscard]] int levelNumber() const { return m_levelNumber; }

    /// Whether the hint box, the prompts and the scroll-in messages are hidden now: a bottom-left announcement shows
    /// (and, for the hint box, a scroll-in message).
    [[nodiscard]] bool scrollInHidden() const { return m_announcement.has_value(); }
    [[nodiscard]] bool hintsHidden() const { return scrollInHidden() || m_scrollIn.showing(); }

    /// Adds the newest step's sprites: nothing while hidden or letterboxed; else the radar, the arrow, the counter
    /// panels, the player panels, the announcements, the scroll-in messages, the hint box and last the action prompt,
    /// each unless hidden by another.
    /// @orig 0x001b1688 HUD_Render (unknown)
    void render(const HudCanvas& canvas) const;

  private:
    // The string a service gives, or an empty one.
    [[nodiscard]] static std::string read(const std::function<std::string(std::uint32_t)>& service, std::uint32_t id);
    // The objective message's header for `slot`: the icon, the HUD colour and the heading string.
    [[nodiscard]] std::string objectiveHeader(int slot) const;
    // The radar's disc and the player's icon (Coney's stand-ins for the map).
    void renderRadar(const HudCanvas& canvas) const;
    // The arrow sprite.
    void renderArrow(const HudCanvas& canvas) const;
    // The scoreboard's rows and the stopwatch (Coney's places and sizes, hud_layout.h).
    void renderScores(const HudCanvas& canvas) const;
    // Player 0's action prompt, centred and raised clear of the text below.
    void renderPrompt(const HudCanvas& canvas) const;

    HudServices m_services;
    std::array<PlayerPanel, kPlayers> m_panels{PlayerPanel(0), PlayerPanel(1)};
    bool m_visible = true;
    Checklist m_checklist;
    ScrollInQueue m_scrollIn;
    HintBox m_hints;
    std::string m_tutorialCallback;
    bool m_gameTutorialText = false;
    bool m_firstObjectiveHintGiven = false;     // the game-state flag 0x40000
    std::optional<Announcement> m_announcement; // HUD +0xe340
    std::optional<Announcement> m_centred;      // HUD +0xe150
    std::array<std::string, kPlayers> m_prompts;
    bool m_clubActionText = false;
    std::array<ActionCycle, kPlayers> m_cycles{};
    bool m_letterbox = false;
    // The letterbox's "restore pending" mark (screen effects +0x1f4): armed by a letterbox move, stamped as the bars
    // reach 0, and the next step with the bars out shows the HUD.
    enum class LetterboxRestore : std::uint8_t { Idle, Armed, Stamped };
    LetterboxRestore m_letterboxRestore = LetterboxRestore::Idle;
    bool m_radarsAutoOn = false; // HUD +0x177b0: the radars' automatic return has turned them on
    CounterPanels m_counters;
    InstructionArrow m_arrow;
    RadarState m_radar;
    std::array<NumIndicator, kNumIndicators> m_indicators{};
    std::array<TextProgressRow, kTextProgressRows> m_progress{};
    std::array<std::uint32_t, 2> m_progressCounts{}; // 0x00622e44 (slot 0) and 0x00622e40 (slot 1)
    StopWatchDisplay m_stopWatch;
    std::array<PanelOverrides, kPlayers> m_overrides{};
    std::uint64_t m_nowMs = 0;
    int m_levelNumber = 0;
};

} // namespace coney::hud
