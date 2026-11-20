// SPDX-License-Identifier: GPL-3.0-or-later
// The gameplay mode's side of what a cinematic does to player 1's gang (scenes::SceneCrew,
// docs/research/scenes.md#gang): his bums sent away at the start; his menu unlocked, the follow forced and one gang
// update at the end. Each hook does nothing while there is no player 1, as the original's steps test his handle first.
#include <cstddef>
#include <memory>
#include <utility>

#include "ai/brain.h"
#include "ai/crew.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_hub.h"
#include "gamemodes/gameplay_mode.h"
#include "scenes/scene_player.h"
#include "scripting/story_bindings.h"
#include "warriors/story_state.h"

namespace coney {

// Wraps the maker so each level's scene system reaches player 1's gang through the hooks below.
void GameplayMode::setSceneMaker(SceneMaker maker) {
    if (!maker) {
        m_sceneMaker = nullptr;
        return;
    }
    m_sceneMaker = [this, maker = std::move(maker)]() {
        std::unique_ptr<scenes::SceneSystem> scenes = maker();
        if (scenes) {
            scenes->setCrew(&m_sceneCrew);
        }
        return scenes;
    };
}

const HumanCreation* GameplayMode::SceneCrewHooks::chief() const {
    return m_mode->m_scripted ? m_mode->m_humans.player(1) : nullptr;
}

void GameplayMode::SceneCrewHooks::clearBums() {
    if (chief() != nullptr) {
        m_mode->m_scripted->hubHost().clearBums();
    }
}

bool GameplayMode::SceneCrewHooks::isPlayer(double human) {
    // The humans `HuCreate` made players, by their player numbers (human +0x1b0).
    for (int player = 1; player <= static_cast<int>(kStoryPlayers); ++player) {
        if (const HumanCreation* made = m_mode->m_humans.player(player); made != nullptr && made->handle == human) {
            return true;
        }
    }
    return false;
}

bool GameplayMode::SceneCrewHooks::hitBack() {
    // Coney stand-in: the chief's count of Warriors hitting back at him (brain +0x2e4) is never set, as the hit-back
    // (WarriorBrain_OnHitByChief) is not built (ai::Brain::hittingBack(), #79).
    return false;
}

void GameplayMode::SceneCrewHooks::unlockCommandMenu() {
    // Player 1's own menu: his player number is 0.
    if (chief() != nullptr) {
        m_mode->m_state.story.menuLocked.at(0) = false;
    }
}

void GameplayMode::SceneCrewHooks::followForced() {
    if (const HumanCreation* player = chief(); player != nullptr) {
        script::issueWarriorCommand(m_mode->m_scripts, m_mode->m_context, player->handle, ai::kCrewFollow, true);
    }
}

void GameplayMode::SceneCrewHooks::updateGang() {
    const HumanCreation* player = chief();
    ai::Brain* brain = player != nullptr ? m_mode->m_scripted->brain(player->handle) : nullptr;
    if (ai::Gang* gang = brain != nullptr ? brain->gang() : nullptr; gang != nullptr) {
        gang->update();
    }
}

} // namespace coney
