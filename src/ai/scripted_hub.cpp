// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_hub.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/script_services.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_humans.h"
#include "ai/spawners.h"
#include "characters/character_class.h"
#include "combat/commands.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/locomotion.h"

namespace coney::ai {

namespace {

// An AI human's handcuffs are kept between 0 and 9.
constexpr int kMaxCuffs = 9;
// The workout's end clip, and the pad's commands: cross pressed pumps (0x12), triangle pressed quits (10).
constexpr int kWorkoutEndAnim = 692;
constexpr combat::CommandId kPumpCommand = combat::command::kCrossPressed;
constexpr combat::CommandId kQuitCommand = combat::command::kTrianglePressed;
// A player's effort is 0-1, used × 3 as an AI's blend.
constexpr float kPlayerBlendScale = 3.0F;
// A gang's message handler slots (`+0xe4`, 26 of them).
constexpr int kGangMessages = 26;

// A vector as a hub point.
script::HubPoint pointOf(anim::Vec3 v) { return script::HubPoint{v.x, v.y, v.z}; }

// The phase of a player's effort (0, 1 or 2): which of the three loop clips it is on.
std::size_t phaseOf(float effort) {
    return static_cast<std::size_t>(std::clamp(static_cast<int>(effort * kPlayerBlendScale), 0, 2));
}

} // namespace

ScriptedHub::ScriptedHub(ScriptedBrains& scripted) : m_scripted(&scripted) { wireServices(); }

void ScriptedHub::wireServices() {
    m_services.locate = [this](double handle) { return m_scripted->locate(handle); };
    m_services.crimeScene = [this] {
        return m_lookups.crimeScene ? m_lookups.crimeScene() : std::optional<anim::Vec3>{};
    };
    m_services.brains = [this] {
        std::vector<Brain*> brains;
        for (const auto& [handle, brain] : m_scripted->bound()) {
            brains.push_back(brain);
        }
        return brains;
    };
    m_services.player = [this] { return m_scripted->player(); };
    m_services.inBox = [this](double box, anim::Vec3 point) { return m_lookups.inBox && m_lookups.inBox(box, point); };
    m_services.flagsInBox = [this](double box) {
        return m_lookups.flagsInBox ? m_lookups.flagsInBox(box) : std::vector<anim::Vec3>{};
    };
    m_services.crimeCount = [this] { return m_lookups.crimeCount ? m_lookups.crimeCount() : std::uint64_t{0}; };
    m_services.lastCrime = [this] { return m_lookups.lastCrime ? m_lookups.lastCrime() : std::optional<anim::Vec3>{}; };
    m_services.reportBreakIn = [this](anim::Vec3 at) {
        if (m_lookups.reportBreakIn) {
            m_lookups.reportBreakIn(at);
        }
    };
    m_services.scripts = m_scripted;
}

void ScriptedHub::onBrain(double handle, const std::function<void(Brain&)>& body) {
    if (m_scripted->defer([this, handle, body] { onBrain(handle, body); })) {
        return;
    }
    if (Brain* found = m_scripted->brain(handle); found != nullptr) {
        body(*found);
    }
}

void ScriptedHub::callBack(const std::string& name, double handle) {
    if (!name.empty()) {
        const std::array<double, 1> args{handle};
        static_cast<void>(m_scripted->call(name, args));
    }
}

std::optional<int> ScriptedHub::categoryOf(const Brain& brain) const {
    if (!m_lookups.category || brain.characterClass() < 0) {
        return std::nullopt;
    }
    return m_lookups.category(characters::characterClassOf(brain.characterClass()).id);
}

// ---- The humans ----

std::optional<script::HubHumanStatus> ScriptedHub::status(double handle) const {
    const Brain* brain = m_scripted->brain(handle);
    if (brain == nullptr) {
        return std::nullopt;
    }
    const human::Human& body = brain->human();
    const std::optional<int> index = m_scripted->humanHost().playerIndex(handle);
    return script::HubHumanStatus{.money = body.script().money,
                                  .position = pointOf(body.position()),
                                  .dead = body.health().depleted(),
                                  .player = index.has_value(),
                                  .playerIndex = index.value_or(-1)};
}

bool ScriptedHub::alive(double handle) const {
    return m_scripted->brain(handle) != nullptr || m_scripted->flag(handle).has_value();
}

void ScriptedHub::attachGear(double human, bool attach, bool knuckles, bool boots) {
    onBrain(human, [this, human, attach, knuckles, boots](Brain& brain) {
        if (!m_scripted->humanHost().playerIndex(human).has_value()) {
            return;
        }
        human::ScriptState& script = brain.human().script();
        script.knuckles = attach && knuckles;
        script.boots = attach && boots;
    });
}

void ScriptedHub::setScale(double human, float scale) {
    onBrain(human, [scale](Brain& brain) { brain.human().setScale(scale); });
}

void ScriptedHub::addCuffs(double human, int count) {
    onBrain(human, [count](Brain& brain) {
        int& cuffs = brain.human().script().cuffs;
        // The count is added in 8 bits, so a negative number takes cuffs away; the result is kept to 0-9.
        cuffs = std::clamp(cuffs + count, 0, kMaxCuffs);
    });
}

void ScriptedHub::setMuggable(double human, bool on) {
    onBrain(human, [on](Brain& brain) { brain.human().script().muggable = on; });
}

void ScriptedHub::setPedReaction(double human, int reaction) {
    onBrain(human, [reaction](Brain& brain) { brain.human().script().pedReaction = reaction; });
}

void ScriptedHub::setUnarrestable(double human, bool on) {
    onBrain(human, [on](Brain& brain) {
        brain.human().setFlag(human::flag::kUnarrestable, on);
        brain.human().script().uncuffOffered = on;
    });
}

void ScriptedHub::setCombatMode(double human, bool on) {
    onBrain(human, [on](Brain& brain) {
        human::Human& body = brain.human();
        // On is refused for a human being held (tackled, grabbed).
        if (on && body.fighter().holdState().has_value()) {
            return;
        }
        body.script().combatMode = on;
    });
}

void ScriptedHub::playDynAnim(double human, std::string_view anim) {
    onBrain(human, [this, name = std::string(anim)](Brain& brain) {
        if (brain.human().health().depleted()) {
            return;
        }
        m_scripted->loadDynamicClip(brain, name);
        static_cast<void>(m_scripted->playClip(brain, kDynamicAnimId));
    });
}

void ScriptedHub::workout(const script::WorkoutCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onBrain(call.human, [this, call](Brain& brain) {
        if (m_scripted->locate(call.equipment) == std::nullopt) {
            return;
        }
        WorkoutRun run;
        run.equipment = call.equipment;
        run.clips = call.clips;
        m_workouts[brain.handle()] = std::move(run);
    });
}

void ScriptedHub::endWorkout(Brain& brain) {
    brain.human().script().workingOut = false;
    m_workouts.erase(brain.handle());
    static_cast<void>(m_scripted->playClip(brain, kWorkoutEndAnim));
    if (m_lookups.workout != nullptr) {
        callBack(m_lookups.workout->onEnd, brain.handle());
    }
}

void ScriptedHub::stopWorkout(double human) {
    onBrain(human, [this](Brain& brain) {
        const auto found = m_workouts.find(brain.handle());
        if (found == m_workouts.end()) {
            return;
        }
        if (found->second.begun) {
            endWorkout(brain);
        } else {
            // A start that has not begun goes back to state 0, with no end callback.
            m_workouts.erase(found);
        }
    });
}

void ScriptedHub::setWorkoutBlend(double human, float blend) {
    onBrain(human, [this, blend](Brain& brain) {
        const auto found = m_workouts.find(brain.handle());
        if (found != m_workouts.end() && found->second.begun) {
            found->second.blend = blend;
        }
    });
}

const WorkoutRun* ScriptedHub::workoutOf(double handle) const {
    const auto found = m_workouts.find(handle);
    return found == m_workouts.end() ? nullptr : &found->second;
}

// ---- The brains ----

void ScriptedHub::canUseWorldFlags(double human, bool allowed, int chance) {
    onBrain(human, [allowed, chance](Brain& brain) {
        brain.senses().worldFlags = allowed;
        brain.senses().worldFlagPercent = allowed ? chance : 0;
    });
}

bool ScriptedHub::hasEnemies(double human) const {
    const Brain* brain = m_scripted->brain(human);
    if (brain == nullptr) {
        return false;
    }
    const auto found = m_enemyCounts.find(brain);
    return found != m_enemyCounts.end() && found->second > 0;
}

// ---- The goals ----

void ScriptedHub::goalAreaWalker(const script::AreaWalkerCall& call) {
    onBrain(call.human, [this, call](Brain& brain) {
        brain.pushGoal(std::make_unique<AreaWalkerGoal>(call.flag, call.radius, call.mode, call.durationSeconds,
                                                        call.pauseSeconds, m_services));
    });
}

void ScriptedHub::goalBoxer(double human, double target) {
    onBrain(human, [this, target](Brain& brain) { brain.pushGoal(std::make_unique<BoxerGoal>(target, m_services)); });
}

void ScriptedHub::goalGrabTarget(double human, double target) {
    onBrain(human,
            [this, target](Brain& brain) { brain.pushGoal(std::make_unique<GrabTargetGoal>(target, m_services)); });
}

void ScriptedHub::goalPeddler(const script::PeddlerCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onBrain(call.human, [this, call](Brain& brain) {
        brain.pushGoal(
            std::make_unique<PeddlerGoal>(call.range, call.reacts, call.greetAnim, call.idleAnim, m_services, m_said));
    });
}

void ScriptedHub::goalPlayGenAnim(double human, int anim, std::string_view callback) {
    onBrain(human, [this, anim, name = std::string(callback)](Brain& brain) {
        brain.pushGoal(std::make_unique<PlayGenAnimGoal>(anim, name, m_scripted));
    });
}

void ScriptedHub::goalShopkeeper(const script::ShopkeeperCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onBrain(call.human, [this, call](Brain& brain) {
        brain.pushGoal(std::make_unique<ShopkeeperGoal>(call.store, call.kind, call.broom, call.range, call.onDisturbed,
                                                        call.onPhone, call.pleads, m_services, m_said));
    });
}

// ---- The gangs ----

void ScriptedHub::canFlee(int gang, bool on) {
    if (m_scripted->defer([this, gang, on] { canFlee(gang, on); })) {
        return;
    }
    const Gang* found = m_scripted->owner().gangs().find(gang);
    if (found == nullptr) {
        return;
    }
    if (!on) {
        m_flights.erase(gang);
        return;
    }
    const int members = static_cast<int>(found->members().size());
    m_flights[gang] = Flight{.start = members, .standing = found->standing()};
}

void ScriptedHub::clearBums() {
    const Brain* player = m_scripted->player();
    const Gang* gang = player != nullptr ? player->gang() : nullptr;
    if (gang == nullptr) {
        return;
    }
    std::vector<double> bums;
    for (const Brain* member : gang->members()) {
        if (member != player && categoryOf(*member) == script::kBumCategory) {
            bums.push_back(member->handle());
        }
    }
    for (const double bum : bums) {
        m_scripted->humanDelete(bum);
    }
}

std::vector<double> ScriptedHub::memberHandles(int gang) const {
    std::vector<double> handles;
    if (const Gang* found = m_scripted->owner().gangs().find(gang); found != nullptr) {
        for (const Brain* member : found->members()) {
            handles.push_back(member->handle());
        }
    }
    return handles;
}

void ScriptedHub::clearGangHandlers(int gang) {
    for (int message = 0; message < kGangMessages; ++message) {
        m_scripted->owner().gangs().setMessageHandler(gang, message, {});
    }
}

bool ScriptedHub::goodToGo(int gang, bool ignoreBusy) const {
    const Gang* found = m_scripted->owner().gangs().find(gang);
    if (found == nullptr) {
        return gang != -1;
    }
    for (const Brain* member : found->members()) {
        const human::Human& body = member->human();
        if (body.health().depleted() || categoryOf(*member) == script::kBumCategory) {
            continue;
        }
        if (body.fighter().holdState().has_value() || body.fighter().held() != nullptr ||
            body.state() == human::TargetState::Grounded) {
            return false;
        }
        if (!ignoreBusy && (!member->attackSlots().empty() || member->target() != nullptr)) {
            return false;
        }
    }
    return true;
}

bool ScriptedHub::isASpawner(int gang, std::string_view name) const {
    const Spawner* spawner = m_scripted->humanHost().spawners().find(gang, name);
    return spawner != nullptr && spawner->inUse;
}

void ScriptedHub::makeNeutralOfType(int gang, int type) {
    if (m_scripted->defer([this, gang, type] { makeNeutralOfType(gang, type); })) {
        return;
    }
    m_scripted->owner().gangs().makeNeutralOfType(gang, type);
}

// ---- The update ----

void ScriptedHub::update(std::uint64_t nowMs) {
    updateWorkouts(nowMs);
    updateEnemyCounts();
    updateFlight();
}

void ScriptedHub::updateWorkouts(std::uint64_t nowMs) {
    const WorkoutSettings* settings = m_lookups.workout;
    std::vector<Brain*> ended;
    for (auto it = m_workouts.begin(); it != m_workouts.end();) {
        Brain* brain = m_scripted->brain(it->first);
        WorkoutRun& run = it->second;
        if (brain == nullptr) {
            it = m_workouts.erase(it);
            continue;
        }
        human::Human& body = brain->human();
        if (!run.begun) {
            // The start: within reach of the equipment the human turns to it and begins; out of reach it gives up.
            const std::optional<anim::Vec3> equipment = m_scripted->locate(run.equipment);
            const bool reach = equipment && std::hypot(equipment->x - body.position().x,
                                                       equipment->y - body.position().y) <= kWorkoutReach;
            if (!reach) {
                it = m_workouts.erase(it);
                continue;
            }
            body.face(*equipment);
            body.script().workingOut = true;
            run.begun = true;
            run.blend = 1.0F;
            run.effort = 1.0F / kPlayerBlendScale;
            run.nextRepMs = nowMs + kRepMs;
            if (settings != nullptr) {
                callBack(settings->onStart, it->first);
            }
            ++it;
            continue;
        }
        // A player pumps with cross (the step of the effort's phase) and quits with triangle; the effort decays
        // otherwise (**Coney stand-in**: the decay is the second table's value of the phase per update).
        if (brain->type() == BrainType::Player && !brain->dead()) {
            const combat::CommandId command = body.record().command;
            if (command == kQuitCommand) {
                ended.push_back(brain);
                ++it;
                continue;
            }
            const std::size_t phase = phaseOf(run.effort);
            if (settings != nullptr) {
                run.effort += command == kPumpCommand ? settings->rates[0].at(phase) : -settings->rates[1].at(phase);
            }
            run.effort = std::min(run.effort, 1.0F);
            run.blend = run.effort * kPlayerBlendScale;
            if (run.effort <= 0.0F) {
                ended.push_back(brain);
                ++it;
                continue;
            }
        }
        // The repetitions: one every kRepMs at an effort of 1, faster or slower with it.
        if (nowMs >= run.nextRepMs && run.blend > 0.0F) {
            if (settings != nullptr) {
                callBack(settings->onRep, it->first);
            }
            run.nextRepMs = nowMs + static_cast<std::uint64_t>(static_cast<float>(kRepMs) / std::max(run.blend, 0.1F));
        }
        ++it;
    }
    for (Brain* brain : ended) {
        endWorkout(*brain);
    }
}

void ScriptedHub::updateEnemyCounts() {
    Brain* player = m_scripted->player();
    if (player == nullptr) {
        return;
    }
    // Every kEnemyCountPeriod brain updates: the non-player members of every gang with the player on their enemy list.
    const std::uint64_t round = player->updates() / kEnemyCountPeriod;
    const auto counted = m_countedAt.find(player);
    if (counted != m_countedAt.end() && counted->second == round) {
        return;
    }
    m_countedAt[player] = round;
    int count = 0;
    const Gangs& gangs = m_scripted->owner().gangs();
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        const Gang* gang = gangs.find(id);
        if (gang == nullptr) {
            continue;
        }
        for (const Brain* member : gang->members()) {
            if (member->type() != BrainType::Player &&
                std::ranges::find(member->enemies(), player) != member->enemies().end()) {
                ++count;
            }
        }
    }
    m_enemyCounts[player] = count;
}

