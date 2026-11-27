// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/game_over.h"

#include <algorithm>

namespace coney {

bool noneAbleToHelp(std::span<const GangHelper> members) {
    // No live member: 0, so the check goes on to the route test. Otherwise any free member, player 1 too, can help.
    if (members.empty()) {
        return false;
    }
    return std::ranges::none_of(members, [](const GangHelper& m) { return !m.cuffed && !m.knockedOut && !m.held; });
}

void GameOverCheck::reset() {
    m_enabled = true;
    m_reason.reset();
    m_routeNoes = 0;
    m_handOff = kHandOffUpdates;
    m_handedOff = false;
    m_armies = false;
}

GameOverReason GameOverCheck::fail(const GameOverPlayer& first) {
    m_reason = first.cuffed ? GameOverReason::Busted : GameOverReason::Beaten;
    m_handOff = kHandOffUpdates;
    return *m_reason;
}

std::optional<GameOverReason> GameOverCheck::update(std::span<const GameOverPlayer> players, bool noneAbleToHelp,
                                                    const std::function<bool()>& canReachToHelp) {
    // The game state counts its updates before the check (`GameState_Tick`), whatever the check does.
    ++m_updates;
    // Off, failed or with no players: nothing. Game state `+0x158` would also stop it, but nothing ever sets it.
    if (!m_enabled || failed() || players.empty()) {
        return std::nullopt;
    }
    const GameOverPlayer& first = players.front();
    // 1. A dead player fails at once, always as beaten (`+0x118` = 0).
    if (std::ranges::any_of(players, &GameOverPlayer::dead)) {
        m_reason = GameOverReason::Beaten;
        m_handOff = kHandOffUpdates;
        return m_reason;
    }
    // 2. Nothing while any player is still in the fight (the route checks' count stays).
    if (std::ranges::any_of(players, [](const GameOverPlayer& p) { return !p.knockedOut && !p.cuffed; })) {
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
    // 5. Cuffed (not out) or holding a flash: wait for help, asking on every 37th update of the game state whether a
    // member can reach him; the 4th no in a row fails. Out without a flash fails at once.
    const bool canWait = (first.cuffed && !first.knockedOut) || first.holdsFlash;
    if (!canWait || first.waitBlocked) {
        return fail(first);
    }
    if (m_updates % kRouteCheckUpdates != 0) {
        return std::nullopt;
    }
    if (canReachToHelp && canReachToHelp()) {
        m_routeNoes = 0;
        return std::nullopt;
    }
    if (++m_routeNoes >= kRouteCheckFailures) {
        return fail(first);
    }
    return std::nullopt;
}

bool GameOverCheck::stepHandOff(bool crossPressed, bool fellOut) {
    if (!failed() || m_handedOff) {
        return false;
    }
    // An Armies level's countdown is capped; cross cuts it; it runs down; a fall out of the world ends it.
    if (m_armies) {
        m_handOff = std::min(m_handOff, kArmiesHandOffCap);
    }
    m_handOff = crossPressed ? std::min(m_handOff, kHandOffCut) : m_handOff;
    --m_handOff;
    if (fellOut) {
        m_handOff = 0;
    }
    if (m_handOff > 0) {
        return false;
    }
    m_handedOff = true;
    return true;
}

} // namespace coney
