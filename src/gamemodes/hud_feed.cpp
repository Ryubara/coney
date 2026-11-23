// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/hud_feed.h"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "combat/meters.h"
#include "combat/player_combat.h"
#include "human/fighter.h"

namespace coney {

void feedHumanToHud(hud::PlayerHudState& state, const human::Human& human) {
    // The rage meter's value, maximum and raging flag, which the panel and the health rings draw.
    const combat::RageMeter& rage = human.fighter().combat().rage();
    hud::PanelValues& values = state.panel;
    values.rage = rage.value();
    values.rageMax = rage.maximum();
    values.raging = rage.raging();
    // Knocked out by his health running out (once his dying reaction is over) or by a script (`Human_IsKnockedOut`),
    // which drops his Armies panel: Human::knockedOut(), never the script's mark alone.
    state.knockedOut = human.knockedOut();
}

std::vector<hud::CrewMember> feedCrewToHud(const ai::Brains& brains, const CreatedHumans* created,
                                           const anim::Vec3& camera) {
    std::vector<hud::CrewMember> crew;
    crew.reserve(brains.size());
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const ai::Brain& brain = brains.at(i);
        const human::Human& human = brain.human();
        hud::CrewMember member;
        member.handle = brain.handle();
        member.gang = brain.gang() != nullptr ? brain.gang()->id() : -1;
        member.brainType = static_cast<int>(brain.type());
        member.characterType = brain.characterClass();
        // His name as the scripts made him. **Coney's stand-in**: a human with no creation record has none.
        if (created != nullptr) {
            const auto record = std::ranges::find(created->all(), brain.handle(), &HumanCreation::handle);
            if (record != created->all().end()) {
                member.name = record->name;
            }
        }
        member.knockedOut = human.knockedOut();
        member.cuffed = human.script().arrested;
        // **Coney's reading** of the hostile count (`+0x152`), which Coney does not keep: another brain lists him as
        // an enemy, as ai::huntersOf() reads it.
        for (std::size_t j = 0; j < brains.size() && !member.hunted; ++j) {
            member.hunted = j != i && std::ranges::contains(brains.at(j).enemies(), &brain);
        }
        member.attacked = !brain.attackSlots().empty();
        // **Coney's stand-in** for the distance to the nearest camera (`+0x334`, Human_UpdateLod): player 1's, the
        // only camera Coney has.
        member.cameraDistance = anim::distance(human.position(), camera);
        crew.push_back(std::move(member));
    }
    return crew;
}

} // namespace coney
