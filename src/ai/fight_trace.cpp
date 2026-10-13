// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/fight_trace.h"

#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <utility>

#include "ai/attack_action.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/give_way.h"
#include "ai/move_action.h"
#include "ai/move_to_human_action.h"
#include "ai/play_anim_action.h"
#include "ai/set_command_action.h"
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
           "stamina,down,blocking,grabbed,grabbing,lock,target,goals,goal,goal0,goal1,goal2,goal3,goal4,goal5,react,"
           "actions,next_attack,attackable,warned,enemies,slots,slots_max,threat,g_tactic,g_leader,g_chosen,"
           "target_dist,action_kind,attack_kind,slot_held\n";
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
        Row row;
        row.add(step).add(idOf(&human)).add(brain.type() == BrainType::Player ? 0 : -1);
        row.add(static_cast<int>(brain.type())).add(gang != nullptr ? gang->id() : -1);
        row.add(feet.x, 4).add(feet.y, 4).add(feet.z, 4).add(human.heading() * kDegrees, 3).add(human.speed(), 4);
        row.add(human.velocity().z, 4).add(static_cast<int>(human.gait())).add(human.animator().animId()).add(frame);
        row.add(human.record().command).add(fighter.health().value()).add(fighter.health().maximum());
        row.add(fighter.combat().power().value()).add(human.stamina().value());
        row.add(state == human::TargetState::Grounded ? 1 : 0).add(fighter.blocking() ? 1 : 0);
        row.add(fighter.grabbed() || held ? 1 : 0).add(fighter.held() != nullptr ? 1 : 0);
        row.add(locked != nullptr ? idOf(locked) : -1).add(idOfBrain(target));
        row.add(brain.goalCount()).add(top != nullptr ? static_cast<int>(top->type()) : -1);
        for (std::size_t i = 0; i < kStackColumns; ++i) {
            const Goal* goal = brain.goalAt(i);
            row.add(goal != nullptr ? static_cast<int>(goal->type()) : -1);
        }
        row.add(reaction != nullptr ? static_cast<int>(reaction->type()) : -1).add(brain.actionCount());
        row.add(static_cast<long>(brain.nextAttackMs()) - nowMs).add(static_cast<long>(brain.attackableAtMs()) - nowMs);
        row.add(brain.attackWarnings()).add(brain.enemies().size()).add(brain.attackSlots().size());
        row.add(brain.attackSlotCount())
            .add(brain.threatResponse())
            .add(gang != nullptr && gang->tactic() != nullptr ? 1 : 0);
        row.add(idOfBrain(leader)).add(idOfBrain(chosen));
        row.add(target != nullptr ? brain.distanceTo(*target) : -1.0F, 3);
        row.add(actionName(front)).add(attack != nullptr ? attack->kind() : -1).add(brain.hasAttackSlot() ? 1 : 0);
        text += row.finish();
    }
    return text;
}

} // namespace coney::ai
