// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
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
#include "hud/crime_panel.h"
#include "hud/fixed_cam_icon.h"
#include "hud/hint_box.h"
#include "hud/hud_audio.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"
#include "hud/lock_pick_hud.h"
#include "hud/mash_meter.h"
#include "hud/messages.h"
#include "hud/mug_meter.h"
#include "hud/num_indicator.h"
#include "hud/player_panel.h"
#include "hud/radar.h"
#include "hud/scripted_bars.h"
#include "hud/spinner.h"
#include "hud/stereo_hud.h"
#include "hud/tag_hud.h"
#include "hud/war_command_display.h"

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
    RadarView radar;           ///< Player 0's radar: where he is, his speed and the camera's facing.
    /// Whether each player's camera ignores the right stick: a fixed, locked, transition or rail camera, or camera
    /// switch 0 off (the fixed-camera icon, docs/research/hud.md#hud-fixed-cam-icon).
    std::array<bool, kPlayers> cameraIgnoresStick{};
    /// How many of gang `gang`'s members are alive (`0x00166158`), which the gang-count indicators show; empty counts
    /// none.
    std::function<int(int gang)> gangLiving;
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
    std::function<std::string(std::size_t entry)> commandString; ///< `GSTRING.COMMAND[entry]` (`CfgWarriorCommand`).
    /// Whether player `player` may give Warrior command `command` (`WCEnableCommand`); empty: every one.
    std::function<bool(std::size_t player, int command)> commandEnabled;
    /// Whether all of player `player`'s Warrior commands are locked (game state `+0x414` + player).
    std::function<bool(std::size_t player)> commandsLocked;
    /// Player `player`'s gang's wanted time (gang `+0x5e8`) and second timer (`+0x5f0`) left at `nowMs`, as fractions
    /// of 10 s (0 for none); the radar frame's arcs show them. Empty: none.
    std::function<std::array<float, 2>(std::size_t player, std::uint64_t nowMs)> wantedTimers;
    /// The game's language (`W_GameState + 0x120`), which picks the gang-count header's sheet and layout; empty:
    /// English.
    std::function<Language()> language;
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
    std::uint32_t counter = 0;       ///< `+0x44c`: updates since the start.
    bool second = false;             ///< `+0x450`: iconB showing rather than iconA.
    bool iconOn = true;              ///< `+0x488`: the blink's on half.
    /// One update (`ActionPrompt_UpdateCycle`): the word swaps whenever the counter is a multiple of framesPerIcon (the
    /// first update too); with blinkFrames the icon is on for that many updates, then off for as many.
    /// @orig 0x0019f328 ActionPrompt_UpdateCycle (unknown)
    void step();
    /// The `part_page0` rectangle showing now.
    [[nodiscard]] std::uint32_t icon() const { return second ? iconB : iconA; }
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
    /// A new or changed text restarts the prompt: shown again even while its cycle hid it.
    /// @orig 0x0019f1b0 ActionPrompt_SetText (unknown)
    void setActionPrompt(std::size_t player, std::string text) {
        if (text != m_prompts.at(player)) {
            m_promptTextHidden.at(player) = false;
        }
        m_prompts.at(player) = std::move(text);
    }
    [[nodiscard]] const std::string& actionPrompt(std::size_t player) const { return m_prompts.at(player); }
    /// `HUDEnableClubActionText(on)`: the prompt's text near the top of the screen (kClubPromptY, the clubhouse's
    /// place) or back at its normal place (kPromptPlace).
    /// @orig 0x001b5ea8 HUD_SetActionTextHigh (unknown)
    void setClubActionText(bool on) { m_clubActionText = on; }
    [[nodiscard]] bool clubActionText() const { return m_clubActionText; }
    /// `HUDTurnOnActionCycleAnim`: player `player`'s prompt icon cycles between two button sprites as `cycle` says,
    /// until stopActionCycle(). Starting it hides the prompt's text until the text changes.
    /// @orig 0x0019f270 ActionPrompt_StartCycle (unknown)
    void startActionCycle(std::size_t player, const ActionCycle& cycle) {
        m_cycles.at(player) = cycle;
        m_promptTextHidden.at(player) = true;
    }
    /// `HUDTurnOffActionCycleAnim`: only the cycle flag is cleared.
    /// @orig 0x0019f320 ActionPrompt_StopCycle (unknown)
    void stopActionCycle(std::size_t player) { m_cycles.at(player).on = false; }
    [[nodiscard]] const ActionCycle& actionCycle(std::size_t player) const { return m_cycles.at(player); }

    /// Player `player`'s mash meter (HUD `+0xebc0` + player × `0x430`, docs/research/hud.md#mash-meter-layout).
    [[nodiscard]] MashMeter& mashMeter(std::size_t player) { return m_mash.at(player); }
    [[nodiscard]] const MashMeter& mashMeter(std::size_t player) const { return m_mash.at(player); }
    /// Player `player`'s stereo-theft panel (HUD `+0xfea0` + player × `0xb10`, docs/research/hud.md#stereo-layout).
    [[nodiscard]] StereoHud& stereo(std::size_t player) { return m_stereo.at(player); }
    [[nodiscard]] const StereoHud& stereo(std::size_t player) const { return m_stereo.at(player); }
    /// Player `player`'s mug meter (player panel `+0x3480`, docs/research/hud.md#mug-meter-layout).
    [[nodiscard]] MugMeter& mug(std::size_t player) { return m_mug.at(player); }
    [[nodiscard]] const MugMeter& mug(std::size_t player) const { return m_mug.at(player); }
    /// Player `player`'s lock-pick dial (HUD `+0xf420` + player × `0x540`, docs/research/hud.md#lock-pick-dial-layout).
    [[nodiscard]] LockPickHud& lockPick(std::size_t player) { return m_lockPick.at(player); }
    [[nodiscard]] const LockPickHud& lockPick(std::size_t player) const { return m_lockPick.at(player); }
    /// Player `player`'s fixed-camera icon (HUD `+0x135e0` + player × `0x100`).
    [[nodiscard]] FixedCamIcon& fixedCamIcon(std::size_t player) { return m_fixedCam.at(player); }
    [[nodiscard]] const FixedCamIcon& fixedCamIcon(std::size_t player) const { return m_fixedCam.at(player); }
    /// Player `player`'s tagging panel (docs/research/hud.md#fn-after-subtitle).
    [[nodiscard]] TagHud& tagPanel(std::size_t player) { return m_tagPanels.at(player); }
    [[nodiscard]] const TagHud& tagPanel(std::size_t player) const { return m_tagPanels.at(player); }
    /// Player `player`'s Warrior command menu (panel `+0x1ef0`, docs/research/hud.md#warrior-command-menu).
    [[nodiscard]] WarCommandDisplay& warCommands(std::size_t player) { return m_warCommands.at(player); }
    [[nodiscard]] const WarCommandDisplay& warCommands(std::size_t player) const { return m_warCommands.at(player); }
    /// How far player 0's prompt is raised from its base y, from the hint box or scroll-in message showing below it,
    /// with `fonts` to measure them.
    /// @orig 0x0019f430 ActionPrompt_SetRaise (unknown)
    [[nodiscard]] float promptRaise(const gui::FontLookup& fonts) const;

    /// The counter panels.
    [[nodiscard]] CounterPanels& counterPanels() { return m_counters; }
    /// The scripts' bars (`HUDEnableBar`), below the counter panels in the right column.
    [[nodiscard]] ScriptedBars& bars() { return m_bars; }
    [[nodiscard]] const ScriptedBars& bars() const { return m_bars; }
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
    /// The level's map on the radars (`Radar_Setup`, from the level record); an unusable map draws no disc.
    /// @orig 0x001c3de0 Radar_Setup (RadarHUD.cpp)
    void setRadarMap(RadarMap map) { m_radar.map = std::move(map); }
    /// `HUDRadarSetRange(near, far)`: the radius shown at rest and at full speed; the shown radius jumps to `near`.
    /// @orig 0x001b2e10 HudManager_SetRadarRange (unknown)
    void setRadarRange(float near, float far);
    /// `HUDSetRadarZoomScale(scale)`: the factor on the radius shown; the zoom eases toward it.
    /// @orig 0x001b40e0 HUD_SetRadarZoomScale (unknown)
    void setRadarZoomScale(float scale) { m_radar.zoomScale = scale; }
    /// Finds where a blip's object is now (game axes) by its handle; nothing for a handle that names nothing placed.
    /// Empty: no blip is drawn. Its owner clears it before it goes.
    void setRadarLocator(std::function<std::optional<anim::Vec3>(double handle)> locate) {
        m_locate = std::move(locate);
    }
    [[nodiscard]] RadarState& radar() { return m_radar; }
    [[nodiscard]] const RadarState& radar() const { return m_radar; }
    /// Sets player `player`'s radar disc to `tint`, blending from the colour it shows now over kRadarTintBlendMs.
    /// Blue only while the HUD is shown (a blue asked for while it is hidden is ignored); nothing for the tint it has.
    /// @orig 0x001b26a8 HUD_RadarSetTintBlue (unknown)
    /// @orig 0x001b2790 HUD_RadarSetTintGrey (unknown)
    void setRadarTint(std::size_t player, RadarTint tint);
    /// Player `player`'s radar disc colour now: the blend from the previous tint to the current one.
    [[nodiscard]] graphics::Rgba radarColour(std::size_t player) const;

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
    /// The shared indicator's index (HUD `+0x16b30`), the only one drawn.
    static constexpr std::size_t kSharedNumIndicator = 2;
    /// `HUDSetNumIndicator(player, on, gang)`: indicator `player` (0-2) shows gang `gang`'s living members as tally
    /// marks, recounted each update; gang -1 turns it off. A bad index does nothing. Only the shared indicator is
    /// drawn, and only in a Rumble level; players 0 and 1's go to their panels' own tallies (**Coney: not drawn**,
    /// the panel tally's numbers are not worked out).
    /// @orig 0x001b4438 HUD_SetNumIndicator (unknown)
    void setNumIndicator(int player, bool on, int gang);
    [[nodiscard]] const NumIndicator& numIndicator(std::size_t index) const { return m_indicators.at(index); }

    /// The spinner (HUD `+0xe050`): the loading screens' blinking element, drawn after the arrow while shown.
    [[nodiscard]] Spinner& spinner() { return m_spinner; }
    [[nodiscard]] const Spinner& spinner() const { return m_spinner; }

    /// `HUD_SetWanted(hud, message, player)`: messages 7-9 show the crime message `crimeText` (`CfgCrimeMessage` of the
    /// game state's last crime type), any other clears it. The first show of a wanted spell plays the alarm (interface
    /// cue 2); a text that differs from the one held is copied into the centred announcement, a repeat is not (so the
    /// copy is not restarted). The radar's own wanted flag is never read, so nothing else changes on screen.
    /// @orig 0x001b2520 HUD_SetWanted (unknown)
    /// @orig 0x001aa720 HudCrimePanel_OnMessage (unknown)
    void setWanted(int message, std::string_view crimeText);
    /// Whether the radar's wanted flag is set (radar `+0x1c`): from a show until a clear.
    [[nodiscard]] bool wanted() const { return m_wanted; }

    /// Player 0's radar frame: the wanted and second-timer arcs round the radar disc.
    [[nodiscard]] const CrimePanel& crimePanel() const { return m_crimePanel; }

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

    /// Whether a mini-game panel is up for any player: the one place that lists the panels that hide the bottom-left
    /// text (a mash meter or a mug meter).
    [[nodiscard]] bool miniGamePanelShown() const {
        return std::ranges::any_of(m_mash, [](const MashMeter& meter) { return meter.shown(); }) ||
               std::ranges::any_of(m_mug, [](const MugMeter& mug) { return mug.active(); });
    }
    /// Whether the hint box, the prompts and the scroll-in messages are hidden now: a bottom-left announcement or a
    /// mini-game panel shows (and, for the hint box, a scroll-in message).
    [[nodiscard]] bool scrollInHidden() const { return m_announcement.has_value() || miniGamePanelShown(); }
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
    // The radar's step: the view, the zoom's easing, the blips' blinking.
    void updateRadar(const HudFrame& frame);
    // The radar's map disc, its blips and the player's arrow.
    void renderRadar(const HudCanvas& canvas) const;
    // The blips over the disc centred at `centre`.
    void renderBlips(const HudCanvas& canvas, graphics::OverlayPoint centre) const;
    // The player's arrow at `centre`, turned by his facing from the camera's.
    void renderPlayerArrow(const HudCanvas& canvas, graphics::OverlayPoint centre) const;
    // Player 0's radar frame's arcs round the disc.
    void renderCrimePanel(const HudCanvas& canvas) const;
    // The arrow sprite.
    void renderArrow(const HudCanvas& canvas) const;
    // The scoreboard's rows and the stopwatch (Coney's places and sizes, hud_layout.h).
    void renderScores(const HudCanvas& canvas) const;
    // The players' Warrior command menus, at the centre x the default layout gives one player.
    void renderWarCommands(const HudCanvas& canvas) const;
    // Player 0's action prompt, centred and raised clear of the text below.
    void renderPrompt(const HudCanvas& canvas) const;

    HudServices m_services;
    std::array<PlayerPanel, kPlayers> m_panels{PlayerPanel(0), PlayerPanel(1)};
    Checklist m_checklist;
    ScrollInQueue m_scrollIn;
    HintBox m_hints;
    std::string m_tutorialCallback;
    std::optional<Announcement> m_announcement; // HUD +0xe340
    std::optional<Announcement> m_centred;      // HUD +0xe150
    std::array<std::string, kPlayers> m_prompts;
    std::array<ActionCycle, kPlayers> m_cycles{};
    std::array<WarCommandDisplay, kPlayers> m_warCommands{};
    std::array<MashMeter, kPlayers> m_mash{};
    std::array<StereoHud, kPlayers> m_stereo{};
    std::array<MugMeter, kPlayers> m_mug{};
    std::array<LockPickHud, kPlayers> m_lockPick{};
    std::array<FixedCamIcon, kPlayers> m_fixedCam{};
    std::array<TagHud, kPlayers> m_tagPanels{};
    CounterPanels m_counters;
    ScriptedBars m_bars;
    InstructionArrow m_arrow;
    RadarState m_radar;
    std::function<std::optional<anim::Vec3>(double)> m_locate;
    std::array<NumIndicator, kNumIndicators> m_indicators{};
    CrimePanel m_crimePanel; // HUD +0x177d0: player 0's radar frame
    std::string m_crimeText; // the radar frame's widget +0x450: the crime message it last took
    std::array<TextProgressRow, kTextProgressRows> m_progress{};
    std::array<std::uint32_t, 2> m_progressCounts{}; // 0x00622e44 (slot 0) and 0x00622e40 (slot 1)
    StopWatchDisplay m_stopWatch;
    std::array<PanelOverrides, kPlayers> m_overrides{};
    std::uint64_t m_nowMs = 0;
    int m_levelNumber = 0;
    // The flags and the one-byte fields last, so the fields above pack without padding.
    bool m_visible = true;
    bool m_gameTutorialText = false;
    bool m_firstObjectiveHintGiven = false; // the game-state flag 0x40000
    bool m_clubActionText = false;
    std::array<bool, kPlayers> m_promptTextHidden{}; // the cycle's start hid the text, until it changes
    bool m_letterbox = false;
    // The letterbox's "restore pending" mark (screen effects +0x1f4): armed by a letterbox move, stamped as the bars
    // reach 0, and the next step with the bars out shows the HUD.
    enum class LetterboxRestore : std::uint8_t { Idle, Armed, Stamped };
    LetterboxRestore m_letterboxRestore = LetterboxRestore::Idle;
    bool m_radarsAutoOn = false; // HUD +0x177b0: the radars' automatic return has turned them on
    bool m_wanted = false;       // radar +0x1c (HUD +0x15ec)
    Spinner m_spinner;           // HUD +0xe050
};

} // namespace coney::hud
