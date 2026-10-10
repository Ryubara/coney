// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/hud.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <utility>

#include "gui/text_layout.h"

namespace coney::hud {

namespace {

// Objective slots and modes.
constexpr int kObjectiveSet = 0;
constexpr int kObjectiveClear = 1;
constexpr int kObjectiveMark = 2;
constexpr int kObjectiveSetMarked = 3;
// The announcement kind that shows the script's own text.
constexpr int kAnnounceCustom = 5;
// The Armies of the Night levels, where the first-objective hint is not given.
constexpr int kArmiesFirstLevel = 60;
constexpr int kArmiesLastLevel = 64;
// The player's arrow: the widget colour (`0x005fd310`, (178, 178, 178, 255) at run time), 0.03 overlay units wide
// (`0x001c5210`).
constexpr graphics::Rgba kPlayerBlipColour{178, 178, 178, 255};
constexpr float kPlayerBlipSize = 0.03F;

// The radars a player argument names: 0 or 1 one, anything else (2, the default) both.
std::array<bool, kPlayers> radarsOf(int player) {
    if (player == 0) {
        return {true, false};
    }
    if (player == 1) {
        return {false, true};
    }
    return {true, true};
}

} // namespace

GuiPoint InstructionArrow::offset() const {
    return GuiPoint{step * std::sin(angle) / kArrowBobDivisor, -step * std::cos(angle) / kArrowBobDivisor};
}

Hud::Hud(HudServices services) : m_services(std::move(services)) {}

std::string Hud::read(const std::function<std::string(std::uint32_t)>& service, std::uint32_t id) {
    return service ? service(id) : std::string{};
}

int Hud::attachPlayer(int slot, int type) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= kPlayers) {
        return -1;
    }
    PlayerPanel& target = m_panels.at(static_cast<std::size_t>(slot));
    if (!target.attach(type)) {
        return -1;
    }
    if (!m_visible) {
        target.hide();
    }
    return slot;
}

void Hud::levelSetUp() {
    m_radar.scriptOn = true;
    // **Coney's stand-in** for HUD_Render's fade branch, which hides the spinner the memory-card screen left on at
    // the level's first fully faded frame (docs/research/hud.md#hud-spinner): a level's set-up hides it.
    m_spinner.set(false);
    // The fixed-camera icons are set up active (HUD_InitLevel); only `HUDEnableFixedCamIcon(false)` turns them off.
    for (FixedCamIcon& icon : m_fixedCam) {
        icon.setEnabled(true);
    }
    hideAll();
}

void Hud::hideAll() {
    // The Armies of the Night exception (game state +0x14c) does not arise in Coney's levels yet.
    for (PlayerPanel& panel : m_panels) {
        panel.hide();
    }
    // The radars keep their "on" flags; render() draws none of them while the HUD is hidden.
    m_visible = false;
}

void Hud::showAll() {
    // A panel shows only when attached and allowed to (PlayerPanel::show); a radar that is off stays off.
    for (PlayerPanel& panel : m_panels) {
        panel.show();
    }
    m_visible = true;
}

void Hud::hidePlayers() {
    for (PlayerPanel& panel : m_panels) {
        panel.setMayShow(false);
        panel.hide();
    }
}

void Hud::showPlayers() {
    for (PlayerPanel& panel : m_panels) {
        panel.setMayShow(true);
        panel.show();
    }
}

std::string Hud::objectiveHeader(int slot) const {
    // The objective icon (yellow for slot 0 at 0.8, blue for slot 1), the HUD colour, the heading string.
    const std::size_t index = slot == 1 ? 1 : 0;
    const std::string icon = index == 0 ? "<SIZE 0.8><YOBJ></SIZE>" : "<BOBJ>";
    const std::string colour =
        m_services.hudColour ? m_services.hudColour(kObjectiveHeaderColours.at(index)) : std::string{};
    return icon + colour + read(m_services.hudString, kObjectiveHeaderStrings.at(index));
}

