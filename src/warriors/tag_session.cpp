// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/tag_session.h"

#include <utility>

#include "warriors/inventory.h"
#include "world_objects/tag_spots.h"

namespace coney {

namespace {

// The game's start: the spot's painted fraction (0 for a spot never sprayed).
float startFraction(const world_objects::TagSpots& spots, double tag) {
    const world_objects::TagSpot* spot = spots.find(tag);
    return spot != nullptr ? spot->fraction : 0.0F;
}

} // namespace

TagSession::TagSession(world_objects::TagSpots& spots, Inventory& inventory, int player, double human, double tag,
                       std::vector<TagCell> path, TagTuning tuning)
    : m_spots(&spots), m_inventory(&inventory), m_player(player), m_human(human), m_tag(tag),
      m_game(std::move(path), startFraction(spots, tag), tuning) {
    if (m_game.result() != TagGame::Result::Playing) {
        finish();
    }
}

bool TagSession::hasPaint(const Inventory& inventory, int player) { return inventory.has(player, item::kSprayPaint); }

bool TagSession::spendCharge() { return m_inventory->give(m_player, item::kSprayPaint, -1) != 0; }

TagGame::Result TagSession::update(float stickX, float stickY, std::uint32_t elapsedMs) {
    if (m_ended) {
        return m_game.result();
    }
    const TagGame::Result result = m_game.update(stickX, stickY, elapsedMs, [this] { return spendCharge(); });
    m_spots->setFraction(m_tag, m_game.fraction());
    if (result != TagGame::Result::Playing) {
        finish();
    }
    return result;
}

void TagSession::abandon() {
    if (m_ended) {
        return;
    }
    m_game.abandon();
    finish();
}

void TagSession::finish() {
    m_ended = true;
    m_end.finished = m_game.result() == TagGame::Result::Finished;
    // Unfinished with little of the charge left, that charge is gone too.
    if (!m_end.finished && m_game.wastesCharge()) {
        m_end.wastedCharge = spendCharge();
    }
    m_spots->endSpray(m_tag, m_end.finished);
}

} // namespace coney
