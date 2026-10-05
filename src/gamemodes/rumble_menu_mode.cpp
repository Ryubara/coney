// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/rumble_menu_mode.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <format>
#include <system_error>
#include <utility>

#include "core/game_timer.h"
#include "core/pad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/profile_manager_mode.h"
#include "scripting/lua_value.h"

namespace coney {

namespace {

// The Rumble arenas' level numbers (docs/references/levels.md).
constexpr int kFirstArena = 101;
constexpr int kLastArena = 137;
// Where in the 23 values the placeholder writes (0-based): the gang size, and each gang's first character type.
constexpr std::size_t kGangSizeValue = 2;
constexpr std::size_t kGang1Types = 5;
constexpr std::size_t kGang2Types = 14;

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

RumbleSetup defaultRumbleSetup() {
    // **Coney's choice**, with the values' places as the arena scripts' ParseLuaData was seen to read them when run in
    // Coney (an observation of the game's scripts in Coney's VM, not research): 1 the game mode, 2 the game type (0,
    // the brawl), 3 the gang size, 4 and 5 the two gangs' packs, 6-14 gang 1's character types (the first is player
    // 1's), 15-23 gang 2's. One Warrior against one Rogue: Cleon (type 1) and the Rogues' leader (type 80).
    constexpr std::uint16_t kGangSize = 1;
    constexpr std::uint16_t kPlayerType = 1;
    constexpr std::uint16_t kRivalType = 80;
    RumbleSetup setup;
    setup.values.at(kGangSizeValue) = kGangSize;
    setup.values.at(kGang1Types) = kPlayerType;
    setup.values.at(kGang2Types) = kRivalType;
    setup.levelNumber = kDefaultRumbleArena;
    return setup;
}

std::optional<RumbleSetup> rumbleSetupForLevel(std::string_view level) {
    const std::optional<int> number = levelNumberOf(level);
    if (!number || *number < kFirstArena || *number > kLastArena) {
        return std::nullopt;
    }
    RumbleSetup setup = defaultRumbleSetup();
    setup.levelNumber = *number;
    return setup;
}

RumbleMenuMode::RumbleMenuMode(graphics::RenderDevice& device, GameModeStack& stack, script::ScriptSystem& scripts,
                               GameState& state, std::function<void(std::string_view)> log)
    : m_device(device), m_stack(stack), m_scripts(scripts), m_state(state), m_log(std::move(log)) {}

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
    m_state.rumble = defaultRumbleSetup();
    m_log(std::format("rumble menu: placeholder screens; level{} with the default set-up (cross starts, triangle or "
                      "circle cancels)\n",
                      m_state.rumble.levelNumber));
}

ModeResult RumbleMenuMode::update(GameModeStack& stack, const FrameTime& frame) {
    // The scripts' frame, as every front-end mode runs it.
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_scripts.setTime(nowMs);
    m_scripts.update(nowMs, frame.seconds);

    // The placeholder's two commands, on release as a menu takes them.
    const Pad& pad = stack.pads().port(0);
    if (pad.released(pad::kCross)) {
        m_started = true;
    } else if (pad.released(pad::kTriangle) || pad.released(pad::kCircle)) {
        m_cancelled = true;
    }
    if (!m_started && !m_cancelled) {
        return ModeResult::Stay;
    }

    // Leave (exit() calls the callback), and for a fight close the menus below, so the level flow starts the arena.
    const bool started = m_started;
    stack.pop();
    if (started && stack.topId() == ProfileManagerMode::kId) {
        stack.pop();
    }
    return ModeResult::Stay;
}

void RumbleMenuMode::render(const RenderTime& /*time*/) {
    m_device.beginFrame(graphics::kBlack);
    m_device.present();
}

void RumbleMenuMode::exit() {
    if (m_cancelled) {
        m_log("rumble menu: cancelled\n");
        m_scripts.call(m_onCancel);
    } else if (m_started) {
        m_log(std::format("rumble menu: start level{}\n", m_state.rumble.levelNumber));
        const std::array<script::Value, 1> args{script::Value(static_cast<double>(m_state.rumble.levelNumber))};
        m_scripts.call(m_onStart, args);
    }
}

} // namespace coney