void Hud::setObjective(int slot, std::string_view text, int mode, bool silent, std::uint32_t ms) {
    if (slot < 0 || static_cast<std::size_t>(slot) >= m_checklist.slots.size()) {
        return;
    }
    std::optional<ChecklistLine>& line = m_checklist.slots.at(static_cast<std::size_t>(slot));
    switch (mode) {
    case kObjectiveSet:
        line = ChecklistLine{std::string(text), false};
        if (!silent && slot < 2) {
            m_scrollIn.queue(ScrollInMessage{objectiveHeader(slot) + "<CR>" + std::string(text), kObjectiveMessagePlace,
                                             ms, kCueObjective});
        }
        // The first slot-1 objective with the game's hints on queues one hint, once.
        if (slot == 1 && m_gameTutorialText && !m_firstObjectiveHintGiven &&
            (m_levelNumber < kArmiesFirstLevel || m_levelNumber > kArmiesLastLevel)) {
            m_firstObjectiveHintGiven = true;
            m_hints.queue(read(m_services.tutorialString, kFirstObjectiveHint), kGameHintPriority);
        }
        break;
    case kObjectiveClear:
        line.reset();
        break;
    case kObjectiveMark:
        if (!line) {
            line = ChecklistLine{std::string(text), false};
        }
        line->marked = true;
        // Two messages: the heading, then the text.
        m_scrollIn.queue(ScrollInMessage{objectiveHeader(slot), kObjectiveMessagePlace, ms, kCueObjective});
        m_scrollIn.queue(ScrollInMessage{std::string(text), kObjectiveMessagePlace, ms, std::nullopt});
        break;
    case kObjectiveSetMarked:
        line = ChecklistLine{std::string(text), true};
        break;
    default:
        break;
    }
}

void Hud::removeGoalText() { m_checklist.slots.at(0).reset(); }

void Hud::setAnnouncement(int kind, std::string_view text, bool flag) {
    Announcement announcement;
    announcement.startMs = m_nowMs;
    announcement.flag = flag;
    if (kind == kAnnounceCustom) {
        announcement.text = std::string(text);
        announcement.displayMs = markupTimesOf(announcement.text).displayMs;
        m_centred = std::move(announcement);
        return;
    }
    // The built-in messages: Coney takes them from GSTRING.ANNOUNCE by kind (inferred from the five entries).
    announcement.text = read(m_services.announceString, static_cast<std::uint32_t>(kind));
    announcement.displayMs = markupTimesOf(announcement.text).displayMs;
    m_services.sound.playCue(kCueAnnounce);
    m_announcement = std::move(announcement);
}

void Hud::setWanted(int message, std::string_view crimeText) {
    constexpr int kFirstShow = 7;
    constexpr int kLastShow = 9;
    if (message < kFirstShow || message > kLastShow) {
        // Any other message clears the flag and the frame's text, so the next crime sounds the alarm again; the
        // centred copy stays for its own display time.
        m_wanted = false;
        m_crimeText.clear();
        return;
    }
    // The alarm once per wanted spell.
    if (!m_wanted) {
        m_wanted = true;
        m_services.sound.playCue(kCueWanted);
    }
    // A new text goes to the frame's widget and is copied into the centred announcement; the same text changes nothing.
    if (crimeText != m_crimeText) {
        m_crimeText = std::string(crimeText);
        setAnnouncement(kAnnounceCustom, crimeText, false);
    }
}

void ActionCycle::step() {
    // The word swaps on every multiple of framesPerIcon, the first update included; the blink halves are blinkFrames
    // long.
    if (framesPerIcon > 0 && counter % framesPerIcon == 0) {
        second = !second;
    }
    iconOn = blinkFrames == 0 || (counter / blinkFrames) % 2 == 0;
    ++counter;
}

float Hud::promptRaise(const gui::FontLookup& fonts) const {
    // Over the hint box while it shows (and no scroll-in message does), else over the scroll-in message, if any.
    if (m_hints.showing() && !m_scrollIn.showing()) {
        return kPromptRaiseOverHint - m_hints.boxHeight(fonts);
    }
    return kPromptRaiseOverMessage - m_scrollIn.showingHeight(fonts);
}

void Hud::enableArrow(bool on, float x, float y, float angle) {
    m_arrow.on = on;
    if (on) {
        m_arrow.place = GuiPoint{x, y};
        m_arrow.angle = angle;
        m_arrow.step = 0.0F;
        m_arrow.rising = true;
    }
}

void Hud::radarOn(int player) {
    const std::array<bool, kPlayers> which = radarsOf(player);
    for (std::size_t i = 0; i < kPlayers; ++i) {
        if (which.at(i)) {
            m_radar.on.at(i) = true;
        }
    }
    m_radar.scriptOn = true;
}

