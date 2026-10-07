// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/rumble_menu_mode.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <expected>
#include <format>
#include <system_error>
#include <utility>

#include "core/game_timer.h"
#include "core/pad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/profile_manager_mode.h"
#include "gui/text_layout.h"
#include "scripting/lua_value.h"

namespace coney {

namespace {

// The Rumble arenas' level numbers (docs/references/levels.md).
constexpr int kFirstArena = 101;
constexpr int kLastArena = 137;

// The level number of `level` (`level102` gives 102); nothing for a name of another shape.
std::optional<int> levelNumberOf(std::string_view level) {
    constexpr std::string_view kPrefix = "level";
    if (!level.starts_with(kPrefix) || level.size() == kPrefix.size()) {
        return std::nullopt;
    }
    int number = 0;
    const std::string_view digits = level.substr(kPrefix.size());
    const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
    if (error != std::errc{} || end != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return number;
}

} // namespace

std::optional<int> rumbleArenaOf(std::string_view level) {
    const std::optional<int> number = levelNumberOf(level);
    if (!number || *number < kFirstArena || *number > kLastArena) {
        return std::nullopt;
    }
    return number;
}

RumbleMenuMode::RumbleMenuMode(graphics::RenderDevice& device, SheetLoader loadSheet, GameModeStack& stack,
                               script::ScriptSystem& scripts, GameState& state, const gui::GlobalStrings& strings,
                               gui::RumbleData& data, FrontEndServices& services, graphics::ScreenFade& fade,
                               bool europe, std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_stack(stack), m_scripts(scripts), m_state(state),
      m_log(std::move(log)), m_services(services), m_fade(fade),
      m_menu(gui::RumbleMenuServices{
          .state = &state,
          .data = &data,
          .strings = &strings,
          .runChunk = [&scripts](std::string_view chunk) { scripts.runFile(chunk); },
          .layout = gui::RumbleLayout::forFlags(europe, false, false),
      }) {
    m_canvas.fonts = [this](int slot) -> const graphics::Font* {
        if (slot == gui::kBigFontSlot && m_bigFont) {
            return &*m_bigFont;
        }
        return m_textFont ? &*m_textFont : nullptr;
    };
    m_canvas.textBatch = [this](int slot) { return textBatch(slot); };
}

void RumbleMenuMode::show(std::string onCancel, std::string onStart, bool fromFrontEnd) {
    m_onCancel = std::move(onCancel);
    m_onStart = std::move(onStart);
    m_fromFrontEnd = fromFrontEnd;
    if (m_stack.topId() != kId) {
        m_stack.push(*this);
    }
}

void RumbleMenuMode::enter() {
    m_started = false;
    m_cancelled = false;
    m_leaving = false;
    // Enter loads the bank `menu` unless it is current.
    if (!m_services.bankLoaded(ProfileManagerMode::kSoundBank)) {
        m_services.loadBank(ProfileManagerMode::kSoundBank);
    }
    // The background picture's own batch, under the text.
    if (auto sheet = m_loadSheet(kBackgroundSheet); sheet) {
        m_backgroundBatch.emplace(std::move(*sheet), kBackgroundCapacity, kBackgroundDepth);
    } else {
        m_log(std::format("rumble menu: {}: {}\n", kBackgroundSheet, sheet.error().message));
    }
    m_menu.setBackgroundBatch(m_backgroundBatch ? &*m_backgroundBatch : nullptr);
    if (std::optional<graphics::Font> font = loadFont(gui::kTextFontSheet)) {
        m_textBatch.emplace(font->sheet(), kTextCapacity, kTextDepth);
        m_textFont = std::move(font);
    }
    if (std::optional<graphics::Font> font = loadFont(gui::kBigFontSheet)) {
        m_bigBatch.emplace(font->sheet(), kTextCapacity, kTextDepth);
        m_bigFont = std::move(font);
    }
    m_lastScreen = {};
    m_startPending = true;
}

ModeResult RumbleMenuMode::update(GameModeStack& stack, const FrameTime& frame) {
    // The scripts', the fade's and the menu's time, as every front-end mode keeps it; the sprites of the step before
    // are drawn.
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_scripts.setTime(nowMs);
    m_fade.update(nowMs);
    m_pass.empty();

    // The menu starts here rather than in enter(), which has no frame: its focus is timed from this one. The first
    // frame the controller is ready queues the fade in.
    if (m_startPending) {
        m_startPending = false;
        m_menu.start(nowMs);
        m_fade.queue(graphics::ScreenFade::kFadeIn, kFadeInSeconds, nowMs);
    }

    // One frame of the screens, read from the HUD player's pad (port 1); a confirm writes the set-up. Once they have
    // ended they are frozen.
    gui::RumbleMenuResult result = gui::RumbleMenuResult::Stay;
    if (!m_leaving) {
        const gui::GuiFrame guiFrame{nowMs, &stack.pads().port(0)};
        result = m_menu.update(guiFrame, stack.pads().connectedCount());
    }
    m_menu.render(m_canvas);
    for (const int cue : m_menu.takeCues()) {
        m_services.playCue(cue);
    }
    for (std::optional<graphics::SpriteBatch>* batch : {&m_backgroundBatch, &m_textBatch, &m_bigBatch}) {
        if (*batch) {
            m_pass.queue(**batch);
        }
    }
    if (const std::string_view screen = m_menu.screenName(); screen != m_lastScreen) {
        m_lastScreen = screen;
        m_log(std::format("rumble menu: {}\n", screen));
    }
    m_scripts.update(nowMs, frame.seconds);

    // The screens' end: backing out from the front end is "cancelled" (in game the menu only closes, at once); a
    // start stops the music. Each fades out.
    if (result != gui::RumbleMenuResult::Stay) {
        m_started = result == gui::RumbleMenuResult::Started;
        m_cancelled = result == gui::RumbleMenuResult::Cancelled && m_fromFrontEnd;
        if (!m_started && !m_cancelled) {
            stack.pop();
            return ModeResult::Stay;
        }
        m_leaving = true;
        if (m_started) {
            m_services.stopMusic();
        }
        m_fade.queue(graphics::ScreenFade::kFadeOut, m_started ? kStartFadeOutSeconds : kCancelFadeOutSeconds, nowMs);
        return ModeResult::Stay;
    }

    // Leave once the fade out has run (exit() calls the callback), and for a fight close the menus below, so the level
    // flow starts the arena.
    if (m_leaving && !m_fade.running()) {
        stack.pop();
        if (m_started && stack.topId() == ProfileManagerMode::kId) {
            stack.pop();
        }
    }
    return ModeResult::Stay;
}

void RumbleMenuMode::render(const RenderTime& /*time*/) {
    // No world behind the opaque background yet: the screen on black, then the fade over it.
    m_device.beginFrame(graphics::kBlack);
    m_pass.draw(m_device, m_camera);
    graphics::ScreenFade::draw(m_device, m_fade.level());
    m_device.present();
}

void RumbleMenuMode::exit() {
    if (m_cancelled) {
        m_log("rumble menu: cancelled\n");
        m_scripts.call(m_onCancel);
    } else if (m_started) {
        if (m_launch) {
            // The developer's choice (`--rumble`) over the menu's: a fresh profile's menu offers only 1 ON 1 and WAR
            // PARTY.
            m_state.rumble.values.at(RumbleSetup::kGameType) = m_launch->gameType;
            m_state.rumble.values.at(RumbleSetup::kGangSize) = m_launch->gangSize;
            m_state.rumble.levelNumber = m_launch->arena;
            m_log(std::format("rumble menu: --rumble sets game type {}, arena {}, {} a side\n", m_launch->gameType,
                              m_launch->arena, m_launch->gangSize));
        }
        m_log(std::format("rumble menu: start level{} ({} vs {})\n", m_state.rumble.levelNumber,
                          m_state.rumble.gangNames[0], m_state.rumble.gangNames[1]));
        const std::array<script::Value, 1> args{script::Value(static_cast<double>(m_state.rumble.levelNumber))};
        m_scripts.call(m_onStart, args);
    }
    // The queue points at the batches released below.
    m_pass.empty();
    m_menu.setBackgroundBatch(nullptr);
    m_backgroundBatch.reset();
    m_textBatch.reset();
    m_bigBatch.reset();
    m_textFont.reset();
    m_bigFont.reset();
}

std::optional<graphics::Font> RumbleMenuMode::loadFont(std::string_view name) {
    auto sheet = m_loadSheet(name);
    auto font = sheet ? graphics::Font::fromSheet(std::move(*sheet))
                      : std::expected<graphics::Font, Error>(std::unexpected(std::move(sheet.error())));
    if (!font) {
        m_log(std::format("rumble menu: {}: {}\n", name, font.error().message));
        return std::nullopt;
    }
    return std::move(*font);
}

graphics::SpriteBatch* RumbleMenuMode::textBatch(int slot) {
    if (slot == gui::kBigFontSlot && m_bigBatch) {
        return &*m_bigBatch;
    }
    return m_textBatch ? &*m_textBatch : nullptr;
}

} // namespace coney
