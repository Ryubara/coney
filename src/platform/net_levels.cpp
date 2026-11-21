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
[[maybe_unused]] std::string_view outcomeText(net::MissionOutcome outcome) {
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
    if (session == nullptr || !session->lastLevelStart()) {
        return std::nullopt;
    }
    const std::pair<std::string, int>& start = *session->lastLevelStart();
    return LevelAt{.level = start.first, .checkpoint = start.second};
}

MissionCounts SessionLevelGame::missionEnds() const {
    GameSession* session = m_session();
    if (session == nullptr) {
        return {};
    }
    return MissionCounts{.passed = session->flow().missionComplete().launches(),
                         .failed = session->flow().missionFailed().launches()};
}

bool SessionLevelGame::jumpToLevel(std::string_view level, int checkpoint) {
    GameSession* session = m_session();
    return session != nullptr && session->jumpToLevel(level, checkpoint);
}

void NetLevels::beforeStep() {}

bool NetLevels::holdsStep() const { return false; }

void NetLevels::reportHostGame() {}

void NetLevels::followHost() {}

void NetLevels::reportMovieEnd() {}

movies::MovieSync NetLevels::movieSync() { return {}; }

} // namespace coney::platform