void Hud::radarOff(int player) {
    const std::array<bool, kPlayers> which = radarsOf(player);
    for (std::size_t i = 0; i < kPlayers; ++i) {
        if (which.at(i)) {
            m_radar.on.at(i) = false;
        }
    }
    m_radar.scriptOn = false;
}

graphics::Rgba radarTintColour(RadarTint tint) { return tint == RadarTint::Blue ? kRadarBlueColour : kRadarDiscColour; }

void Hud::setRadarTint(std::size_t player, RadarTint tint) {
    if (player >= kPlayers || m_radar.tint.at(player) == tint || (tint == RadarTint::Blue && !m_visible)) {
        return;
    }
    // A change blends from the old tint's colour, as the original keeps only the two states and the time.
    m_radar.previousTint.at(player) = m_radar.tint.at(player);
    m_radar.tint.at(player) = tint;
    m_radar.tintChangedMs.at(player) = m_nowMs;
}

graphics::Rgba Hud::radarColour(std::size_t player) const {
    const graphics::Rgba from = radarTintColour(m_radar.previousTint.at(player));
    const graphics::Rgba to = radarTintColour(m_radar.tint.at(player));
    const std::uint64_t since = m_nowMs - std::min(m_nowMs, m_radar.tintChangedMs.at(player));
    const float share = std::min(1.0F, static_cast<float>(since) / static_cast<float>(kRadarTintBlendMs));
    // Each channel moved `share` of the way, rounded.
    const auto mix = [share](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(
            std::lround(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * share));
    };
    return graphics::Rgba{mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), mix(from.a, to.a)};
}

void Hud::setNumIndicator(int player, bool on, int gang) {
    if (player < 0 || player >= static_cast<int>(kNumIndicators)) {
        return;
    }
    // No gang forces it off.
    NumIndicator& indicator = m_indicators.at(static_cast<std::size_t>(player));
    indicator.on = on && gang != -1;
    indicator.gang = gang;
}

void Hud::enableTextProgress(bool on, std::span<const std::string> labels, std::uint32_t count, std::uint32_t slot) {
    const std::size_t rows = std::min<std::size_t>(count, kTextProgressRows);
    m_progressCounts.at(slot == 1 ? 1 : 0) = static_cast<std::uint32_t>(rows);
    if (!on) {
        m_progress.fill(TextProgressRow{});
        return;
    }
    for (std::size_t i = 0; i < rows; ++i) {
        TextProgressRow& row = m_progress.at(i);
        if (!row.active) {
            row = TextProgressRow{.active = true,
                                  .label = i < labels.size() ? labels[i] : std::string{},
                                  .score = 0,
                                  .colour = graphics::kWhite};
        }
    }
}

void Hud::setTextProgress(std::string_view label, std::uint32_t value, graphics::Rgba colour, std::uint32_t slot) {
    const auto found = std::ranges::find_if(
        m_progress, [label](const TextProgressRow& row) { return row.active && row.label == label; });
    if (found == m_progress.end()) {
        return;
    }
    found->score = value;
    found->colour = colour;
    // A selection sort of the first rows, highest score first; only a strictly higher score moves up.
    const std::size_t rows = std::min<std::size_t>(m_progressCounts.at(slot == 1 ? 1 : 0), kTextProgressRows);
    for (std::size_t i = 0; i < rows; ++i) {
        std::size_t best = i;
        for (std::size_t j = i + 1; j < rows; ++j) {
            if (m_progress.at(j).score > m_progress.at(best).score) {
                best = j;
            }
        }
        if (best != i) {
            std::swap(m_progress.at(i), m_progress.at(best));
        }
    }
}

