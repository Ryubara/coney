// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/level_barrier.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "net/protocol.h"

namespace coney::net {

LevelBarrier::LevelBarrier(LevelBarrierConfig config) : m_config(config) {}

const LoadLevel& LevelBarrier::load(const std::string& level, std::uint16_t checkpoint) {
    CONEY_ASSERT(checkpoint != 0);
    CONEY_ASSERT(validLevelName(level));
    // The same level again is a restart at a checkpoint (a retry, or the mission going on); any other, a change.
    const bool restart = m_current.has_value() && m_current->level == level;
    m_current = LoadLevel{.load = ++m_loads,
                          .level = level,
                          .checkpoint = checkpoint,
                          .reason = restart ? LoadReason::Restart : LoadReason::Change};
    // Every joined player loads it from now, their wait counted afresh.
    m_loading.clear();
    for (auto& [slot, steps] : m_joined) {
        m_loading.insert(slot);
        steps = 0;
    }
    return *m_current;
}

LoadLevel LevelBarrier::joinOrder() const {
    CONEY_ASSERT(m_current.has_value());
    LoadLevel order = m_current.value_or(LoadLevel{});
    order.reason = LoadReason::Join;
    return order;
}

void LevelBarrier::playerJoined(std::uint8_t slot) {
    m_joined[slot] = 0;
    if (m_current) {
        m_loading.insert(slot);
    } else {
        m_loading.erase(slot);
    }
}

void LevelBarrier::playerLeft(std::uint8_t slot) {
    m_joined.erase(slot);
    m_loading.erase(slot);
    if (m_movie) {
        m_movie->watching.erase(slot);
    }
}

LoadReply LevelBarrier::loaded(std::uint8_t slot, std::uint32_t load) {
    // A reply names a load the server ordered, from a player it holds; anything else comes from a broken peer.
    if (!m_joined.contains(slot) || !m_current || load == 0 || load > m_current->load) {
        return LoadReply::Invalid;
    }
    if (load < m_current->load) {
        return LoadReply::Late; // a newer order overtook it: the player is loading that one now
    }
    m_loading.erase(slot);
    m_joined[slot] = 0;
    return LoadReply::Accepted;
}

bool LevelBarrier::loading(std::uint8_t slot) const { return m_loading.contains(slot); }

bool LevelBarrier::ready() const { return m_loading.empty(); }

std::vector<std::uint8_t> LevelBarrier::step() {
    std::vector<std::uint8_t> timedOut;
    // m_loading is ordered, so the slots come out rising.
    for (const std::uint8_t slot : m_loading) {
        std::uint32_t& steps = m_joined[slot];
        ++steps;
        if (steps > m_config.loadTimeoutSteps) {
            timedOut.push_back(slot);
        }
    }
    if (m_movie && m_movie->hostEnded) {
        ++m_movie->stepsSinceHostEnded;
    }
    return timedOut;
}

void LevelBarrier::startMovie(std::uint32_t movieStep) {
    Movie movie{.step = movieStep};
    // A player still loading is not in the level, so not watching the level's movie.
    for (const auto& [slot, steps] : m_joined) {
        if (!m_loading.contains(slot)) {
            movie.watching.insert(slot);
        }
    }
    m_movie = std::move(movie);
}

void LevelBarrier::endHostMovie() {
    if (m_movie && !m_movie->hostEnded) {
        m_movie->hostEnded = true;
        m_movie->stepsSinceHostEnded = 0;
    }
}

void LevelBarrier::movieDone(std::uint8_t slot, std::uint32_t movieStep, bool skipped) {
    if (!m_movie || m_movie->step != movieStep || !m_movie->watching.contains(slot)) {
        return; // another movie's answer, late or early, or a player not watching it
    }
    m_movie->watching.erase(slot);
    m_movie->skipped = m_movie->skipped || skipped;
}

bool LevelBarrier::movieMayEnd() const {
    if (!m_movie) {
        return true;
    }
    if (!m_movie->hostEnded) {
        return false;
    }
    return m_movie->watching.empty() || m_movie->stepsSinceHostEnded >= m_config.movieTimeoutSteps;
}

} // namespace coney::net
