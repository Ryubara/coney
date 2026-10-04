// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/legal_screen_mode.h"

#include <array>
#include <format>
#include <utility>

#include "core/game_timer.h"
#include "core/name_hash.h"
#include "graphics/screen.h"

namespace coney {

std::string legalScreenResourceName(const LegalScreenSettings& settings) {
    if (settings.europe && settings.language == Language::English) {
        return "legal_screen_euro";
    }
    // The language suffixes in the order of the Language enumerators; English has none.
    static constexpr std::array<std::string_view, 5> kSuffixes{"", "_sp", "_fr", "_it", "_ge"};
    std::string name = "legal_screen";
    if (settings.widescreen) {
        name += "_w";
    }
    name += kSuffixes.at(static_cast<std::size_t>(settings.language));
    return name;
}

std::string resourceFileName(std::string_view resourceName) { return std::format("{}", crc32(resourceName)); }

LegalScreenMode::LegalScreenMode(graphics::RenderDevice& device, SheetLoader loadSheet, LegalScreenSettings settings,
                                 std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_settings(settings), m_log(std::move(log)) {}

void LegalScreenMode::enter() {
    // The original also runs the preload scripts (enum_preload.lua, config_preload.lua) here; they wait for Coney's
    // Lua system. TODO(docs/research/frontend.md#mode-flow): run them once there is one.
    m_startTicks.reset();
    const std::string name = legalScreenResourceName(m_settings);
    auto sheet = m_loadSheet(name);
    if (!sheet) {
        m_log(std::format("legal screen: {}: {}\n", name, sheet.error().message));
        return;
    }
    if (sheet->page.rects.empty() || sheet->texture == nullptr) {
        m_log(std::format("legal screen: {}: the sprite sheet has no picture\n", name));
        return;
    }
    m_sheet = std::move(*sheet);
}

ModeResult LegalScreenMode::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    // The hold counts from the moment the mode was entered: just before this first update's step, as the original
    // records its start time in Enter and ticks its timer in Update.
    if (!m_startTicks) {
        m_startTicks = frame.gameTicks - frame.stepTicks;
    }
    drawFrame();
    const std::uint64_t elapsedMs = (frame.gameTicks - *m_startTicks) / (GameTimer::kTicksPerSecond / 1000);
    return elapsedMs >= kHoldMilliseconds ? ModeResult::Leave : ModeResult::Stay;
}

void LegalScreenMode::exit() { m_sheet.reset(); }

void LegalScreenMode::drawFrame() {
    m_device.beginFrame(graphics::kBlack);
    if (m_sheet) {
        // Coney's choice: the first rectangle stretched over the whole logical screen (see the class comment).
        const graphics::LogicalQuad quad{
            0.0F, 0.0F, graphics::kLogicalWidth, graphics::kLogicalHeight, m_sheet->page.rect(0), graphics::kWhite};
        m_device.drawQuads(m_sheet->texture.get(), std::span(&quad, 1));
    }
    m_device.present();
}

} // namespace coney
