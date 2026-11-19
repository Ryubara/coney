// SPDX-License-Identifier: GPL-3.0-or-later
// The humans' drops when their health runs out: PlayLevelSimulation's part of `Human_OnHealthOut`'s
// `Human_DropCarried` call (docs/research/crimes.md#down-drop).
#include <format>

#include "ai/brain.h"
#include "ai/sectors.h"
#include "characters/character_class.h"
#include "gamemodes/play_level_simulation.h"
#include "human/carried_drop.h"
#include "human/human_flags.h"
#include "scripting/script_bindings.h"
#include "warriors/inventory.h"

namespace coney {

namespace {

// How far out a dealer's pocket item goes into a free sector, metres (Sectors_GetPoint's 1.0).
constexpr float kSectorDropDistance = 1.0F;

} // namespace

void PlayLevelSimulation::stepDownDrops() {
    if (m_pickups == nullptr || m_ai == nullptr) {
        return;
    }
    // Player 1, then the AI humans, each whose health ran out this step.
    dropOnHealthOut(m_player->human(), 0);
    for (const ai::AiHuman& fighter : m_ai->humans()) {
        if (fighter.human) {
            dropOnHealthOut(*fighter.human, -1);
        }
    }
}

void PlayLevelSimulation::dropOnHealthOut(human::Human& human, int player) {
    if (!human.fighter().takeHealthOut()) {
        return;
    }
    // Human_OnHealthOut drops nothing for a human with flag 0x4 (the players and the crew), and nothing at the
    // knock-out of one who was wounded already (he dropped at the wound). Coney's stand-in: Coney's health out
    // never wounds, so his wound is the scripts' HuSetWounded alone.
    if (human.hasFlag(human::flag::kRevivable) || human.script().wounded) {
        return;
    }
    ai::Brain* brain = m_ai->brainOf(human);
    human::ScriptState& script = human.script();
    human::DropInput input;
    if (brain != nullptr) {
        input.brainType = static_cast<int>(brain->type());
        input.pedType = brain->senses().pedType;
    }
    input.player = player >= 0;
    input.pocketItem = script.pocketItem;
    input.pocketCount = script.pocketCount;
    // Human_GetCuffCount: a player's from his inventory, a cop always one, any other AI his own.
    if (input.player) {
        input.cuffs = m_pickups->carried(player, item::kHandcuffs);
    } else if (brain != nullptr && brain->type() == ai::BrainType::Cop) {
        input.cuffs = 1;
    } else {
        input.cuffs = script.cuffs;
    }
    input.carried = script.carriedItem;
    input.carriedChance = script.carriedChance;
    input.money = script.money;
    // His class's record, from the type his HuCreate gave.
    const double handle = handleOf(human);
    if (const HumanCreation* made = m_cast.humans != nullptr ? m_cast.humans->find(handle) : nullptr) {
        input.dropClass = script::recordedDropClass(m_cast.recorded, characters::characterClassOf(made->type).id);
    }
    const human::SectorBlocked blocked = [brain](int k) {
        return brain != nullptr && brain->sectors(ai::kSectorAgeMs).blocked(*brain, k);
    };
    const LevelPickups::SectorPoint point = [&human](int k) { return ai::sectorPoint(human, k, kSectorDropDistance); };
    const human::CarriedDrop drop = m_pickups->dropCarried(input, human.position(), blocked, point, player);
    script.money = drop.money;
    if (drop.carried != script.carriedItem) {
        script.carriedItem = drop.carried;
    }
    for (const human::DroppedObject& object : drop.objects) {
        m_print(std::format("drop: human {:.0f} dropped {}{}\n", handle, object.type,
                            object.money != 0 ? std::format(" ${}", object.money) : std::string()));
    }
}

} // namespace coney
