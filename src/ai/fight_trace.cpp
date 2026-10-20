// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_trace.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <utility>

#include "ai/attack_action.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/crew.h"
#include "ai/fight_book.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/give_way.h"
#include "ai/move_action.h"
#include "ai/move_to_human_action.h"
#include "ai/play_anim_action.h"
#include "ai/set_command_action.h"
#include "ai/tactic.h"
#include "ai/tactic_attack.h"
#include "ai/tactic_confront.h"
#include "ai/turn_action.h"
#include "human/fighter.h"
#include "human/human.h"

namespace coney::ai {

namespace {

constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;

// The name of the front action's kind, for the trace.
std::string_view actionName(const Action* action) {
    if (action == nullptr) {
        return "none";
    }
    if (dynamic_cast<const AttackAction*>(action) != nullptr) {
        return "attack";
    }
    if (dynamic_cast<const MoveAction*>(action) != nullptr) {
        return "move";
    }
    if (dynamic_cast<const MoveToHumanAction*>(action) != nullptr) {
        return "move_to_human";
    }
    if (dynamic_cast<const PlayAnimAction*>(action) != nullptr) {
        return "play_anim";
    }
    if (dynamic_cast<const SetCommandAction*>(action) != nullptr) {
        return "set_command";
    }
    if (dynamic_cast<const TurnAction*>(action) != nullptr) {
        return "turn";
    }
    if (dynamic_cast<const TakeStepAction*>(action) != nullptr) {
        return "take_step";
    }
    return "other";
}

// The number of goals a row lists from the bottom of the stack (goal0 to goal5).
constexpr std::size_t kStackColumns = 6;

// The record state word's bits the trace composes (docs/research/combat.md#state-flags,
// docs/research/rumble.md#gang-battle-results, human/script_state.h).
constexpr std::uint32_t kStateGrabbedFront = 0x10;
constexpr std::uint32_t kStateGrabbedRear = 0x20;
constexpr std::uint32_t kStateGrabbingFront = 0x40;
constexpr std::uint32_t kStateGrabbingRear = 0x80;
constexpr std::uint32_t kStateMounted = 0x800;
constexpr std::uint32_t kStateBlocking = 0x8000;
constexpr std::uint32_t kStateWounded = 0x10000;
constexpr std::uint32_t kStateArrested = 0x20000;
constexpr std::uint32_t kStateKnockedOut = 0x40000;
constexpr std::uint32_t kStateDown = 0x80000;
constexpr std::uint32_t kStateStunned = 0x100000;

// The attack phase bits of the record's `+0x08` that make `attacking`: the wind-up, the chain window and the end.
constexpr std::uint32_t kAttackPhases = 0x7;

// The original's tactic vtables the gang-battle recordings name (docs/research/gang-fights.md#runtime rule 1), and
// what the trace writes for a tactic the research names no vtable for (Coney's stand-in).
constexpr std::uint32_t kConfrontVtable = 0x00543740;
constexpr std::uint32_t kAttackVtable = 0x00543320;
constexpr std::uint32_t kWarriorAttackVtable = 0x00544160;
constexpr std::uint32_t kOtherTactic = 1;

// The record's state word as far as Coney keeps the states it names: the grab and tackle pair, the block, the wound,
// the arrest, the knock-out (the brain's or the scripts': `0x40000`, which only `Human_KnockOut` sets,
// docs/research/combat-reactions.md#knocked-out-bit), the ground and the stun. **Coney's**: Coney has no state word, so
// the stance (0x1, 0x2), the mugging, dying (`0x80000000`), the dead high half and the other bits are 0.
std::uint32_t stateWord(const Brain& brain) {
    const human::Human& human = brain.human();
    const human::Fighter& fighter = human.fighter();
    const human::TargetState state = human.state();
    std::uint32_t word = 0;
    if (fighter.grabbed()) {
        word |= fighter.grabbedFromRear() ? kStateGrabbedRear : kStateGrabbedFront;
    } else if (state == human::TargetState::Held) {
        word |= fighter.heldFromRear() ? kStateGrabbedRear : kStateGrabbedFront;
    } else if (state == human::TargetState::Mounted) {
        word |= kStateMounted;
    }
    if (fighter.held() != nullptr) {
        word |= fighter.fromRear() ? kStateGrabbingRear : kStateGrabbingFront;
    }
    word |= fighter.blocking() ? kStateBlocking : 0;
    word |= human.script().wounded ? kStateWounded : 0;
    word |= human.script().arrested ? kStateArrested : 0;
    word |= human.script().knockedOut || brain.knockedOut() ? kStateKnockedOut : 0;
    word |= state == human::TargetState::Grounded ? kStateDown : 0;
    word |= fighter.victim().stunned() ? kStateStunned : 0;
    return word;
}

// The `target_causes` cell: the brain's target changes so far by cause, `name=count` joined by `;`, the causes with
// none left out (empty when it never changed target), in TargetCause order.
std::string targetCauses(const Brain& brain) {
    std::string text;
    const auto& changes = brain.targetChanges();
    for (std::size_t index = 0; index < changes.size(); ++index) {
        if (changes.at(index) == 0) {
            continue;
        }
        if (!text.empty()) {
            text += ';';
        }
        text += std::format("{}={}", targetCauseName(static_cast<TargetCause>(index)), changes.at(index));
    }
    return text;
}

// The `g_tactic` value of a gang's tactic: the original's vtable for the tactics the research names, kOtherTactic for
// another, 0 for none.
std::uint32_t tacticValue(const Tactic* tactic) {
    if (tactic == nullptr) {
        return 0;
    }
    if (dynamic_cast<const TacticConfront*>(tactic) != nullptr) {
        return kConfrontVtable;
    }
    if (dynamic_cast<const TacticAttack*>(tactic) != nullptr) {
        return kAttackVtable;
    }
    if (dynamic_cast<const WarriorAttackTactic*>(tactic) != nullptr) {
        return kWarriorAttackVtable;
    }
    return kOtherTactic;
}

// The row's text under construction: values appended in column order.
class Row {
  public:
    // Appends a value of any formattable type.
    template <typename T> Row& add(const T& value) {
        if (!m_text.empty()) {
            m_text += ',';
        }
        m_text += std::format("{}", value);
        return *this;
    }
    // Appends a number with `digits` decimals.
    Row& add(float value, int digits) {
        if (!m_text.empty()) {
            m_text += ',';
        }
        m_text += std::format("{:.{}f}", value, digits);
        return *this;
    }
    // The finished row, ending in a newline.
    [[nodiscard]] std::string finish() {
        m_text += '\n';
        return std::move(m_text);
    }

