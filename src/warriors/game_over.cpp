// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/game_over.h"

#include <algorithm>

namespace coney {

void GameOverCheck::reset() {
    m_enabled = true;
    m_reason.reset();
    m_waitUpdates = 0;
    m_routeNoes = 0;
    m_handOff = kHandOffUpdates;
    m_handedOff = false;
}

GameOverReason GameOverCheck::fail(const GameOverPlayer& first) {
    m_reason = first.cuffed ? GameOverReason::Busted : GameOverReason::Beaten;
    m_handOff = kHandOffUpdates;
    return *m_reason;
}

std::optional<GameOverReason> GameOverCheck::update(std::span<const GameOverPlayer> players, bool noneAbleToHelp,
                                                    const std::function<bool()>& canReachToHelp) {
    if (!m_enabled || failed() || players.empty()) {
        return std::nullopt;
    }
    const GameOverPlayer& first = players.front();
    // 1. A dead player fails at once.
    if (std::ranges::any_of(players, &GameOverPlayer::dead)) {
        return fail(first);
    }
    // 2. Nothing while any player is still in the fight.
    if (std::ranges::any_of(players, [](const GameOverPlayer& p) { return !p.knockedOut && !p.cuffed; })) {
        m_waitUpdates = 0;
        return std::nullopt;
    }
    // 3. A cuffed player 1 who can free himself is spared.
    if (first.cuffed && first.canFreeSelf) {
        return std::nullopt;
    }
    // 4. No one of his gang is free to help.
    if (noneAbleToHelp) {
        return fail(first);
    }
    // 5. Cuffed (not out) or holding a flash: wait for help, asking every 37 updates whether a member can reach him;
    // the 4th no in a row fails. Out without a flash fails at once.
    const bool canWait = (first.cuffed && !first.knockedOut) || first.holdsFlash;
    if (!canWait || first.waitBlocked) {
        return fail(first);
    }
    if (++m_waitUpdates < kRouteCheckUpdates) {
        return std::nullopt;
    }
    m_waitUpdates = 0;
    if (canReachToHelp && canReachToHelp()) {
        m_routeNoes = 0;
        return std::nullopt;
    }
    if (++m_routeNoes >= kRouteCheckFailures) {
        return fail(first);
    }
    return std::nullopt;
}

bool GameOverCheck::stepHandOff(bool crossPressed) {
    if (!failed() || m_handedOff) {
        return false;
    }
    m_handOff = crossPressed ? std::min(m_handOff, kHandOffCut) : m_handOff;
    if (--m_handOff > 0) {
        return false;
    }
    m_handedOff = true;
    return true;
}

} // namespace coney
