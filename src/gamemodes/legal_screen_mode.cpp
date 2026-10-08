// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/legal_screen_mode.h"

#include <array>
#include <format>
#include <span>
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

std::pair<float, float> legalScreenFactors(const LegalScreenSettings& settings) {
    // The interlaced factors; flag 0x02 picks the picture, not the mode (docs/research/graphics.md#first-screen).
    return settings.widescreen ? std::pair{1.9F, 1.45F} : std::pair{1.55F, 1.35F};
}

graphics::LogicalQuad legalScreenQuad(const graphics::OverlayCamera& camera, std::pair<float, float> factors,
                                      const graphics::UvRect& rect) {
    // The sprite record's depth: the overlay camera's near clip. Any positive depth gives the same result.
    constexpr float kDepth = 0.5F;
    const graphics::LogicalPoint size = camera.projectSize(factors.first * kDepth * (rect.u1 - rect.u0),
                                                           factors.second * kDepth * (rect.v1 - rect.v0), kDepth);
    const graphics::LogicalPoint centre = camera.project(graphics::OverlayPoint{0.0F, 0.0F, kDepth});
    return graphics::LogicalQuad{.x = centre.x - size.x * 0.5F,
                                 .y = centre.y - size.y * 0.5F,
                                 .width = size.x,
                                 .height = size.y,
                                 .uv = rect,
                                 .colour = graphics::kWhite};
}

std::string resourceFileName(std::string_view resourceName) { return std::format("{}", crc32(resourceName)); }

void runPreloadScripts(script::ScriptSystem& scripts) {
    scripts.runFiles(script::kEnumPreloadScripts);
    scripts.runFiles(script::kConfigPreloadScripts);
}

LegalScreenMode::LegalScreenMode(graphics::RenderDevice& device, SheetLoader loadSheet, LegalScreenSettings settings,
                                 std::function<void(std::string_view)> log, script::ScriptSystem* scripts)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_settings(settings), m_log(std::move(log)),
      m_scripts(scripts) {}

void LegalScreenMode::enter() {
    // The preload scripts, in the one Lua state.
    if (m_scripts != nullptr && m_scripts->exists()) {
        runPreloadScripts(*m_scripts);
    }
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
    const std::uint64_t elapsedMs = (frame.gameTicks - *m_startTicks) / (GameTimer::kTicksPerSecond / 1000);
    return elapsedMs >= kHoldMilliseconds ? ModeResult::Leave : ModeResult::Stay;
}

void LegalScreenMode::exit() { m_sheet.reset(); }

void LegalScreenMode::render(const RenderTime& /*time*/) {

    m_device.beginFrame(graphics::kBlack);
    if (m_sheet) {
        // The first rectangle, sized as the original's sprite record sizes it, through the mode's overlay camera.
        const graphics::OverlayCamera camera = graphics::OverlayCamera::forMode(m_settings.widescreen);
        const graphics::LogicalQuad quad =
            legalScreenQuad(camera, legalScreenFactors(m_settings), m_sheet->page.rect(0));
        m_device.drawQuads(m_sheet->texture.get(), std::span(&quad, 1));
    }
    m_device.present();
}

} // namespace coney
