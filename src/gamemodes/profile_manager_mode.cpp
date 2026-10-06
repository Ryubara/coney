// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/profile_manager_mode.h"

#include <format>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gui/text_layout.h"

namespace coney {

ProfileManagerMode::ProfileManagerMode(graphics::RenderDevice& device, SheetLoader loadSheet,
                                       const gui::GlobalStrings& strings, FrontEndServices& services,
                                       graphics::ScreenFade& fade, script::ScriptSystem& scripts, GameState& state,
                                       ProfileStore& profiles, bool europe, std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_services(services), m_fade(fade), m_scripts(scripts),
      m_profiles(profiles), m_log(std::move(log)), m_controller(m_shared) {
    m_shared.strings = &strings;
    m_shared.state = &state;
    m_shared.profiles = &profiles;
    m_shared.video.flag02 = europe;
    m_shared.fade = &m_fade;
    m_shared.playSound = [this](int cue) { m_services.playCue(cue); };
    m_shared.callScript = [this](std::string_view function, std::span<const double> args) {
        m_services.callScript(function, args);
    };
    m_shared.canvas.fonts = [this](int slot) -> const graphics::Font* {
        if (slot == gui::kBigFontSlot && m_bigFont) {
            return &*m_bigFont;
        }
        return m_textFont ? &*m_textFont : nullptr;
    };
    m_shared.canvas.textBatch = [this](int slot) { return textBatch(slot); };
}

void ProfileManagerMode::show(GameModeStack& stack, std::string onRumble, std::string onStartGame) {
    m_onRumble = std::move(onRumble);
    m_onStartGame = std::move(onStartGame);
    if (stack.topId() != kId) {
        stack.push(*this);
    }
}

void ProfileManagerMode::enter() {
    if (!m_services.musicPlaying(kMusic)) {
        m_services.playMusic(kMusic);
    }
    loadResources();
    // Nothing to blend from a time the menus were not up.
    m_fadeLevel.reset(m_fade.level());
    m_finished = false;
    m_fadingOut = false;

    m_lastScreen.clear();
    m_startPending = true;
}

ModeResult ProfileManagerMode::update(GameModeStack& stack, const FrameTime& frame) {
    // The frame the screens see: game time in milliseconds and the HUD player's pad (port 1). The scripts and the
    // fade count from the same time, so a call the menus schedule this frame is timed from it.
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_shared.frame = gui::GuiFrame{nowMs, &stack.pads().port(0)};
    m_shared.connectedPads = stack.pads().connectedCount();
    m_shared.secondPad = &stack.pads().port(1);
    m_scripts.setTime(nowMs);
    m_fade.update(nowMs);
    // The world frame under the menus: the scene's step.
    if (m_scene != nullptr) {
        m_scene->update(nowMs);
    }
    m_fadeLevel.commit();
    // The sprites of the step before are drawn; this step lists its own.
    m_pass.empty();

    // The controller starts here rather than in enter(), which has no frame: its first screen times itself from the
    // game time of the frame it is entered on.
    if (m_startPending) {
        m_startPending = false;
        m_controller.start(m_onRumble);
    }

    // The menus list their sprites for the 2D pass; the fade's level is taken where the original draws it, before
    // the scripts run.
    m_finished = m_controller.update() || m_finished;
    for (std::optional<graphics::SpriteBatch>* batch : {&m_menuBatch, &m_textBatch, &m_bigBatch, &m_frontBatch}) {
        if (*batch) {
            m_pass.queue(**batch);
        }
    }
    m_fadeLevel.current() = m_fade.level();
    // The scripts' frame: the scheduled calls that are due (a fade's follow-up, the Rumble mode's launch).
    m_scripts.update(nowMs, frame.seconds);

    // Log each change of screen once, so a headless run shows how far the menus went.
    if (const std::string_view screen = m_controller.currentName(); screen != m_lastScreen) {
        m_lastScreen = screen;
        m_log(std::format("profile manager: {}\n", screen.empty() ? "done" : screen));
    }
    // Done: fade out over 1.0 s, then leave. A flow that emptied without the flag (no screen of the original's does)
    // leaves at once.
    if (m_finished && m_shared.session.done && !m_fadingOut) {
        m_fadingOut = true;
        m_shared.finishing = true;
        m_fade.queue(graphics::ScreenFade::kFadeOut, kDoneFadeSeconds, nowMs);
        m_log("profile manager: done; fading out\n");
    }
    if (m_fadingOut) {
        return m_fade.running() ? ModeResult::Stay : ModeResult::Leave;
    }
    return m_finished ? ModeResult::Leave : ModeResult::Stay;
}

void ProfileManagerMode::render(const RenderTime& time) {
    // The menus' 2D pass, then the fade over everything.
    const auto overlay = [this, &time] {
        m_pass.draw(m_device, m_camera);
        graphics::ScreenFade::draw(m_device, lerp(m_fadeLevel.previous(), m_fadeLevel.current(), time.alpha));
    };
    // The front-end world behind them, or black without one.
    if (m_scene != nullptr) {
        m_scene->render(time, overlay);
        return;
    }
    m_device.beginFrame(graphics::kBlack);
    overlay();
    m_device.present();
}

void ProfileManagerMode::exit() {
    // A new profile from PM_Subtitles goes to the save system, with the choices the screens made.
    const gui::PmSession& session = m_shared.session;
    if (session.createOnExit && session.slot && m_shared.state != nullptr) {
        const GameState& state = *m_shared.state;
        const Profile profile{.name = session.name,
                              .difficulty = static_cast<int>(state.profileDifficulty),
                              .brightness = state.brightness,
                              .subtitles = state.subtitles};
        const bool created = m_profiles.create(*session.slot, profile);
        m_log(std::format("profile manager: profile \"{}\" {} in slot {}\n", session.name,
                          created ? "created" : "not created", *session.slot));
    }
    m_controller.stop();
    // The queue points at the batches released below.
    m_pass.empty();

    // The second callback starts the chosen game, unless the flow was left another way (the Rumble-mode flag the
    // original also reads is always clear in Coney).
    if (m_finished && !m_onStartGame.empty()) {
        m_services.callScript(m_onStartGame);
    }
    m_menuBatch.reset();
    m_frontBatch.reset();
    m_shared.frontSprites = nullptr;
    m_textBatch.reset();
    m_bigBatch.reset();
    m_textFont.reset();
    m_bigFont.reset();
    m_shared.menuSprites = nullptr;
}

void ProfileManagerMode::loadResources() {
    if (auto sheet = m_loadSheet(kMenuSheet); sheet) {
        // The sprite widgets that make their own instance at depth 11,000 draw over the text.
        m_frontBatch.emplace(*sheet, kFrontCapacity, kFrontDepth);
        m_shared.frontSprites = &*m_frontBatch;
        m_menuBatch.emplace(std::move(*sheet), kMenuCapacity, kMenuDepth);
        m_shared.menuSprites = &*m_menuBatch;
    } else {
        m_log(std::format("profile manager: {}: {}\n", kMenuSheet, sheet.error().message));
    }
    if (std::optional<graphics::Font> font = loadFont(gui::kTextFontSheet)) {
        m_textBatch.emplace(font->sheet(), kTextCapacity, kTextDepth);
        m_textFont = std::move(font);
    }
    if (std::optional<graphics::Font> font = loadFont(gui::kBigFontSheet)) {
        m_bigBatch.emplace(font->sheet(), kTextCapacity, kTextDepth);
        m_bigFont = std::move(font);
    }
}

std::optional<graphics::Font> ProfileManagerMode::loadFont(std::string_view name) {
    auto sheet = m_loadSheet(name);
    auto font = sheet ? graphics::Font::fromSheet(std::move(*sheet))
                      : std::expected<graphics::Font, Error>(std::unexpected(std::move(sheet.error())));
    if (!font) {
        m_log(std::format("profile manager: {}: {}\n", name, font.error().message));
        return std::nullopt;
    }
    return std::move(*font);
}

graphics::SpriteBatch* ProfileManagerMode::textBatch(int slot) {
    if (slot == gui::kBigFontSlot && m_bigBatch) {
        return &*m_bigBatch;
    }
    return m_textBatch ? &*m_textBatch : nullptr;
}

} // namespace coney