void Hud::update(const HudFrame& frame) {
    m_nowMs = frame.nowMs;
    m_levelNumber = frame.levelNumber;
    // 0. The letterbox's step: a move (in or out) arms the restore, the first step with the bars out stamps it, the
    // next shows both panels and the HUD (docs/research/hud.md#who-shows-the-hud-again).
    m_letterbox = frame.letterbox;
    if (frame.letterbox) {
        m_letterboxRestore = LetterboxRestore::Armed;
    } else if (m_letterboxRestore == LetterboxRestore::Armed) {
        m_letterboxRestore = LetterboxRestore::Stamped;
    } else if (m_letterboxRestore == LetterboxRestore::Stamped) {
        m_letterboxRestore = LetterboxRestore::Idle;
        showAll();
    }
    // While the bars are in or moving (or a fade runs) both radars go off; nothing else steps under the bars
    // (docs/research/hud.md#radars-across-a-scene).
    if (frame.screenFading || frame.letterbox) {
        m_radar.on = {false, false};
        m_radarsAutoOn = false;
    } else if (m_radar.scriptOn && !m_radarsAutoOn) {
        // Their automatic return once the bars and the fade are done, when the last radar call was "on".
        m_radar.on = {true, true};
        m_radarsAutoOn = true;
    }
    if (frame.letterbox) {
        return;
    }
    updateRadar(frame);
    // 1. The player panels, with their values and pads.
    for (std::size_t i = 0; i < kPlayers; ++i) {
        PanelValues values = frame.players.at(i);
        const PanelOverrides& overrides = m_overrides.at(i);
        values.rage = overrides.rage.value_or(values.rage);
        values.score = overrides.score.value_or(values.score);
        values.money = overrides.money.value_or(values.money);
        values.items = overrides.items.value_or(values.items);
        values.promptWakes = promptWakesPanel(m_prompts.at(i));
        m_panels.at(i).update(values, frame.pads.at(i), frame.nowMs, m_services.sound);
        m_stereo.at(i).update(frame.nowMs);
        m_mug.at(i).update();
        m_fixedCam.at(i).update(frame.cameraIgnoresStick.at(i), frame.pads.at(i), frame.nowMs);
        if (m_cycles.at(i).on) {
            m_cycles.at(i).step();
        }
    }
    // The gang-count indicators recount their gang's living members while on (NumIndicator_Update); players 0 and
    // 1's are their panels' tallies (PlayerHUD_Update).
    for (NumIndicator& indicator : m_indicators) {
        if (indicator.on) {
            indicator.count =
                frame.gangLiving ? static_cast<std::uint32_t>(std::max(0, frame.gangLiving(indicator.gang))) : 0U;
        }
    }
    for (std::size_t i = 0; i < kPlayers; ++i) {
        m_panels.at(i).setTally(m_indicators.at(i).on, m_indicators.at(i).count);
    }
    // Player 0's radar frame follows his gang's two timers (HudCrimePanel_Update).
    {
        const std::array<float, 2> timers =
            m_services.wantedTimers ? m_services.wantedTimers(0, frame.nowMs) : std::array<float, 2>{};
        m_crimePanel.update(timers[0], timers[1]);
    }
    // 2. The messages, the hint box and the counter panels on their game-time clocks; an announcement ends when its
    // `<DISPLAYTIME>` has passed.
    m_scrollIn.update(frame.nowMs, m_services.sound);
    m_hints.update(frame.nowMs, m_services.sound);
    m_counters.update(frame.nowMs);
    for (std::optional<Announcement>* announcement : {&m_announcement, &m_centred}) {
        if (*announcement && (*announcement)->displayMs &&
            frame.nowMs - (*announcement)->startMs >= *(*announcement)->displayMs) {
            announcement->reset();
        }
    }
    // 3. The arrow's bob: up by 2 a frame to the top, back down by 0.5.
    if (m_arrow.on) {
        if (m_arrow.rising) {
            m_arrow.step += kArrowStepUp;
            if (m_arrow.step >= kArrowStepMax) {
                m_arrow.step = kArrowStepMax;
                m_arrow.rising = false;
            }
        } else {
            m_arrow.step -= kArrowStepDown;
            if (m_arrow.step <= 0.0F) {
                m_arrow.step = 0.0F;
                m_arrow.rising = true;
            }
        }
    }
}

void Hud::setRadarRange(float near, float far) {
    m_radar.rest = near;
    m_radar.fast = far;
    m_radar.zoom = near;
}

void Hud::updateRadar(const HudFrame& frame) {
    // The view and the zoom's easing by the game time since the last step.
    const std::uint64_t elapsed =
        m_radar.lastMs != 0 && frame.nowMs > m_radar.lastMs ? frame.nowMs - m_radar.lastMs : 0;
    m_radar.lastMs = frame.nowMs;
    if (frame.radar.known) {
        m_radar.view = frame.radar;
        m_radar.zoom = radarZoomStep(m_radar, frame.radar.speed, static_cast<std::uint32_t>(elapsed));
    }
    // A new objective's blinking counts down by the update.
    ++m_radar.updates;
    for (auto& [handle, blip] : m_radar.blips) {
        if (blip.flashCountdown > 0) {
            --blip.flashCountdown;
        }
    }
}

