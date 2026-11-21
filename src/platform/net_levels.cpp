// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/net_levels.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "net/net_client.h"
#include "net/net_status.h"
#include "platform/game_session.h"
#include "platform/net_play.h"

namespace coney::platform {

namespace {

// How a mission ended, for the log.
std::string_view outcomeText(net::MissionOutcome outcome) {
    switch (outcome) {
    case net::MissionOutcome::Passed:
        return "passed";
    case net::MissionOutcome::Failed:
        return "failed";
    case net::MissionOutcome::Quit:
        return "quit";
    }
    return "ended";
}

} // namespace

bool SessionLevelGame::inPlay() const {
    const GameSession* session = m_session();
    return session != nullptr && session->inPlay();
}

std::uint64_t SessionLevelGame::levelStarts() const {
    const GameSession* session = m_session();
    return session != nullptr ? session->levelStarts() : 0;
}

std::optional<LevelAt> SessionLevelGame::currentLevel() const {
    const GameSession* session = m_session();
    if (session == nullptr) {
        return std::nullopt;
    }
    // Check the optional where it is dereferenced, which is what the analysis follows.
    const std::optional<std::pair<std::string, int>>& start = session->lastLevelStart();
    if (!start.has_value()) {
        return std::nullopt;
    }
    return LevelAt{.level = start->first, .checkpoint = start->second};
}

MissionCounts SessionLevelGame::missionEnds() const {
    GameSession* session = m_session();
    if (session == nullptr) {
        return {};
    }
    return MissionCounts{.passed = session->flow().missionComplete().launches(),
                         .failed = session->flow().missionFailures()};
}

bool SessionLevelGame::jumpToLevel(std::string_view level, int checkpoint) {
    GameSession* session = m_session();
    return session != nullptr && session->jumpToLevel(level, checkpoint);
}

void NetLevels::beforeStep() {
    if (m_net.role() == net::NetRole::Client) {
        // The game's own mission ends are not the session's: a guest that hosts later tells only those after.
        if (m_game != nullptr) {
            m_missions = m_game->missionEnds();
        }
        followHost();
        for (const net::MissionEnd& end : m_net.takeMissionEnds()) {
            m_net.report(std::format("the host's mission ended: {}", outcomeText(end.outcome)));
        }
        reportMovieEnd();
        return;
    }
    // Hosting, or offline (network play keeps the level for a host started later).
    m_order.reset();
    m_movieEnd.reset();
    reportHostGame();
}

bool NetLevels::holdsStep() const { return m_game != nullptr && m_net.holdsStep() && m_game->inPlay(); }

void NetLevels::reportHostGame() {
    if (m_game == nullptr) {
        return;
    }
    // A new level load: the newest one is the level the guests go to (two in one step tell only the second).
    if (const std::uint64_t starts = m_game->levelStarts(); starts != m_starts) {
        m_starts = starts;
        if (const std::optional<LevelAt> at = m_game->currentLevel(); at) {
            if (const std::optional<std::string> name = net::levelNameOf(at->level); name) {
                m_net.loadLevel(*name, static_cast<std::uint16_t>(std::clamp(at->checkpoint, 1, 0xffff)));
            } else {
                m_net.report(std::format("level {} has no network name, so no guest is told to load it", at->level));
            }
        }
    }
    // Each mission-complete and mission-failed screen since the last step.
    const MissionCounts missions = m_game->missionEnds();
    for (; m_missions.passed < missions.passed; ++m_missions.passed) {
        m_net.endMission(net::MissionOutcome::Passed);
    }
    for (; m_missions.failed < missions.failed; ++m_missions.failed) {
        m_net.endMission(net::MissionOutcome::Failed);
    }
}

void NetLevels::followHost() {
    if (std::optional<net::LoadLevel> order = m_net.takeLevelOrder(); order) {
        // A newer order replaces one not yet carried out; a movie of the level left is not answered.
        m_order = std::move(order);
        m_jumpAsked = false;
        m_refusalNoted = false;
        m_movieEnd.reset();
    }
    if (!m_order || m_game == nullptr) {
        return;
    }
    const net::LoadLevel& order = *m_order;
    if (!m_jumpAsked) {
        const LevelAt wanted{.level = order.level, .checkpoint = order.checkpoint};
        if (order.reason == net::LoadReason::Join && m_game->currentLevel() == wanted) {
            // Joining a host in the level this game already plays or loads, at the same checkpoint: its own load is
            // the host's level, answered once it plays.
            m_jumpAsked = true;
            m_startsAtJump = m_game->levelStarts() - 1;
        } else if (m_game->jumpToLevel(order.level, order.checkpoint)) {
            m_jumpAsked = true;
            m_startsAtJump = m_game->levelStarts();
            m_net.report(std::format("loading the host's level {} at checkpoint {}", order.level, order.checkpoint));
        } else if (!m_refusalNoted) {
            // Asked again each step: the level table may not be read yet.
            m_refusalNoted = true;
            m_net.report(std::format("cannot load the host's level {} yet", order.level));
        }
    }
    // Loaded once a load started since the jump reaches its first frame of play.
    if (m_jumpAsked && m_game->levelStarts() > m_startsAtJump && m_game->inPlay()) {
        m_net.levelLoaded(order.load);
        m_net.report(std::format("in the host's level {}", order.level));
        m_order.reset();
    }
}

void NetLevels::reportMovieEnd() {
    if (!m_movieEnd) {
        return;
    }
    // The host's MovieStart names the movie; this game's copy may end before it arrives.
    const std::optional<net::HostMovie> movie = m_net.hostMovie();
    if (!movie || movie->name != m_movieEnd->first || movie->step == m_movieDone) {
        return;
    }
    m_net.movieDone(movie->step, m_movieEnd->second);
    m_movieDone = movie->step;
    m_movieEnd.reset();
}

movies::MovieSync NetLevels::movieSync() {
    return movies::MovieSync{
        .started = [this](std::string_view /*name*/) { (void)m_net.startMovie(); },
        .ended =
            [this](std::string_view name, bool skipped) {
                if (m_net.role() == net::NetRole::Host) {
                    m_net.endHostMovie();
                } else if (m_net.role() == net::NetRole::Client) {
                    m_movieEnd.emplace(std::string(name), skipped);
                    reportMovieEnd();
                }
            },
        .skippedElsewhere = [this] { return m_net.movieSkipped(); },
        .mayGoOn = [this] { return m_net.movieMayEnd(); },
    };
}

} // namespace coney::platform
