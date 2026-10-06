// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_humans.h"

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_goals.h"
#include "human/body.h"
#include "human/human.h"
#include "human/human_flags.h"

namespace coney::ai {

namespace {

// Coney's NilHandle.
constexpr double kNilHandle = 0.0;
// `HuSetCarriedItem` sets the drop chance to this (`+0x278`).
constexpr int kCarriedChance = 100;
// A gang keeps at most this many spawners (`+0x640`).
// The gangs a spawner makes to respond to a crime are named this and a number.
constexpr std::string_view kResponderName = "Responder";

// The body radius of `human` (its walking sphere: a player's is larger).
float bodyRadius(const human::Human& human) {
    return human.fighterProfile().player ? human::playerWalkingRadius(human.scale())
                                         : human::walkingRadius(human.scale());
}

} // namespace

void ScriptedHumans::onBrain(double handle, const std::function<void(Brain&)>& body) {
    if (m_scripted->defer([this, handle, body] { onBrain(handle, body); })) {
        return;
    }
    if (Brain* brain = m_scripted->brain(handle); brain != nullptr) {
        body(*brain);
    }
}

std::optional<script::HumanStatus> ScriptedHumans::status(double handle) const {
    const Brain* brain = m_scripted->brain(handle);
    if (brain == nullptr) {
        // A human the scripts made before the level's humans exist answers as standing (ScriptedBrains::heldHuman()).
        if (const std::optional<ScriptedBrains::HeldHuman> held = m_scripted->heldHuman(handle)) {
            return script::HumanStatus{.alive = true,
                                       .player = held->playerIndex > 0,
                                       .arrested = false,
                                       .healthPercent = 100.0F,
                                       .gangType = 0xffff,
                                       .heldObject = 0.0,
                                       .soundCommands = true};
        }
        return std::nullopt;
    }
    const human::Human& human = brain->human();
    return script::HumanStatus{.alive = human.alive(),
                               .player = brain->type() == BrainType::Player,
                               .arrested = human.script().arrested,
                               .healthPercent = human.healthPercent(),
                               .gangType = brain->gang() != nullptr ? brain->gang()->kind() : 0xffff,
                               .heldObject = human.script().heldObject,
                               .soundCommands = human.script().soundCommands};
}

void ScriptedHumans::setFlags(double human, std::uint64_t bits, bool on) {
    onBrain(human, [bits, on](Brain& brain) { brain.human().setFlag(bits, on); });
}

void ScriptedHumans::setLockedRage(double human, bool on) {
    onBrain(human, [on](Brain& brain) { brain.human().setRageLocked(on); });
}

void ScriptedHumans::fillRage(double human, int holdMs) {
    onBrain(human, [holdMs](Brain& brain) { brain.human().fillRage(holdMs); });
}

void ScriptedHumans::setRageFraction(double human, float fraction) {
    onBrain(human, [fraction](Brain& brain) { brain.human().setRageFraction(fraction); });
}

void ScriptedHumans::setHealthPercent(double human, float percent) {
    onBrain(human, [percent](Brain& brain) { brain.human().setHealthPercent(percent); });
}

void ScriptedHumans::revive(double human) {
    onBrain(human, [](Brain& brain) { brain.human().revive(); });
}

void ScriptedHumans::setNormalMode(double human, bool full) {
    onBrain(human, [full](Brain& brain) { brain.human().setNormalMode(full); });
}

void ScriptedHumans::setArrested(double human, bool arrested) {
    onBrain(human, [arrested](Brain& brain) { brain.human().setArrested(arrested); });
}

void ScriptedHumans::setPushable(double human, bool pushable) {
    onBrain(human, [pushable](Brain& brain) { brain.human().script().pushable = pushable; });
}

void ScriptedHumans::setMoney(double human, int dollars) {
    onBrain(human, [dollars](Brain& brain) { brain.human().script().money = dollars; });
}

void ScriptedHumans::setCarriedItem(double human, std::string_view object) {
    onBrain(human, [item = std::string(object)](Brain& brain) {
        brain.human().script().carriedItem = item;
        brain.human().script().carriedChance = kCarriedChance;
    });
}

void ScriptedHumans::setMugCallback(double human, std::string_view callback) {
    onBrain(human, [name = std::string(callback)](Brain& brain) { brain.human().script().mugCallback = name; });
}

void ScriptedHumans::setConscious(double human, bool conscious) {
    onBrain(human, [conscious](Brain& brain) {
        bool& out = brain.human().script().knockedOut;
        if (out == !conscious) {
            return;
        }
        out = !conscious;
        brain.setDead(!conscious);
    });
}

void ScriptedHumans::setSoundCommands(double human, bool on) {
    onBrain(human, [on](Brain& brain) { brain.human().script().soundCommands = on; });
}

void ScriptedHumans::setPocket(double human, int item, int count) {
    onBrain(human, [item, count](Brain& brain) {
        brain.human().script().pocketItem = item;
        brain.human().script().pocketCount = count;
    });
}

void ScriptedHumans::setLookTarget(const script::LookTargetCall& call) {
    onBrain(call.human, [this, call](Brain& brain) {
        if (!m_scripted->locate(call.target)) {
            return;
        }
        human::ScriptState& script = brain.human().script();
        script.looking = true;
        script.look = human::LookOrder{
            .target = call.target, .timeMs = call.timeMs, .weight = call.weight, .options = call.options};
    });
}

void ScriptedHumans::teleportNear(double human, double near) {
    onBrain(human, [this, near](Brain& brain) {
        const Brain* other = m_scripted->brain(near);
        if (other == nullptr || other == &brain || brain.type() == BrainType::Player) {
            return;
        }
        // Beside the other human, on its right, the two bodies just apart.
        const human::Human& beside = other->human();
        const float gap = bodyRadius(brain.human()) + bodyRadius(beside);
        const float right = beside.heading() - (std::numbers::pi_v<float> / 2.0F);
        const anim::Vec3 at{beside.position().x - (std::sin(right) * gap),
                            beside.position().y + (std::cos(right) * gap), beside.position().z};
        brain.human().spawn(nullptr, at, brain.human().heading() * 180.0F / std::numbers::pi_v<float>);
    });
}

void ScriptedHumans::lockPad(double human, bool locked) {
    onBrain(human, [locked](Brain& brain) { brain.human().script().padLocked = locked; });
}

void ScriptedHumans::enableCommand(double human, int command, bool enabled) {
    onBrain(human, [command, enabled](Brain& brain) {
        // Only a pad-controlled human's pad has the mask (per-player +0x1b).
        if (brain.type() != BrainType::Player || brain.dead()) {
            return;
        }
        std::uint64_t& disabled = brain.human().script().disabledCommands;
        const std::uint64_t bits = command == 0 ? ~std::uint64_t{0} : std::uint64_t{1} << command;
        disabled = enabled ? (disabled & ~bits) : (disabled | bits);
    });
}

void ScriptedHumans::setIcon(double human, std::string_view object, int param) {
    onBrain(human, [icon = std::string(object), param](Brain& brain) {
        // The same icon again does nothing; another one replaces it.
        human::ScriptState& script = brain.human().script();
        if (script.icon != icon) {
            script.icon = icon;
            script.iconParam = param;
        }
    });
}

void ScriptedHumans::dropWeapon(double human) {
    onBrain(human, [](Brain& brain) {
        human::ScriptState& script = brain.human().script();
        script.heldObject = kNilHandle;
        script.heldObjectName.clear();
    });
}

void ScriptedHumans::releaseObject(double object) {
    if (m_scripted->defer([this, object] { releaseObject(object); })) {
        return;
    }
    // Whoever holds it lets go, as HuDropWeapon does (the class-specific release routines are not modelled).
    for (const auto& [handle, brain] : m_scripted->bound()) {
        human::ScriptState& script = brain->human().script();
        if (script.heldObject == object) {
            script.heldObject = kNilHandle;
            script.heldObjectName.clear();
        }
    }
}

double ScriptedHumans::placeItemInHand(double human, std::string_view object,
                                       const std::function<double()>& nextHandle) {
    // Put the object in a free hand; a human that holds something already takes nothing.
    const auto place = [item = std::string(object)](Brain& brain, double handle) {
        human::ScriptState& script = brain.human().script();
        if (script.heldObject != kNilHandle) {
            return false;
        }
        script.heldObject = handle;
        script.heldObjectName = item;
        return true;
    };
    if (m_scripted->holding()) {
        const double handle = nextHandle();
        // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
        onBrain(human, [place, handle](Brain& brain) { static_cast<void>(place(brain, handle)); });
        return handle;
    }
    Brain* brain = m_scripted->brain(human);
    if (brain == nullptr || brain->human().script().heldObject != kNilHandle) {
        return kNilHandle;
    }
    const double handle = nextHandle();
    return place(*brain, handle) ? handle : kNilHandle;
}

bool ScriptedHumans::useAnim(double human, int slot, std::string_view anim, bool loaded) {
    if (!loaded || slot < 0 || slot >= static_cast<int>(human::kUseAnimIds.size())) {
        return false;
    }
    const auto use = [slot, clip = std::string(anim)](Brain& brain) {
        human::ScriptState& script = brain.human().script();
        script.animOverrides.at(static_cast<std::size_t>(slot)) = clip;
        // The idle's replacement holds the human in place while it is set.
        if (slot == 0) {
            script.pushable = clip.empty();
        }
    };
    if (m_scripted->holding()) {
        onBrain(human, use);
        return true;
    }
    Brain* brain = m_scripted->brain(human);
    if (brain == nullptr) {
        return false;
    }
    use(*brain);
    return true;
}

void ScriptedHumans::changePlayerGang(int gang, bool stamp) {
    const Brain* player = m_scripted->player();
    if (player != nullptr && player->gang() != nullptr && player->gang()->id() == gang) {
        return;
    }
    m_playerGang = gang;
    if (stamp) {
        m_gangChangeMs = m_scripted->owner().nowMs();
    }
}

std::optional<int> ScriptedHumans::playerIndex(double handle) const {
    const Brain* brain = m_scripted->brain(handle);
    if (brain == nullptr || brain->type() != BrainType::Player) {
        return std::nullopt;
    }
    return 0;
}

void ScriptedHumans::clearBackoff(double human) {
    onBrain(human, [](Brain& brain) {
        if (const Goal* top = brain.topGoal(); top != nullptr && top->type() == GoalType::Backoff) {
            brain.popGoal();
        }
    });
}

void ScriptedHumans::setWantsWeapon(double human, bool wants) {
    onBrain(human, [wants](Brain& brain) { brain.setWantsWeapon(wants); });
}

void ScriptedHumans::setDamageResponse(double human, int response) {
    onBrain(human, [response](Brain& brain) { brain.senses().damageResponse = response; });
}

void ScriptedHumans::goalBackoff(const script::BackoffCall& call) {
    onBrain(call.human, [this, call](Brain& brain) {
        static_cast<void>(brain.pushGoal(
            std::make_unique<BackoffGoal>(*m_scripted, call.from, call.distance, call.timeMs, call.option)));
    });
}

void ScriptedHumans::goalBumLogic(const script::BumLogicCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onBrain(call.human, [call](Brain& brain) {
        static_cast<void>(brain.pushGoal(std::make_unique<BumLogicGoal>(BumOrder{.type = call.type,
                                                                                 .option = call.option,
                                                                                 .chance = call.chance,
                                                                                 .value = call.value,
                                                                                 .callback = call.callback,
                                                                                 .option2 = call.option2})));
    });
}

void ScriptedHumans::goalMoveToUseFlag(const script::MoveToUseFlagCall& call) {
    onBrain(call.human, [this, call](Brain& brain) {
        // The flag is the human's from now until the goal ends.
        const double user = brain.handle();
        m_reservations[call.flag] = user;
        auto release = [this, flag = call.flag, user] {
            if (const auto found = m_reservations.find(flag); found != m_reservations.end() && found->second == user) {
                m_reservations.erase(found);
            }
        };
        MoveToFlagOrder move;
        move.flag = call.flag;
        move.gait = call.gait;
        move.radius = call.radius;
        move.faceFlag = true;
        static_cast<void>(brain.pushGoal(std::make_unique<MoveToUseFlagGoal>(
            move, *m_scripted, UseFlagOrder{.delay = call.delay, .duration = call.duration, .reserve = call.reserve},
            std::move(release))));
    });
}

void ScriptedHumans::addSpawner(const script::SpawnerCall& call) {
    if (m_scripted->owner().gangs().find(call.gang) == nullptr) {
        return;
    }
    m_spawners.add(call);
}

void ScriptedHumans::clearResponders() {
    Gangs& gangs = m_scripted->owner().gangs();
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        const Gang* gang = gangs.find(id);
        if (gang != nullptr && gang->name().starts_with(kResponderName) && gang->kind() != kPoliceKind &&
            gang->kind() != kPoliceLikeKind) {
            gangs.remove(id);
        }
    }
}