void Hud::renderRadar(const HudCanvas& canvas) const {
    if (!m_radar.on.at(0) || !m_radar.view.known) {
        return;
    }
    // The disc's centre in overlay-camera space at depth 1.0; R = 0.9 × 0.19 × w / 2 of the overlay view's width,
    // stretched by the default video mode.
    const graphics::OverlayPoint centre{kRadarX, kRadarY, kRadarDepth};
    if (canvas.radarMap != nullptr && m_radar.map.usable()) {
        addRadarDisc(*canvas.radarMap, centre, kRadarRadius * kRadarStretchX, kRadarRadius * kRadarStretchY, m_radar,
                     radarColour(0));
    }
    renderBlips(canvas, centre);
    renderPlayerArrow(canvas, centre);
}

void Hud::renderCrimePanel(const HudCanvas& canvas) const {
    // The arcs share the disc's centre, untextured, among the 2D shapes.
    if (canvas.shapes != nullptr) {
        m_crimePanel.render(*canvas.shapes, graphics::OverlayPoint{kRadarX, kRadarY, kRadarDepth});
    }
}

void Hud::renderBlips(const HudCanvas& canvas, graphics::OverlayPoint centre) const {
    if (canvas.parts == nullptr || !m_locate) {
        return;
    }
    const graphics::SpriteSheet& sheet = canvas.parts->sheet();
    for (const auto& [handle, blip] : m_radar.blips) {
        // **Coney choice**: a blip whose object cannot be found is skipped, not freed (the original frees the slot
        // of a dead handle; Coney cannot yet locate every kind of object).
        const std::optional<anim::Vec3> at = m_locate(handle);
        if (!at || !radarBlipShown(blip, m_radar.updates) || blip.icon < 0 ||
            static_cast<std::size_t>(blip.icon) >= sheet.page.rects.size()) {
            continue;
        }
        const RadarOffset offset = radarBlipOffset(m_radar.view, m_radar.zoom, *at);
        const graphics::UvRect uv = sheet.page.rect(static_cast<std::size_t>(blip.icon));
        const float texWidth = sheet.texture ? static_cast<float>(sheet.texture->width()) : 0.0F;
        const float texHeight = sheet.texture ? static_cast<float>(sheet.texture->height()) : 0.0F;
        const std::array<float, 2> size = radarDotSize(uv, texWidth, texHeight, blip.scale);
        canvas.parts->addSprite(
            graphics::Sprite{graphics::OverlayPoint{centre.x + offset.x, centre.y + offset.y, centre.z}, size[0],
                             size[1], uv, blip.colour});
    }
}

void Hud::renderPlayerArrow(const HudCanvas& canvas, graphics::OverlayPoint centre) const {
    if (canvas.parts == nullptr || kRadarPlayerIcon >= canvas.parts->sheet().page.rects.size()) {
        return;
    }
    // The widget, 0.03 overlay units wide and as tall as its rectangle's texels make it (inferred: the size set is the
    // width), at the overlay's depth 1.
    const graphics::SpriteSheet& sheet = canvas.parts->sheet();
    const graphics::UvRect uv = sheet.page.rect(kRadarPlayerIcon);
    const float texWidth = sheet.texture ? static_cast<float>(sheet.texture->width()) : 1.0F;
    const float texHeight = sheet.texture ? static_cast<float>(sheet.texture->height()) : 1.0F;
    const float texelsWide = std::abs(uv.u1 - uv.u0) * texWidth;
    const float halfWidth = kPlayerBlipSize / 2.0F;
    const float halfHeight =
        texelsWide > 0.0F ? halfWidth * std::abs(uv.v1 - uv.v0) * texHeight / texelsWide : halfWidth;
    // Turned by the player's heading minus the camera's (a turn to the left turns it counter-clockwise on screen),
    // drawn as two triangles: the rotated sprite format is not built.
    const float angle = m_radar.view.heading - m_radar.view.cameraHeading;
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const auto corner = [&](float x, float y, float u, float v) {
        return graphics::OverlayVertex{
            graphics::OverlayPoint{centre.x + (x * c) - (y * s), centre.y + (x * s) + (y * c), centre.z}, u, v,
            kPlayerBlipColour};
    };
    const graphics::OverlayVertex topLeft = corner(-halfWidth, halfHeight, uv.u0, uv.v0);
    const graphics::OverlayVertex topRight = corner(halfWidth, halfHeight, uv.u1, uv.v0);
    const graphics::OverlayVertex bottomRight = corner(halfWidth, -halfHeight, uv.u1, uv.v1);
    const graphics::OverlayVertex bottomLeft = corner(-halfWidth, -halfHeight, uv.u0, uv.v1);
    canvas.parts->addTriangle(topLeft, topRight, bottomRight);
    canvas.parts->addTriangle(topLeft, bottomRight, bottomLeft);
}

