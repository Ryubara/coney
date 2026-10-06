// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/rumble_intro_layer.h"

#include <format>
#include <utility>

#include "core/game_timer.h"

namespace coney {

RumbleIntroLayer::RumbleIntroLayer(PauseSheetLoader loadSheet, const gui::GlobalStrings& strings,
                                   FrontEndServices& services, GameState& state,
                                   std::function<void(std::string_view)> log)
    : m_loadSheet(std::move(loadSheet)), m_services(services), m_state(state), m_log(std::move(log)) {
    m_intro.setStrings(&strings);
    m_intro.setRandom(&m_state.random);
    m_intro.setDoneSink([this](std::string_view onDone) {
        m_log(std::format("rumble intro: done, calling {}\n", onDone));
        m_services.callScript(onDone);
    });
}

void RumbleIntroLayer::show(std::string_view onDone, std::span<const std::string> names) {
    m_pending = Pending{.onDone = std::string(onDone), .names = std::vector<std::string>(names.begin(), names.end())};
}

void RumbleIntroLayer::playFrame(const FrameTime& frame, const Pads& pads) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    // The intro asked for opens on this first frame of play (Coney runs the start callback before the level loads).
    if (m_pending) {
        if (!m_loaded) {
            m_layer.load(m_loadSheet, PauseRecordLoader{}, std::nullopt, 0, 0.0F, m_log, "rumble intro");
            m_loaded = true;
        }
        const auto& values = m_state.rumble.values;
        const std::array<int, 2> packs{values[RumbleSetup::kGang1Pak], values[RumbleSetup::kGang2Pak]};
        m_intro.open(m_pending->onDone, m_pending->names, packs, nowMs);
        m_log(std::format("rumble intro: {} names, then {}\n", m_intro.names().size(), m_pending->onDone));
        m_pending.reset();
    }
    if (!m_intro.isOpen()) {
        return;
    }
    m_layer.begin();
    m_intro.update(gui::GuiFrame{nowMs, &pads.port(0)});
    m_intro.render(m_layer.canvas());
    m_layer.queue();
    // Closed by this frame (the countdown ran): the fonts go.
    if (!m_intro.isOpen() && m_loaded) {
        m_layer.release();
        m_loaded = false;
    }
}

void RumbleIntroLayer::levelEnded() {
    m_pending.reset();
    m_intro.close();
    if (m_loaded) {
        m_layer.release();
        m_loaded = false;
    }
}

void RumbleIntroLayer::draw(graphics::RenderDevice& device) {
    if (m_loaded) {
        m_layer.draw(device);
    }
}

} // namespace coney
