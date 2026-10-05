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

RumbleSetup defaultRumbleSetup() { return gui::rumbleMenuDefaults(); }

std::optional<RumbleSetup> rumbleSetupForLevel(std::string_view level) {
    const std::optional<int> number = levelNumberOf(level);
    if (!number || *number < kFirstArena || *number > kLastArena) {
        return std::nullopt;
    }
    RumbleSetup setup = defaultRumbleSetup();
    setup.levelNumber = *number;
    return setup;
}

RumbleMenuMode::RumbleMenuMode(graphics::RenderDevice& device, SheetLoader loadSheet, GameModeStack& stack,
                               script::ScriptSystem& scripts, GameState& state,
                               std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_stack(stack), m_scripts(scripts), m_state(state),
      m_log(std::move(log)) {
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
    // The scripts' and the menu's time, as every front-end mode keeps it; the sprites of the step before are drawn.
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_scripts.setTime(nowMs);
    m_pass.empty();

    // The menu starts here rather than in enter(), which has no frame: its focus is timed from this one.
    if (m_startPending) {
        m_startPending = false;
        m_menu.start(nowMs);
    }

    // One frame of the screens, read from the HUD player's pad (port 1); a confirm writes the set-up.
    const gui::GuiFrame guiFrame{nowMs, &stack.pads().port(0)};
    const gui::RumbleMenuResult result = m_menu.update(guiFrame, stack.pads().connectedCount(), m_state.rumble);
    m_menu.render(m_canvas);
    for (std::optional<graphics::SpriteBatch>* batch : {&m_textBatch, &m_bigBatch}) {
        if (*batch) {
            m_pass.queue(**batch);
        }
    }
    if (const std::string_view screen = m_menu.screenName(); screen != m_lastScreen) {
        m_lastScreen = screen;
        m_log(std::format("rumble menu: {}\n", screen));
    }
    m_scripts.update(nowMs, frame.seconds);

    m_started = result == gui::RumbleMenuResult::Started;
    m_cancelled = result == gui::RumbleMenuResult::Cancelled;
    if (!m_started && !m_cancelled) {
        return ModeResult::Stay;
    }

    // Leave (exit() calls the callback), and for a fight close the menus below, so the level flow starts the arena.
    stack.pop();
    if (m_started && stack.topId() == ProfileManagerMode::kId) {
        stack.pop();
    }
    return ModeResult::Stay;
}

void RumbleMenuMode::render(const RenderTime& /*time*/) {
    // No front-end world yet: the screen's text on black.
    m_device.beginFrame(graphics::kBlack);
    m_pass.draw(m_device, m_camera);
    m_device.present();
}

void RumbleMenuMode::exit() {
    if (m_cancelled) {
        m_log("rumble menu: cancelled\n");
        m_scripts.call(m_onCancel);
    } else if (m_started) {
        m_log(std::format("rumble menu: start level{} ({} vs {})\n", m_state.rumble.levelNumber,
                          m_state.rumble.gangNames[0], m_state.rumble.gangNames[1]));
        const std::array<script::Value, 1> args{script::Value(static_cast<double>(m_state.rumble.levelNumber))};
        m_scripts.call(m_onStart, args);
    }
    // The queue points at the batches released below.
    m_pass.empty();
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