void Hud::renderArrow(const HudCanvas& canvas) const {
    if (!m_arrow.on || canvas.minigames == nullptr || kArrowRect >= canvas.minigames->sheet().page.rects.size()) {
        return;
    }
    const graphics::UvRect uv = canvas.minigames->sheet().page.rect(kArrowRect);
    // A turned sprite (the rotated format 1), bobbing along its direction.
    const GuiPoint bob = m_arrow.offset();
    const float width = squareTexelWidth(canvas.minigames->sheet(), uv, kArrowSize);
    canvas.minigames->addSprite(
        guiSprite(m_arrow.place.x + bob.x, m_arrow.place.y + bob.y, width, kArrowSize, uv, kArrowColour),
        m_arrow.angle);
}

void Hud::renderScores(const HudCanvas& canvas) const {
    const graphics::FontMetrics metrics = metricsOfHeight(kScoreRowHeight);
    for (std::size_t i = 0; i < kTextProgressRows; ++i) {
        const TextProgressRow& row = m_progress.at(i);
        if (!row.active) {
            continue;
        }
        const float y = kScoreRowsPlace.y + (static_cast<float>(i) * kScoreRowStep);
        drawPlainText(canvas, gui::kTextFontSlot, row.label, kScoreRowsPlace.x, y, metrics, row.colour);
        drawPlainText(canvas, gui::kTextFontSlot, std::to_string(row.score), kScoreRowsPlace.x + kScoreValueOffset, y,
                      metrics, row.colour);
    }
    if (m_stopWatch.shown) {
        // Minutes:seconds, the seconds truncated.
        const std::int32_t ms = m_services.stopWatchTime ? std::max(0, m_services.stopWatchTime()) : 0;
        const std::int32_t seconds = ms / 1000;
        const std::string text = std::format("{}{}:{:02}", m_stopWatch.label, seconds / 60, seconds % 60);
        drawPlainText(canvas, gui::kTextFontSlot, text, kStopWatchPlace.x, kStopWatchPlace.y, metrics,
                      graphics::kWhite);
    }
}

void Hud::renderWarCommands(const HudCanvas& canvas) const {
    for (std::size_t player = 0; player < kPlayers; ++player) {
        WarCommandDisplay::Look look;
        if (m_services.commandString) {
            look.name = m_services.commandString;
        }
        if (m_services.commandEnabled) {
            look.enabled = [this, player](int command) { return m_services.commandEnabled(player, command); };
        }
        look.allLocked = m_services.commandsLocked && m_services.commandsLocked(player);
        m_warCommands.at(player).render(canvas, look, m_nowMs);
    }
}

