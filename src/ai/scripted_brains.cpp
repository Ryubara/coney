// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_brains.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "ai/address_person_goal.h"
#include "ai/dealer_goal.h"
#include "ai/engage_goals.h"
#include "ai/pedestrian_goal.h"
#include "ai/play_dyn_animation_goal.h"
#include "ai/scripted_story.h"
#include "ai/tactic_attack.h"
#include "ai/tactic_confront.h"
#include "ai/tactic_crowd.h"
#include "ai/tactic_domination.h"
#include "ai/track_human_goal.h"
#include "ai/turn_action.h"
#include "combat/meters.h"
#include "scripting/anim_callbacks.h"
#include "scripting/lua_value.h"
#include "scripting/message_handlers.h"
#include "scripting/script_system.h"

namespace coney::ai {

namespace {

// `NilHandle`'s value (script_bindings.cpp sets the global).
constexpr double kNilHandle = 0.0;
// The message a flag sends when a human arrives at it.
constexpr int kFlagArrival = 8;

} // namespace

ScriptedBrains::ScriptedBrains(Brains& brains, const world_objects::WorldFlags& flags,
                               world_objects::ObjectLocator locate)
    : m_owner(&brains), m_flags(&flags), m_locate(std::move(locate)), m_humans(std::make_unique<ScriptedHumans>(*this)),
      m_story(std::make_unique<ScriptedStory>(*this)) {
    m_owner->gangs().setScripts(this);
}

ScriptedBrains::~ScriptedBrains() {
    if (m_owner->gangs().scripts() == this) {
        m_owner->gangs().setScripts(nullptr);
    }
    setAnimCallbacks(nullptr);
}

script::StoryBindingHost* ScriptedBrains::story() { return m_story.get(); }

void ScriptedBrains::setAnimCallbacks(script::AnimCallbacks* callbacks) {
    if (m_animCallbacks != nullptr) {
        m_animCallbacks->setResolves({});
    }
    m_animCallbacks = callbacks;
    if (m_animCallbacks != nullptr) {
        m_animCallbacks->setResolves([this](double handle) { return brain(handle) != nullptr; });
    }
}

void ScriptedBrains::runAnimCallbacks() {
    std::vector<std::pair<double, std::uint32_t>> starts = std::exchange(m_animStarts, {});
    if (m_animCallbacks == nullptr || m_scripts == nullptr) {
        return;
    }
    for (const auto& [human, anim] : starts) {
        const std::string_view function = m_animCallbacks->match(human, anim);
        if (!function.empty()) {
            const std::array<script::Value, 2> args{script::Value(human), script::Value(static_cast<double>(anim))};
            m_scripts->call(std::string(function), args);
        }
    }
}

void ScriptedBrains::bind(double handle, Brain& brain, int gang) {
    m_brains[handle] = &brain;
    brain.setHandle(handle);
    brain.setServices(this);
    // Its anim starts, for the scripts' animation callbacks (the queue is bounded by what one step can start).
    brain.human().setAnimStartHook([this, handle](std::uint32_t anim) { m_animStarts.emplace_back(handle, anim); });
    if (gang >= 0) {
        m_owner->gangs().addMember(gang, brain);
    }
}

Brain* ScriptedBrains::brain(double handle) const {
    const auto found = m_brains.find(handle);
    return found == m_brains.end() ? nullptr : found->second;
}

std::optional<anim::Vec3> ScriptedBrains::locate(double handle) const {
    if (const Brain* found = brain(handle); found != nullptr) {
        return found->human().position();
    }
    std::optional<world_objects::Placement> placement = flag(handle);
    if (!placement && m_locate) {
        placement = m_locate(handle);
    }
    if (!placement) {
        return std::nullopt;
    }
    return anim::Vec3{placement->position[0], placement->position[1], placement->position[2]};
}

void ScriptedBrains::goalMoveToFlag(const script::MoveToFlagCall& call) {
    if (held([this, call] { goalMoveToFlag(call); })) {
        return;
    }
    if (Brain* found = named(call.human); found != nullptr) {
        ai::goalMoveToFlag(*found,
                           MoveToFlagOrder{.flag = call.flag,
                                           .gait = call.gait,
                                           .angleDegrees = call.angle,
                                           .distance = call.distance,
                                           .radius = call.radius,
                                           .intervalMs = call.intervalMs,
                                           .faceFlag = call.faceFlag,
                                           .option = call.option},
                           *this);
    }
}

void ScriptedBrains::actLookAt(const script::LookAtCall& call) {
    if (held([this, call] { actLookAt(call); })) {
        return;
    }
    if (Brain* found = named(call.human); found != nullptr) {
        found->queueAction(
            TurnAction::lookAt([this](double target) { return locate(target); }, call.target, call.turn, call.delayMs));
    }
}

void ScriptedBrains::goalFight(double human, double target) {
    if (held([this, human, target] { goalFight(human, target); })) {
        return;
    }
    Brain* fighter = named(human);
    Brain* opponent = named(target);
    if (fighter != nullptr && opponent != nullptr && fighter != opponent) {
        fighter->startFight(*opponent);
    }
}

void ScriptedBrains::brFlush(double human) {
    if (held([this, human] { brFlush(human); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        found->flush();
    }
}

void ScriptedBrains::brDead(double human, bool dead) {
    if (held([this, human, dead] { brDead(human, dead); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        found->setDead(dead);
    }
}

void ScriptedBrains::brSuspend(double human, bool suspended) {
    if (held([this, human, suspended] { brSuspend(human, suspended); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        found->clearActions();
        found->setSuspended(suspended);
    }
}

void ScriptedBrains::brSetThreatResponse(double human, int response) {
    if (held([this, human, response] { brSetThreatResponse(human, response); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        found->setThreatResponse(response);
    }
}

void ScriptedBrains::goalPlayDynAnimation(const script::DynAnimationCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    if (held([this, call] { goalPlayDynAnimation(call); })) {
        return;
    }
    if (Brain* found = named(call.human); found != nullptr) {
        ai::goalPlayDynAnimation(*found, *this, call.anim, call.callback, call.option);
    }
}

void ScriptedBrains::goalAddressPerson(const script::AddressPersonCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    if (held([this, call] { goalAddressPerson(call); })) {
        return;
    }
    if (Brain* speaker = named(call.human); speaker != nullptr) {
        ai::goalAddressPerson(*speaker, *this,
                              AddressOrder{.target = call.target,
                                           .approach = call.approach,
                                           .range = call.range,
                                           .scene = call.speech,
                                           .callback = call.callback});
    }
}

void ScriptedBrains::goalTrackHuman(double human, double target, float distance) {
    if (held([this, human, target, distance] { goalTrackHuman(human, target, distance); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        ai::goalTrackHuman(*found, *this, m_owner->formations(), target, distance);
    }
}

void ScriptedBrains::goalMoveToHuman(double human, double target, int gait, float radius) {
    if (held([this, human, target, gait, radius] { goalMoveToHuman(human, target, gait, radius); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        found->pushGoal(std::make_unique<MoveToHumanGoal>(*this, target, gait, radius));
    }
}

void ScriptedBrains::goalEngageEnemy(double human, double enemy) {
    if (held([this, human, enemy] { goalEngageEnemy(human, enemy); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        found->pushGoal(std::make_unique<EngageEnemyGoal>(*this, enemy));
    }
}

void ScriptedBrains::brSetType(double human, int type) {
    if (held([this, human, type] { brSetType(human, type); })) {
        return;
    }
    constexpr int kFirstAiType = static_cast<int>(BrainType::Cop);
    constexpr int kLastType = static_cast<int>(BrainType::CivilianDi);
    if (Brain* found = named(human); found != nullptr && type >= kFirstAiType && type <= kLastType) {
        found->setType(static_cast<BrainType>(type));
    }
}

void ScriptedBrains::brSetAttackWeight(double human, int attack, int weight) {
    if (held([this, human, attack, weight] { brSetAttackWeight(human, attack, weight); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        found->setAttackWeight(attack, static_cast<std::uint8_t>(weight));
    }
}

void ScriptedBrains::goalDealer(const script::DealerCall& call) {
    if (held([this, call] { goalDealer(call); })) {
        return;
    }
    if (Brain* found = named(call.human); found != nullptr) {
        found->pushGoal(std::make_unique<DealerGoal>(*this, dealerTypeFor(found->characterClass(), call.type),
                                                     call.range, call.runChance, call.dirtyChance, call.option));
    }
}

void ScriptedBrains::flagNetTraverse(const script::FlagNetTraverseCall& call) {
    if (held([this, call] { flagNetTraverse(call); })) {
        return;
    }
    // The player's own character is not sent wandering; without a network there is nowhere to go.
    Brain* found = named(call.human);
    if (found == nullptr || found->type() == BrainType::Player || m_flagNet == nullptr) {
        return;
    }
    found->pushGoal(std::make_unique<PedestrianGoal>(
        PedestrianOrder{.mode = call.mode, .chance = call.chance, .flagA = call.flagA, .flagB = call.flagB}, *m_flagNet,
        *this));
}

void ScriptedBrains::brSetNumFollowSlots(double leader, int count, int allowed) {
    if (held([this, leader, count, allowed] { brSetNumFollowSlots(leader, count, allowed); })) {
        return;
    }
    if (Brain* found = named(leader); found != nullptr) {
        if (Formation* formation = m_owner->formations().of(*found, true); formation != nullptr) {
            formation->setSlotCount(count, allowed, m_owner->nowMs());
        }
    }
}

void ScriptedBrains::brSetFollowSlot(double leader, int slot, float x, float y, int set) {
    if (held([this, leader, slot, x, y, set] { brSetFollowSlot(leader, slot, x, y, set); })) {
        return;
    }
    if (Brain* found = named(leader); found != nullptr) {
        if (Formation* formation = m_owner->formations().of(*found, true); formation != nullptr) {
            formation->setSlot(slot, x, y, set, m_owner->nowMs());
        }
    }
}

void ScriptedBrains::brSetFollowSlotSet(double leader, int set) {
    if (held([this, leader, set] { brSetFollowSlotSet(leader, set); })) {
        return;
    }
    if (Brain* found = named(leader); found != nullptr) {
        if (Formation* formation = m_owner->formations().of(*found, true); formation != nullptr) {
            formation->setSlotSet(set, m_owner->nowMs());
        }
    }
}

void ScriptedBrains::tacticCrowd(int gang, std::string_view callback, bool cheering) {
    if (held([this, gang, name = std::string(callback), cheering] { tacticCrowd(gang, name, cheering); })) {
        return;
    }
    m_owner->gangs().setTactic(gang, std::make_unique<TacticCrowd>(std::string(callback), cheering, m_owner->nowMs()));
}

void ScriptedBrains::tacticTrigger(int gang, int what, bool on) {
    if (held([this, gang, what, on] { tacticTrigger(gang, what, on); })) {
        return;
    }
    Gang* found = m_owner->gangs().find(gang);
    if (found == nullptr) {
        return;
    }
    if (auto* crowd = dynamic_cast<TacticCrowd*>(found->tactic()); crowd != nullptr) {
        crowd->trigger(*found, what, on);
    }
}

void ScriptedBrains::tacticClear(int gang) {
    if (held([this, gang] { tacticClear(gang); })) {
        return;
    }
    m_owner->gangs().setTactic(gang, nullptr);
}

void ScriptedBrains::tacticAttack(int gang, std::string_view callback) {
    if (held([this, gang, name = std::string(callback)] { tacticAttack(gang, name); })) {
        return;
    }
    m_owner->gangs().setTactic(gang, std::make_unique<TacticAttack>(std::string(callback)));
}

void ScriptedBrains::brFlushActions(double human) {
    if (held([this, human] { brFlushActions(human); })) {
        return;
    }
    if (Brain* brain = named(human); brain != nullptr) {
        brain->clearActions();
    }
}

void ScriptedBrains::brFlushGoals(double human) {
    if (held([this, human] { brFlushGoals(human); })) {
        return;
    }
    if (Brain* brain = named(human); brain != nullptr) {
        brain->clearGoals();
    }
}

void ScriptedBrains::setMaxHealth(double human, int health) {
    if (held([this, human, health] { setMaxHealth(human, health); })) {
        return;
    }
    if (Brain* brain = named(human); brain != nullptr) {
        brain->human().fighter().health() = combat::Health(std::max(health, 1));
    }
}

void ScriptedBrains::humanDelete(double human) {
    if (held([this, human] { humanDelete(human); })) {
        m_heldHumans.erase(human);
        return;
    }
    Brain* brain = named(human);
    if (brain == nullptr || brain == m_player || brain->type() == BrainType::Player) {
        return;
    }
    unbind(human);
    if (m_remover) {
        m_remover(*brain);
    }
}

Brain* ScriptedBrains::switchTarget(const Brain& from, bool anyKindZero) const {
    // Not driven by a pad, and on its feet with health left.
    const auto canTake = [&from](const Brain* member) {
        return member != &from && member->type() != BrainType::Player &&
               !member->human().fighter().health().depleted() &&
               member->human().state() != human::TargetState::Grounded;
    };
    if (const Gang* gang = from.gang(); gang != nullptr) {
        for (Brain* member : gang->members()) {
            if (canTake(member)) {
                return member;
            }
        }
    }
    if (!anyKindZero) {
        return nullptr;
    }
    for (std::size_t id = 0; id < kGangSlots; ++id) {
        const Gang* other = m_owner->gangs().find(static_cast<int>(id));
        if (other == nullptr || other->kind() != 0) {
            continue;
        }
        for (Brain* member : other->members()) {
            if (canTake(member)) {
                return member;
            }
        }
    }
    return nullptr;
}

double ScriptedBrains::switchPlayer(double human, bool storyMode) {
    Brain* from = named(human);
    if (from == nullptr || from->type() != BrainType::Player || !m_switcher) {
        return 0.0;
    }
    Brain* to = switchTarget(*from, m_switchToKindZero && storyMode);
    if (to == nullptr) {
        return 0.0;
    }
    m_switcher(*from, *to);
    if (m_player == from) {
        m_player = to;
    }
    return to->handle();
}

std::optional<int> ScriptedBrains::gangOf(double human) const {
    const Brain* brain = named(human);
    if (brain == nullptr) {
        // A human created while holding answers with the gang it will join.
        const std::optional<HeldHuman> held = heldHuman(human);
        return held && held->gang >= 0 ? std::optional<int>(held->gang) : std::nullopt;
    }
    if (brain->gang() == nullptr) {
        return std::nullopt;
    }
    return brain->gang()->id();
}

void ScriptedBrains::tacticConfront(const script::ConfrontCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    if (held([this, call] { tacticConfront(call); })) {
        return;
    }
    const ConfrontSettings settings{.targetGang = call.targetGang,
                                    .approachRange = call.approachRange,
                                    .criticalRange = call.criticalRange,
                                    .confrontation = call.confrontation};
    m_owner->gangs().setTactic(call.gang, std::make_unique<TacticConfront>(settings, call.callback));
}

void ScriptedBrains::tacticDomination(int gang, double flag, float range, std::string_view callback) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    if (held([this, gang, flag, range, name = std::string(callback)] { tacticDomination(gang, flag, range, name); })) {
        return;
    }
    m_owner->gangs().setTactic(gang, std::make_unique<TacticDomination>(flag, range, *this, std::string(callback)));
}

int ScriptedBrains::gangCreate(int kind, std::string_view name) { return m_owner->gangs().create(kind, name); }

void ScriptedBrains::gangDelete(int gang) {
    if (held([this, gang] { gangDelete(gang); })) {
        return;
    }
    m_owner->gangs().remove(gang);
}

void ScriptedBrains::gangAddMember(int gang, double human) {
    if (held([this, gang, human] { gangAddMember(gang, human); })) {
        return;
    }
    if (Brain* found = named(human); found != nullptr) {
        m_owner->gangs().addMember(gang, *found);
    }
}

void ScriptedBrains::gangBrDead(int gang, bool dead) {
    if (held([this, gang, dead] { gangBrDead(gang, dead); })) {
        return;
    }
    m_owner->gangs().setDead(gang, dead);
}

void ScriptedBrains::gangBrFlush(int gang) {
    if (held([this, gang] { gangBrFlush(gang); })) {
        return;
    }
    m_owner->gangs().flush(gang);
}

void ScriptedBrains::gangSetThreatResponse(int gang, int response) {
    if (held([this, gang, response] { gangSetThreatResponse(gang, response); })) {
        return;
    }
    m_owner->gangs().setThreatResponse(gang, response);
}

void ScriptedBrains::gangMakeEnemies(int a, int b) {
    if (held([this, a, b] { gangMakeEnemies(a, b); })) {
        return;
    }
    m_owner->gangs().makeEnemies(a, b);
}

void ScriptedBrains::gangMakeFriends(int a, int b) {
    if (held([this, a, b] { gangMakeFriends(a, b); })) {
        return;
    }
    m_owner->gangs().makeFriends(a, b);
}

void ScriptedBrains::gangSetMsgHandler(int gang, int message, std::string_view handler) {
    if (held([this, gang, message, name = std::string(handler)] { gangSetMsgHandler(gang, message, name); })) {
        return;
    }
    m_owner->gangs().setMessageHandler(gang, message, std::string(handler));
}

void ScriptedBrains::gangSuspend(int gang, bool suspended) {
    if (held([this, gang, suspended] { gangSuspend(gang, suspended); })) {
        return;
    }
    m_owner->gangs().suspend(gang, suspended);
}

int ScriptedBrains::gangHeadCount(int gang, bool living) {
    const Gang* found = m_owner->gangs().find(gang);
    if (found == nullptr) {
        return 0;
    }
    const int held = heldMembers(gang);
    if (!living) {
        return static_cast<int>(found->members().size()) + held;
    }
    return held + static_cast<int>(std::ranges::count_if(found->members(), [](const Brain* member) {
               return !member->human().fighter().health().depleted();
           }));
}

int ScriptedBrains::gangStandingCount(int gang) {
    const Gang* found = m_owner->gangs().find(gang);
    return found != nullptr ? found->standing() + heldMembers(gang) : 0;
}

std::optional<world_objects::Placement> ScriptedBrains::flag(double handle) const {
    const world_objects::WorldFlag* found = m_flags->find(handle);
    if (found == nullptr) {
        return std::nullopt;
    }
    return world_objects::Placement{.position = world_objects::WorldFlags::position(*found, m_locate),
                                    .headingDegrees = world_objects::WorldFlags::headingDegrees(*found, m_locate)};
}

void ScriptedBrains::arrived(double handle, Brain& user) {
    ++m_arrivals;
    if (m_messages != nullptr && m_scripts != nullptr) {
        m_messages->deliver(*m_scripts, handle, kFlagArrival, user.handle(), kNilHandle, 0.0);
    }
}

bool ScriptedBrains::humanEvent(Brain& human, const BrainEvent& event) {
    if (m_messages == nullptr || m_scripts == nullptr || human.handle() == kNilHandle) {
        return false;
    }
    const double other = event.other != nullptr ? event.other->handle() : kNilHandle;
    return m_messages->deliver(*m_scripts, human.handle(), event.id, human.handle(), other,
                               static_cast<double>(event.value));
}

std::vector<world_objects::BoxSubject> ScriptedBrains::boxSubjects() const {
    std::vector<world_objects::BoxSubject> subjects;
    subjects.reserve(m_brains.size());
    for (const auto& [handle, brain] : m_brains) {
        const anim::Vec3 feet = brain->human().position();
        subjects.push_back({.handle = handle,
                            .position = {feet.x, feet.y, feet.z},
                            .alive = !brain->human().fighter().health().depleted()});
    }
    return subjects;
}

void ScriptedBrains::schedule(std::string_view function, std::span<const double> args, std::uint32_t delayMs) {
    if (m_scripts != nullptr) {
        m_scripts->schedule(std::string(function), delayMs, args);
    }
}

bool ScriptedBrains::call(std::string_view function, std::span<const double> args) {
    if (m_scripts == nullptr) {
        return false;
    }
    std::vector<script::Value> values;
    values.reserve(args.size());
    for (const double arg : args) {
        values.emplace_back(arg);
    }
    return m_scripts->call(function, values);
}

std::optional<ScriptedBrains::HeldHuman> ScriptedBrains::heldHuman(double handle) const {
    const auto found = m_heldHumans.find(handle);
    return found == m_heldHumans.end() ? std::nullopt : std::optional<HeldHuman>(found->second);
}

int ScriptedBrains::heldMembers(int gang) const {
    return static_cast<int>(
        std::ranges::count_if(m_heldHumans, [gang](const auto& entry) { return entry.second.gang == gang; }));
}

bool ScriptedBrains::held(std::function<void()> call) {
    if (!m_holding) {
        return false;
    }
    m_held.push_back(std::move(call));
    return true;
}

void ScriptedBrains::release(Spawner spawner) {
    m_spawner = std::move(spawner);
    // The calls held run in their order, as they would have when the script made them; one that is held again cannot
    // happen, as the hold ends first.
    m_heldHumans.clear();
    m_holding = false;
    std::vector<std::function<void()>> calls = std::exchange(m_held, {});
    for (const std::function<void()>& call : calls) {
        call();
    }
}

void ScriptedBrains::humanCreated(const HumanCreation& human) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    if (held([this, human] { humanCreated(human); })) {
        // Counted in its gang until the hold ends, so the start callback's head counts see it.
        m_heldHumans[human.handle] = HeldHuman{.gang = human.gang, .playerIndex = human.playerIndex};
        return;
    }
    if (!m_spawner) {
        return;
    }
    if (Brain* made = m_spawner(human); made != nullptr) {
        made->setCharacterClass(human.type);
        bind(human.handle, *made, human.gang);
    }
}

void ScriptedBrains::humanTeleported(double handle, const world_objects::Placement& placement) {
    if (held([this, handle, placement] { humanTeleported(handle, placement); })) {
        return;
    }
    Brain* found = named(handle);
    if (found == nullptr || found->type() == BrainType::Player) {
        return;
    }
    const std::array<float, 3>& p = placement.position;
    found->human().spawn(nullptr, anim::Vec3{p[0], p[1], p[2]}, placement.headingDegrees);
}

std::optional<world_objects::Placement> ScriptedBrains::humanPlacement(double handle) const {
    const Brain* found = brain(handle);
    if (found == nullptr) {
        return std::nullopt;
    }
    const anim::Vec3 p = found->human().position();
    return world_objects::Placement{.position = {p.x, p.y, p.z},
                                    .headingDegrees = found->human().heading() * 180.0F / std::numbers::pi_v<float>};
}

void ScriptedBrains::playScene(int /*scene*/, Brain& human, std::string_view callback) {
    if (!callback.empty()) {
        const std::array<double, 2> args{human.handle(), 1.0};
        schedule(callback, args, kGoalCallbackDelayMs);
    }
}

} // namespace coney::ai