  private:
    std::string m_text;
};

} // namespace

std::string_view FightTracer::header() {
    return "step,human,player,btype,gang,x,y,z,heading,speed,vz,gait,clip,clip_frame,command,health,health_max,power,"
           "stamina,state,phase,down,blocking,stunned,grabbed,grabbing,attacking,lock,target,goals,goal,goal0,goal1,"
           "goal2,goal3,goal4,goal5,react,actions,next_attack,attackable,warned,enemies,slots,slots_max,active,"
           "space_up,space_down,threat,fight_updates,fight_active,g_alert,g_tactic,g_leader,g_chosen,g_teamups,"
           "g_order,brain_off,target_dist,action_kind,attack_kind,slot_held,help_calls,helpers_sent,"
           "target_causes\n";
}

int FightTracer::idOf(const human::Human* human) {
    const auto [found, added] = m_ids.try_emplace(human, static_cast<int>(m_ids.size()));
    (void)added;
    return found->second;
}

std::string FightTracer::rows(std::uint64_t step, const Brains& brains) {
    std::string text;
    for (std::size_t slot = 0; slot < brains.size(); ++slot) {
        const Brain& brain = brains.at(slot);
        const human::Human& human = brain.human();
        const human::Fighter& fighter = human.fighter();
        const anim::Vec3 feet = human.position();
        // The clip's frame, from the playing task's time (30 frames a second).
        const anim::AnimTask* clip = human.animator().tasks().top();
        const long frame = clip != nullptr ? std::lround(clip->time() * 30.0F) : 0;
        const human::TargetState state = human.state();
        const bool held = state == human::TargetState::Held || state == human::TargetState::Mounted;
        const Goal* top = brain.topGoal();
        const Goal* reaction = brain.reactionGoal();
        const Action* front = brain.frontAction();
        const auto* attack = dynamic_cast<const AttackAction*>(front);
        // The humans it names: its brain's target, the one it has locked on, and its gang's leader and chosen target
        // (a human not seen as a row yet is numbered on the spot).
        const Brain* target = brain.target();
        const auto* locked = dynamic_cast<const human::Human*>(fighter.target());
        const Gang* gang = brain.gang();
        const Brain* leader = gang != nullptr ? gang->leader() : nullptr;
        const Brain* chosen = gang != nullptr ? gang->chosenTarget() : nullptr;
        const auto idOfBrain = [this](const Brain* named) { return named != nullptr ? idOf(&named->human()) : -1; };
        const long nowMs = static_cast<long>(brain.nowMs());
        // The fight goal on top (fight_updates, fight_active; -1 when the top goal is another), and the attack phase.
        const auto* fight = dynamic_cast<const FightGoal*>(top);
        const std::uint32_t phase = human.heldFlags();
        const FightBook& book = brain.fightBook();
        Row row;
        row.add(step).add(idOf(&human)).add(brain.type() == BrainType::Player ? 0 : -1);
        row.add(static_cast<int>(brain.type())).add(gang != nullptr ? gang->id() : -1);
        row.add(feet.x, 4).add(feet.y, 4).add(feet.z, 4).add(human.heading() * kDegrees, 3).add(human.speed(), 4);
        row.add(human.velocity().z, 4).add(static_cast<int>(human.gait())).add(human.animator().animId()).add(frame);
        row.add(human.record().command).add(fighter.health().value()).add(fighter.health().maximum());
        row.add(fighter.combat().power().value()).add(human.stamina().value());
        row.add(stateWord(brain)).add(phase);
        row.add(state == human::TargetState::Grounded ? 1 : 0).add(fighter.blocking() ? 1 : 0);
        row.add(fighter.victim().stunned() ? 1 : 0);
        row.add(fighter.grabbed() || held ? 1 : 0).add(fighter.held() != nullptr ? 1 : 0);
        row.add((phase & kAttackPhases) != 0 ? 1 : 0);
        row.add(locked != nullptr ? idOf(locked) : -1).add(idOfBrain(target));
        row.add(brain.goalCount()).add(top != nullptr ? static_cast<int>(top->type()) : -1);
        for (std::size_t i = 0; i < kStackColumns; ++i) {
            const Goal* goal = brain.goalAt(i);
            row.add(goal != nullptr ? static_cast<int>(goal->type()) : -1);
        }
        row.add(reaction != nullptr ? static_cast<int>(reaction->type()) : -1).add(brain.actionCount());
        row.add(static_cast<long>(brain.nextAttackMs()) - nowMs).add(static_cast<long>(brain.attackableAtMs()) - nowMs);
        row.add(brain.attackWarnings()).add(brain.enemies().size()).add(brain.attackSlots().size());
        row.add(brain.attackSlotCount()).add(book.places.held()).add(book.spacing.standing).add(book.spacing.down);
        row.add(brain.threatResponse());
        row.add(fight != nullptr ? static_cast<long>(fight->updates()) : -1L);
        row.add(fight != nullptr ? (fight->holdsPlace() ? 1 : 0) : -1);
        // The gang: the alert state is not modelled (-1, docs/research/gang-fights.md#coney item 9), the tactic as the
        // original's vtable, and no team-up order ever runs (-1: TacticAttack's team-ups are not built).
        row.add(-1).add(tacticValue(gang != nullptr ? gang->tactic() : nullptr));
        row.add(idOfBrain(leader)).add(idOfBrain(chosen));
        row.add(gang != nullptr && gang->orders().attackStrategies ? 1 : 0).add(-1).add(brain.dead() ? 1 : 0);
        row.add(target != nullptr ? brain.distanceTo(*target) : -1.0F, 3);
        row.add(actionName(front)).add(attack != nullptr ? attack->kind() : -1).add(brain.hasAttackSlot() ? 1 : 0);
        row.add(brain.helpCalls()).add(brain.helpersSent()).add(targetCauses(brain));
        text += row.finish();
    }
    return text;
}

} // namespace coney::ai
