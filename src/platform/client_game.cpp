// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/client_game.h"

#include <charconv>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "net/event_channel.h"
#include "net/jitter_buffer.h"
#include "net/net_status.h"
#include "net/player_huds.h"
#include "platform/net_play.h"

namespace coney::platform {

namespace {

// What a frame with no level shows: black, as between two of the game's screens.
constexpr graphics::Rgba kNoLevelColour{0, 0, 0, 255};

// The number a level's name ends in (`level99`: 99), which the HUD reads as the level's number; 0 when it has none.
int levelNumberOf(std::string_view level) {
    const std::size_t digits = level.find_first_of("0123456789");
    int number = 0;
    if (digits != std::string_view::npos) {
        std::from_chars(level.data() + digits, level.data() + level.size(), number);
    }
    return number;
}

} // namespace

ClientGame::ClientGame(NetPlay& net, ClientSceneLoader load, std::function<bool(std::string_view)> levelExists,
                       std::function<void(std::string_view)> print, graphics::RenderDevice* device,
                       ClientPresentation presentation)
    : m_net(net), m_load(std::move(load)), m_presentation(std::move(presentation)),
      m_levelExists(std::move(levelExists)), m_print(std::move(print)), m_device(device),
      m_events(m_presentation.sound, nullptr) {}

ModeResult ClientGame::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    // A session that ended takes the level with it: nothing more will come for it.
    if (m_net.role() != net::NetRole::Client && m_level) {
        m_leavePending = true;
        m_pending.reset();
    }
    if (m_leavePending) {
        m_leavePending = false;
        dropLevel();
    }
    // The host's level, loaded between two steps; in play at once, so NetLevels tells the host at its next look.
    if (m_pending) {
        const LevelAt at = std::move(*m_pending);
        m_pending.reset();
        dropLevel();
        ++m_starts;
        m_level = at;
        // The level's HUD, as its scene draws it: a fresh one for each level, with the services the run gave.
        m_hud = std::make_unique<hud::HudOwner>();
        m_hud->hud().setServices(m_presentation.hud);
        m_hudMs = 0;
        m_levelNumber = levelNumberOf(at.level);
        auto scene = m_load(at, *m_hud);
        if (scene) {
            m_scene = std::move(*scene);
            m_world = std::make_unique<net::ClientWorld>();
            m_events.setHud(&m_hud->hud());
            // The receiver still keeps the last level's snapshots; this level's world takes none of them.
            if (const net::SnapshotReceiver* snapshots = m_net.clientSnapshots(); snapshots != nullptr) {
                m_world->skipDecoded(*snapshots);
            }
            m_print(
                std::format("client game: drawing the host's level {} at checkpoint {}\n", at.level, at.checkpoint));
        } else {
            m_hud.reset();
            m_print(
                std::format("client game: the host's level {} did not load: {}\n", at.level, scene.error().message));
        }
    }
    // The world takes what the session decoded and moves on a step; then the host's events its playback has reached
    // and the HUD's step; the scene streams round the world and queues the HUD's sprites.
    if (m_world && m_scene) {
        if (const net::SnapshotReceiver* snapshots = m_net.clientSnapshots(); snapshots != nullptr) {
            m_world->receive(*snapshots);
        }
        m_world->step();
        presentEvents();
        stepHud(frame);
        m_scene->step(*m_world, frame);
    }
    // No copy of the host's movie plays here: each is over as soon as it is known, so the host waits for no copy.
    if (const std::optional<net::HostMovie> movie = m_net.hostMovie(); movie && movie->step != m_movieAnswered) {
        m_net.movieDone(movie->step, false);
        m_movieAnswered = movie->step;
    }
    return ModeResult::Stay;
}

void ClientGame::render(const RenderTime& time) {
    if (m_scene && m_world) {
        m_scene->render(*m_world, time);
        return;
    }
    if (m_device != nullptr) {
        m_device->beginFrame(kNoLevelColour);
        m_device->present();
    }
}

bool ClientGame::inPlay() const { return m_scene != nullptr && !m_pending && !m_leavePending; }

bool ClientGame::jumpToLevel(std::string_view level, int checkpoint) {
    // NetLevels asks again each step and notes the refusal once.
    if (!m_levelExists(level)) {
        return false;
    }
    m_pending = LevelAt{.level = std::string(level), .checkpoint = checkpoint};
    m_leavePending = false;
    return true;
}

bool ClientGame::leaveLevel() {
    if (!m_level && !m_pending) {
        return false;
    }
    m_pending.reset();
    m_leavePending = true;
    return true;
}

void ClientGame::presentEvents() {
    // Only once the buffer's clock runs: the host step the playback has reached, and every event up to it.
    const std::optional<std::uint64_t> shown = m_world->shownTime();
    if (!shown) {
        return;
    }
    const auto step = static_cast<std::uint32_t>(*shown / net::kTimeUnitsPerStep);
    for (const net::ArrivedEvent& arrived : m_net.takeArrivedEvents(step)) {
        m_events.apply(net::toGameEvent(arrived.event));
    }
}

void ClientGame::stepHud(const FrameTime& frame) {
    // Nothing to show until the first snapshot has said what the host's HUD holds.
    const net::SnapshotReceiver* snapshots = m_net.clientSnapshots();
    if (snapshots == nullptr || snapshots->newest() == 0) {
        return;
    }
    // The guest's own player is the HUD's one player: its slot's panel state is panel 0's, and its slot's standing
    // state the HUD's player 0's. The newest snapshot's, not the playback's: the HUD has no history to blend
    // (**Coney's choice**).
    const std::size_t slot = m_net.playerSlot();
    hud::Hud& hud = m_hud->hud();
    hud.applyStanding(net::standingForSlot(m_net.arrivedHudStanding(), slot));
    hud::HudFrame hudFrame;
    m_hudMs += static_cast<std::uint64_t>(std::lround(frame.seconds * 1000.0));
    hudFrame.nowMs = m_hudMs;
    hudFrame.levelNumber = m_levelNumber;
    hud::applyPlayerHud(hudFrame, 0, net::playerHudForSlot(m_net.arrivedHuds(), slot));
    hud.update(hudFrame);
    hud.updateHints(m_hudMs);
}

void ClientGame::dropLevel() {
    // The scene draws the HUD, so it goes first; the sounds the host started in the level end with it.
    m_scene.reset();
    m_events.setHud(nullptr);
    m_events.stopAll();
    m_hud.reset();
    m_world.reset();
    m_level.reset();
}

} // namespace coney::platform
