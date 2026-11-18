// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/game_humans.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"
#include "animation/anim_task.h"
#include "core/assert.h"
#include "human/human.h"

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

HumanState replicatedState(const human::Human& human) {
    HumanState state;
    state.position = human.position();
    state.heading = human.heading();
    state.health = human.fighter().health().value();
    state.healthMax = human.fighter().health().maximum();
    state.grounded = !human.airborne();
    state.knockedOut = human.knockedOut() || human.script().wounded;
    // The layers, newest first; each older layer's fade is how far the one over it has faded in.
    const anim::AnimTaskStack& tasks = human.animator().tasks();
    const std::size_t layers = std::min(tasks.layerCount(), state.layers.size());
    for (std::size_t i = 0; i < layers; ++i) {
        state.layers.at(i) = layerOf(tasks.layerTask(i));
        state.layers.at(i).fade = i == 0 ? 1.0F : tasks.layerIncoming(i - 1);
    }
    return state;
}

void GameHumans::capture(const ai::Brains& brains) {
    // Ids for the humans seen this step, new ones in brain order.
    m_humans.clear();
    std::set<const human::Human*> seen;
    for (std::size_t slot = 0; slot < brains.size(); ++slot) {
        const human::Human& human = brains.at(slot).human();
        const auto id = m_table.assign(m_ids, &human);
        CONEY_ASSERT(id.has_value()); // 65,536 ids: more humans than any level holds
        seen.insert(&human);
        m_humans.push_back(SnapshotHuman{.id = *id, .words = humanWords(replicatedState(human)), .priority = 1});
    }
    // The humans gone since the last step lose their ids.
    std::vector<const human::Human*> gone;
    m_table.forEach([&](NetId, const human::Human* handle) {
        if (!seen.contains(handle)) {
            gone.push_back(handle);
        }
    });
    for (const human::Human* handle : gone) {
        m_table.retire(m_ids, handle);
    }
}

void GameHumans::clear() {
    std::vector<const human::Human*> all;
    m_table.forEach([&all](NetId, const human::Human* handle) { all.push_back(handle); });
    for (const human::Human* handle : all) {
        m_table.retire(m_ids, handle);
    }
    m_humans.clear();
}

SnapshotHumans GameHumans::holding() const {
    SnapshotHumans humans;
    for (const SnapshotHuman& human : m_humans) {
        humans.emplace(human.id, human.words);
    }
    return humans;
}

} // namespace coney::net