void Hud::render(const HudCanvas& canvas) const {
    if (!m_visible || m_letterbox) {
        return;
    }
    renderRadar(canvas);
    renderCrimePanel(canvas);
    renderArrow(canvas);
    m_spinner.render(canvas);
    renderScores(canvas);
    m_counters.render(canvas);
    // The bars go below the visible counter panels, and lower again while the stopwatch shows.
    const auto panels = static_cast<float>(std::ranges::count_if(
        m_counters.panels(), [](const CounterPanel& panel) { return panel.used && panel.visible; }));
    m_bars.render(canvas, (panels * kCounterPanelRow) + (m_stopWatch.shown ? kStopWatchBarDrop : 0.0F));
    for (const PlayerPanel& panel : m_panels) {
        panel.render(canvas, m_levelNumber);
    }
    // The shared gang-count indicator, only in a Rumble level (GameState_IsRumbleLevel; Coney reads it as a level
    // numbered 100 or more).
    if (m_levelNumber >= kArcadeLevelStart) {
        m_indicators.at(kSharedNumIndicator)
            .render(canvas, m_services.language ? m_services.language() : Language::English);
    }
    // Per player, the mini-game panels.
    for (std::size_t i = 0; i < kPlayers; ++i) {
        m_lockPick.at(i).render(canvas, i);
        m_tagPanels.at(i).render(canvas, i, m_nowMs);
        m_stereo.at(i).render(canvas, i);
        if (m_mug.at(i).active()) {
            const bool mugged = m_mug.at(i).mode() == MugMeterMode::Mugged || m_mug.at(i).mode() == MugMeterMode::Held;
            const std::uint32_t id = mugged ? kMuggedPromptString : kMugPromptString;
            m_mug.at(i).render(canvas, i, m_services.hudString ? m_services.hudString(id) : std::string{});
        }
    }
    renderWarCommands(canvas);
    // The fixed-camera icon: player 0's place only (Coney has one view; the split-screen places are not used).
    m_fixedCam.at(0).render(canvas, m_nowMs);
    for (std::size_t player = 0; player < kPlayers; ++player) {
        m_mash.at(player).render(canvas, player, m_nowMs);
    }
    // The announcements' own `<DISPLAYTIME>` fades them, timed from when they were set.
    if (m_announcement) {
        gui::TextStyle style = messageStyle(kAnnouncePlace.x);
        style.timeMs = static_cast<std::uint32_t>(m_nowMs - m_announcement->startMs);
        drawMessage(canvas, m_announcement->text, style, kAnnouncePlace.y);
    }
    if (m_centred) {
        gui::TextStyle style = messageStyle(kCentredAnnouncePlace.x);
        style.alignment = gui::TextAlignment::Centre;
        style.timeMs = static_cast<std::uint32_t>(m_nowMs - m_centred->startMs);
        drawMessage(canvas, m_centred->text, style, kCentredAnnouncePlace.y);
    }
    if (!scrollInHidden()) {
        m_scrollIn.render(canvas);
    }
    if (!hintsHidden()) {
        m_hints.render(canvas);
    }
    if (!scrollInHidden()) {
        renderPrompt(canvas);
    }
}

void Hud::renderPrompt(const HudCanvas& canvas) const {
    // The cycle's icon, under the text: a `part_page0` button centred on the prompt's anchor (the base y plus the
    // raise, without the multi-line lift), 0.1 high, during the blink's on half.
    const ActionCycle& cycle = m_cycles.at(0);
    if (cycle.on && cycle.iconOn && canvas.parts != nullptr && cycle.icon() < canvas.parts->sheet().page.rects.size() &&
        canvas.text.fonts) {
        const graphics::UvRect uv = canvas.parts->sheet().page.rect(cycle.icon());
        const float y = (m_clubActionText ? kClubPromptY : kPromptPlace.y) + promptRaise(canvas.text.fonts);
        canvas.parts->addSprite(guiSprite(kPromptPlace.x, y,
                                          squareTexelWidth(canvas.parts->sheet(), uv, kCycleIconSize), kCycleIconSize,
                                          uv, kPromptColour));
    }
    const std::string& text = m_prompts.at(0);
    if (text.empty() || m_promptTextHidden.at(0) || !canvas.text.fonts) {
        return;
    }
    gui::TextStyle style;
    style.x = kPromptPlace.x;
    style.scale = metricsOfHeight(kPromptTextHeight).width * 30.0F;
    style.colour = kPromptColour;
    style.alignment = gui::TextAlignment::Centre;
    // The base y plus the raise; a prompt of several lines moves up a further step per line.
    const gui::TextLayout measured = gui::layoutText(text, style, canvas.text.fonts);
    // The clubhouse puts the text near the top (HUDEnableClubActionText).
    float y = (m_clubActionText ? kClubPromptY : kPromptPlace.y) + promptRaise(canvas.text.fonts);
    if (measured.lines > 1) {
        y -= kPromptLineRaise * static_cast<float>(measured.lines);
    }
    style.y = y;
    gui::addTextSprites(gui::layoutText(text, style, canvas.text.fonts), canvas.text.textBatch);
}

} // namespace coney::hud
