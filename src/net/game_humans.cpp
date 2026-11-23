// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/game_humans.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"
#include "animation/anim_task.h"
#include "core/assert.h"
#include "human/human.h"
#include "net/character_id.h"
#include "net/player_huds.h"
#include "net/radar_blips.h"

namespace coney::net {

namespace {

// The top of the clip fields' range (12 bits; replicated_fields.cpp's stand-in range).
constexpr std::uint32_t kMostClip = 4095;

// One layer of an animation stack as a snapshot carries it: its clip, its playhead and, for a gait blend, its value.
HumanAnimLayer layerOf(const anim::AnimTask& task) {
    HumanAnimLayer layer;
    layer.clip = static_cast<std::int32_t>(std::min(task.animId(), kMostClip));
    if (task.type() == anim::AnimTaskType::GaitBlend) {
        const auto& gait = static_cast<const anim::GaitBlendTask&>(task);
        layer.playhead = gait.phase();
        layer.gait = gait.value();
    } else {
        layer.playhead = task.normalisedTime();
    }
    return layer;
}

} // namespace

HumanState replicatedState(const human::Human& human, std::int32_t character) {
    HumanState state;
    state.position = human.position();
    state.heading = human.heading();
    state.health = human.fighter().health().value();
    state.healthMax = human.fighter().health().maximum();
    state.grounded = !human.airborne();
    state.knockedOut = human.knockedOut() || human.script().wounded;
    state.moveStyle = human.animator().stealthStyle() ? MoveStyle::Hidden : MoveStyle::Normal;
    state.character = character;
    // The layers, newest first; each older layer's fade is how far the one over it has faded in.
    const anim::AnimTaskStack& tasks = human.animator().tasks();
    const std::size_t layers = std::min(tasks.layerCount(), state.layers.size());
    for (std::size_t i = 0; i < layers; ++i) {
        state.layers.at(i) = layerOf(tasks.layerTask(i));
        state.layers.at(i).fade = i == 0 ? 1.0F : tasks.layerIncoming(i - 1);
    }
    return state;
}

void GameHumans::capture(const ai::Brains& brains, const CharacterOf& characterOf) {
    m_humans.clear();
    if (!m_capturing) {
        return;
    }
    // Ids for the humans seen this step, new ones in brain order. A human that finds no free slot (65,536 held or
    // retired, as a long session whose removals nobody has acknowledged could leave) is left out of this step rather
    // than stopping the game; it gets an id once one is released.
    std::set<const human::Human*> seen;
    for (std::size_t slot = 0; slot < brains.size(); ++slot) {
        const human::Human& human = brains.at(slot).human();
        seen.insert(&human);
        const auto id = m_table.assign(m_ids, &human);
        if (!id) {
            continue;
        }
        // The character he is drawn as, from the game that draws him; none when nobody says.
        const std::int32_t character = characterOf ? characterOf(human) : kNoCharacter;
        m_humans.push_back(
            SnapshotHuman{.id = *id, .words = humanWords(replicatedState(human, character)), .priority = 1});
    }
    // The humans gone since the last step lose their ids.
    std::vector<const human::Human*> gone;
    m_table.forEach([&](NetId, const human::Human* handle) {
        if (!seen.contains(handle)) {
            gone.push_back(handle);
        }
    });
    for (const human::Human* handle : gone) {
        if (const auto id = m_table.retire(m_ids, handle); id) {
            m_retired.push_back(*id);
        }
    }
}

void GameHumans::captureHuds(std::span<const hud::PlayerHudState> slots, const hud::HudStanding* standing,
                             std::span<const human::Human* const> drivers) {
    if (!m_capturing) {
        return;
    }
    // The id each slot's human has now: ids, never pointers, name him on the wire.
    std::vector<NetId> humans;
    humans.reserve(drivers.size());
    for (const human::Human* driver : drivers) {
        const std::optional<NetId> id = driver != nullptr ? m_table.idOf(driver) : std::nullopt;
        humans.push_back(id.value_or(NetId{}));
    }
    m_huds = snapshotHuds(slots, humans);
    m_hudStanding = standing != nullptr ? snapshotHudStanding(*standing) : SnapshotHudStanding{};
}

void GameHumans::captureBlips(const hud::HudStanding& standing, const BlipHumanOf& followed) {
    if (!m_capturing) {
        return;
    }
    // The id the human a handle names has now: ids, never pointers, name him on the wire; a human the capture holds no
    // id for (not captured yet, or gone) is not followed, and his blip names its place.
    const BlipFollowed idOfHandle = [this, &followed](double handle) {
        const human::Human* human = followed ? followed(handle) : nullptr;
        return human != nullptr ? m_table.idOf(human).value_or(NetId{}) : NetId{};
    };
    m_blips = snapshotBlips(standing.shared.blips, m_blipSlots, idOfHandle);
}

void GameHumans::clear() {
    std::vector<const human::Human*> all;
    m_table.forEach([&all](NetId, const human::Human* handle) { all.push_back(handle); });
    for (const human::Human* handle : all) {
        if (const auto id = m_table.retire(m_ids, handle); id) {
            m_retired.push_back(*id);
        }
    }
    m_humans.clear();
    m_huds.clear();
    m_hudStanding = {};
    m_blips.clear();
    m_blipSlots.clear();
}

void GameHumans::releaseRetired() {
    for (const NetId id : m_retired) {
        const bool released = m_ids.release(id);
        CONEY_ASSERT(released); // only this class retires, each id once
    }
    m_retired.clear();
}

void GameHumans::setCapturing(bool on) {
    // Off: nobody needs the humans or can name their ids, so all of it goes.
    if (!on) {
        clear();
        releaseRetired();
    }
    m_capturing = on;
}

SnapshotHumans GameHumans::holding() const {
    SnapshotHumans humans;
    for (const SnapshotHuman& human : m_humans) {
        humans.emplace(human.id, human.words);
    }
    return humans;
}

} // namespace coney::net