void ScriptedHub::updateFlight() {
    Gangs& gangs = m_scripted->owner().gangs();
    for (auto it = m_flights.begin(); it != m_flights.end();) {
        Gang* gang = gangs.find(it->first);
        if (gang == nullptr) {
            it = m_flights.erase(it);
            continue;
        }
        Flight& flight = it->second;
        const int standing = gang->standing();
        const bool wentDown = standing < flight.standing;
        flight.standing = standing;
        const int percent = m_lookups.fleePercent ? m_lookups.fleePercent(gang->kind()) : 0;
        // A member went down and those still standing are no more than the percentage of the start: they flee.
        if (!wentDown || percent <= 0 || standing * 100 > percent * flight.start) {
            ++it;
            continue;
        }
        for (Brain* member : gang->members()) {
            human::Human& body = member->human();
            if (body.health().depleted() || body.state() != human::TargetState::Standing ||
                categoryOf(*member) != script::kFleeCategory) {
                continue;
            }
            // The nearest enemy is fled from; the enemies are dropped and the member stops fighting.
            const Brain* nearest = nullptr;
            for (const Brain* enemy : member->enemies()) {
                if (nearest == nullptr || member->distanceTo(*enemy) < member->distanceTo(*nearest)) {
                    nearest = enemy;
                }
            }
            const anim::Vec3 from = nearest != nullptr ? nearest->human().position() : body.position();
            const std::vector<Brain*> enemies = member->enemies();
            for (const Brain* enemy : enemies) {
                member->forget(*enemy);
            }
            member->setThreatResponse(0);
            member->pushGoal(std::make_unique<PedestrianReactionGoal>(from));
        }
        it = m_flights.erase(it);
    }
}

} // namespace coney::ai