void ScriptedHumans::clearWanted(int /*gang*/) {}

void ScriptedHumans::setGangDamageResponse(int gang, int response) {
    // Only the members it has now: later ones keep their own (docs/references/bindings/gang.md).
    if (Gang* found = m_scripted->owner().gangs().find(gang); found != nullptr) {
        for (Brain* member : found->members()) {
            member->senses().damageResponse = response;
        }
    }
}

void ScriptedHumans::setGangIcon(int gang, std::string_view object, int param) {
    if (Gang* found = m_scripted->owner().gangs().find(gang); found != nullptr) {
        for (Brain* member : found->members()) {
            setIcon(member->handle(), object, param);
        }
    }
}

void ScriptedHumans::setInvincible(int gang, bool on) { m_scripted->owner().gangs().setInvincible(gang, on); }

void ScriptedHumans::setTargetable(int gang, bool on) { m_scripted->owner().gangs().setTargetable(gang, on); }

void ScriptedHumans::applyRules(const CharacterRules& rules) {
    m_rage = rules.rage;
    for (std::size_t set = 0; set < rules.followSlots.size(); ++set) {
        if (const auto& slots = rules.followSlots.at(set); slots) {
            m_scripted->owner().formations().setDefaults(static_cast<int>(set), *slots);
        }
    }
}

void ScriptedHumans::runRageHandlers() {
    for (const auto& [handle, brain] : m_scripted->bound()) {
        const combat::RageMeter& rage = brain->human().fighter().combat().rage();
        RageSeen& seen = m_rageSeen[handle];
        const std::array<double, 1> args{handle};
        if (rage.full() && !seen.full && !m_rage.onFull.empty()) {
            static_cast<void>(m_scripted->call(m_rage.onFull, args));
        }
        if (rage.raging() && !seen.raging && !m_rage.onEnter.empty()) {
            static_cast<void>(m_scripted->call(m_rage.onEnter, args));
        }
        if (!rage.raging() && seen.raging && !m_rage.onExit.empty()) {
            static_cast<void>(m_scripted->call(m_rage.onExit, args));
        }
        seen = RageSeen{.full = rage.full(), .raging = rage.raging()};
    }
}

double ScriptedHumans::reservation(double flag) const {
    const auto found = m_reservations.find(flag);
    return found == m_reservations.end() ? kNilHandle : found->second;
}

} // namespace coney::ai
