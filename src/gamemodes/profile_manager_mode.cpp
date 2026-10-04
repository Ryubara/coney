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
                                       graphics::ScreenFade& fade, script::ScriptSystem& scripts, bool europe,
                                       std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_services(services), m_fade(fade), m_scripts(scripts),
      m_log(std::move(log)), m_controller(m_shared) {
    m_shared.strings = &strings;
    m_shared.europe = europe;
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
    m_finished = false;
    m_lastScreen.clear();
    m_startPending = true;
}

ModeResult ProfileManagerMode::update(GameModeStack& stack, const FrameTime& frame) {
    // The frame the screens see: game time in milliseconds and the HUD player's pad (port 1). The scripts and the
    // fade count from the same time, so a call the menus schedule this frame is timed from it.
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_shared.frame = gui::GuiFrame{nowMs, &stack.pads().port(0)};
    m_shared.connectedPads = stack.pads().connectedCount();
    m_scripts.setTime(nowMs);
    m_fade.update(nowMs);

    // The controller starts here rather than in enter(), which has no frame: its first screen times itself from the
    // game time of the frame it is entered on.
    if (m_startPending) {
        m_startPending = false;
        m_controller.start(m_onRumble);
    }

    // No front-end world yet: black where the scene would be. Then the menus, the 2D pass and the present.
    m_device.beginFrame(graphics::kBlack);
    m_finished = m_controller.update();
    for (std::optional<graphics::SpriteBatch>* batch : {&m_menuBatch, &m_textBatch, &m_bigBatch}) {
        if (*batch) {
            m_pass.queue(**batch);
        }
    }
    m_pass.render(m_device, m_camera);
    m_fade.render(m_device);
    // The scripts' frame: the scheduled calls that are due (a fade's follow-up, the Rumble mode's launch).
    m_scripts.update(nowMs, frame.seconds);
    m_device.present();

    // Log each change of screen once, so a headless run shows how far the menus went.
    if (const std::string_view screen = m_controller.currentName(); screen != m_lastScreen) {
        m_lastScreen = screen;
        m_log(std::format("profile manager: {}\n", screen.empty() ? "done" : screen));
    }
    return m_finished ? ModeResult::Leave : ModeResult::Stay;
}

void ProfileManagerMode::exit() {
    m_controller.stop();
    // The second callback starts the chosen game, unless the flow was left another way (the Rumble-mode flag the
    // original also reads is always clear in Coney).
    if (m_finished && !m_onStartGame.empty()) {
        m_services.callScript(m_onStartGame);
    }
    m_menuBatch.reset();
    m_textBatch.reset();
    m_bigBatch.reset();
    m_textFont.reset();
    m_bigFont.reset();
    m_shared.menuSprites = nullptr;
}

void ProfileManagerMode::loadResources() {
    if (auto sheet = m_loadSheet(kMenuSheet); sheet) {
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
