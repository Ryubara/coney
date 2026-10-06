// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_story.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/route_planner.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_goals.h"
#include "ai/story_goals.h"
#include "ai/story_tactics.h"
#include "ai/tactic_attack.h"
#include "ai/track_human_goal.h"
#include "combat/meters.h"
#include "human/human.h"
#include "human/victim.h"

namespace coney::ai {

namespace {

// The flags' activity of an exit flag (docs/references/bindings/ai.md#goalmovetoexitflag).
constexpr int kExitActivity = 8;
// `HuKill`: the health it leaves and the damage it leaves pending.
constexpr int kKillHealth = 1;
constexpr int kKillDamage = 100;
// A human with this flag is not killed by `HuKill` (its meaning is not traced).
constexpr std::uint64_t kKillProof = 0x80000000;
// `GangExitWorld`'s walk: `HuExitWorld`'s gait and radius.
constexpr int kExitGait = 4;
constexpr float kExitRadius = 2.0F;
// The bum's reaction clips: 669 for type 0, 668 (the dynamic slot) for the others.
constexpr int kBumReactAnim = 669;
// The Warrior commands' stand-ins.
constexpr int kCommandFollow = 0;
constexpr int kCommandAttack = 1;
constexpr int kCommandDefend = 2;
// How far the crew keeps from the chief when it follows or defends him, metres (**Coney stand-in**).
constexpr float kCrewTrackDistance = 2.0F;

// Degrees to radians.
float radians(float degrees) { return degrees * std::numbers::pi_v<float> / 180.0F; }

// A vector as a story point.
script::StoryPoint pointOf(anim::Vec3 v) { return script::StoryPoint{v.x, v.y, v.z}; }

// Whether `brain` is an AI human's (no player's).
bool isAi(const Brain& brain) { return brain.type() != BrainType::Player; }

} // namespace

void ScriptedStory::onBrain(double handle, const std::function<void(Brain&)>& body) {
    if (m_scripted->defer([this, handle, body] { onBrain(handle, body); })) {
        return;
    }
    if (Brain* found = m_scripted->brain(handle); found != nullptr) {
        body(*found);
    }
}

void ScriptedStory::onGang(int id, const std::function<void(Gang&)>& body) {
    if (m_scripted->defer([this, id, body] { onGang(id, body); })) {
        return;
    }
    if (Gang* found = m_scripted->owner().gangs().find(id); found != nullptr) {
        body(*found);
    }
}

void ScriptedStory::update() {
    Gangs& gangs = m_scripted->owner().gangs();
    for (int id = 0; id < static_cast<int>(kGangSlots); ++id) {
        Gang* gang = gangs.find(id);
        if (gang == nullptr || !gang->orders().exiting) {
            continue;
        }
        const bool anyLeft = std::ranges::any_of(
            gang->members(), [](const Brain* member) { return !member->human().fighter().health().depleted(); });
        if (anyLeft) {
            continue;
        }
        // Gone: the callback with the gang's id, then the gang unless it is kept.
        const std::string callback = gang->orders().exitCallback;
        const bool keep = gang->orders().keepWhenEmpty;
        gang->orders().exiting = false;
        if (!callback.empty()) {
            const std::array<double, 1> args{static_cast<double>(id)};
            static_cast<void>(m_scripted->call(callback, args));
        }
        if (!keep) {
            gangs.remove(id);
        }
    }
}

std::optional<script::StoryPoint> ScriptedStory::position(double handle) const {
    const std::optional<anim::Vec3> found = m_scripted->locate(handle);
    return found ? std::optional<script::StoryPoint>(pointOf(*found)) : std::nullopt;
}

std::optional<float> ScriptedStory::walkingDistance(double from, double to) {
    const std::optional<anim::Vec3> a = m_scripted->locate(from);
    const std::optional<anim::Vec3> b = m_scripted->locate(to);
    if (!a || !b) {
        return std::nullopt;
    }
    RoutePlanner* planner = m_scripted->owner().planner();
    if (planner == nullptr) {
        return anim::distance(*a, *b);
    }
    std::expected<RoutePlan, MoveFailure> plan = planner->request(*a, *b);
    if (!plan) {
        return std::nullopt;
    }
    if (!plan->route) {
        return anim::distance(*a, *b);
    }
    const Route& route = *plan->route;
    // Along the route: the start, each node, the end.
    const std::span<const world::PathNode> nodes = planner->map().nodes();
    float length = 0.0F;
    anim::Vec3 at = *a;
    for (const std::uint32_t node : route.nodes()) {
        const anim::Vec3 next = nodes[node].position;
        length += anim::distance(at, next);
        at = next;
    }
    return length + anim::distance(at, *b);
}

bool ScriptedStory::addPath(double handle, std::string_view name, const std::array<double, 8>& points) {
    if (m_paths.size() >= kPathSlots) {
        return false;
    }
    WorldPath path{.name = std::string(name), .points = {}};
    for (const double point : points) {
        if (point != 0.0 && m_scripted->flag(point)) {
            path.points.push_back(point);
        }
    }
    m_paths[handle] = std::move(path);
    return true;
}

const WorldPath* ScriptedStory::path(double handle) const {
    const auto found = m_paths.find(handle);
    return found == m_paths.end() ? nullptr : &found->second;
}

void ScriptedStory::killHuman(double human) {
    onBrain(human, [](Brain& brain) {
        human::Human& body = brain.human();
        if (body.fighter().health().depleted() || body.hasFlag(kKillProof)) {
            return;
        }
        body.fighter().health().set(kKillHealth);
        body.takeHit(human::IncomingHit{.damage = kKillDamage, .attacker = body.position(), .ignoresArmour = true});
    });
}

void ScriptedStory::setHealth(double human, int health) {
    onBrain(human, [health](Brain& brain) { brain.human().fighter().health().set(std::max(health, 0)); });
}

void ScriptedStory::setShadow(double human, bool on) {
    onBrain(human, [on](Brain& brain) { brain.human().script().shadow = on; });
}

void ScriptedStory::lockMovement(double human, bool locked) {
    onBrain(human, [locked](Brain& brain) { brain.human().script().movementLocked = locked; });
}

std::optional<std::string> ScriptedStory::controlName(double human) const {
    const Brain* brain = m_scripted->brain(human);
    if (brain == nullptr) {
        return std::nullopt;
    }
    const human::Human& body = brain->human();
    if (isAi(*brain) || brain->dead()) {
        return std::string("aiControl");
    }
    if (body.state() == human::TargetState::Held || body.state() == human::TargetState::Mounted) {
        return std::string("grabbedControl");
    }
    if (body.fighter().held() != nullptr) {
        return std::string("grabbingControl");
    }
    if (body.airborne()) {
        return std::string("jumpingControl");
    }
    if (body.fighter().lockTarget() != nullptr) {
        return std::string("lockOnControl");
    }
    return std::string("screenRelativeControl");
}

bool ScriptedStory::aimingAt(double /*human*/, double /*target*/) const { return false; }

std::string ScriptedStory::heldObject(double human) const {
    const Brain* brain = m_scripted->brain(human);
    return brain != nullptr ? brain->human().script().heldObjectName : std::string();
}

bool ScriptedStory::grabbed(double human) const {
    const Brain* brain = m_scripted->brain(human);
    if (brain == nullptr) {
        return false;
    }
    const human::TargetState state = brain->human().state();
    return state == human::TargetState::Held || state == human::TargetState::Mounted;
}

bool ScriptedStory::actionsBlocked(double human) const {
    const Brain* brain = m_scripted->brain(human);
    if (brain == nullptr) {
        return false;
    }
    const human::Human& body = brain->human();
    return body.state() != human::TargetState::Standing || body.fighter().held() != nullptr || body.airborne() ||
           body.climb().has_value() || body.fighter().health().depleted();
}

void ScriptedStory::setSightRange(double human, float range) {
    onBrain(human, [range](Brain& brain) { brain.setSight(range, brain.fieldOfView()); });
}

void ScriptedStory::setFieldOfView(double human, float degrees) {
    onBrain(human, [degrees](Brain& brain) { brain.setSight(brain.sightRange(), radians(degrees)); });
}

void ScriptedStory::setInvestigateResponse(double human, int response) {
    onBrain(human, [response](Brain& brain) { brain.senses().investigate = response; });
}

void ScriptedStory::setReactToViolence(double human, bool reacts) {
    onBrain(human, [reacts](Brain& brain) { brain.senses().reactsToViolence = reacts; });
}

void ScriptedStory::setTagColour(double human, std::uint32_t rgba) {
    onBrain(human, [rgba](Brain& brain) { brain.human().script().tagColour = rgba; });
}

void ScriptedStory::tag(double human, double tag, double flag) {
    if (m_tagHandler) {
        m_tagHandler(human, tag, flag);
    }
}

std::optional<double> ScriptedStory::nearestExit(anim::Vec3 from, double exclude) const {
    std::optional<double> best;
    float bestDistance = std::numeric_limits<float>::max();
    for (const world_objects::WorldFlag& flag : m_scripted->flags().all()) {
        if (flag.kind != kExitActivity || !flag.enabled || flag.handle == exclude) {
            continue;
        }
        const std::optional<world_objects::Placement> placement = m_scripted->flag(flag.handle);
        if (!placement) {
            continue;
        }
        const anim::Vec3 at{placement->position[0], placement->position[1], placement->position[2]};
        if (const float distance = anim::distance(from, at); distance < bestDistance) {
            best = flag.handle;
            bestDistance = distance;
        }
    }
    return best;
}

void ScriptedStory::leave(Brain& brain, double flag, int gait, float angle, float distance, float radius) {
    if (flag == 0.0) {
        const std::optional<double> nearest = nearestExit(brain.human().position(), 0.0);
        if (!nearest) {
            return;
        }
        flag = *nearest;
    }
    // Out of the AI's reach while it leaves (`BrDead`), unless a player's brain.
    if (isAi(brain)) {
        brain.setDead(true);
    }
    ExitServices services{
        .flag = [this](double handle) { return m_scripted->flag(handle); },
        .nearestExit = [this](anim::Vec3 from, double exclude) { return nearestExit(from, exclude); },
        .viewer = [this]() -> std::optional<anim::Vec3> {
            const Brain* player = m_scripted->player();
            return player != nullptr ? std::optional<anim::Vec3>(player->human().position()) : std::nullopt;
        },
        .remove = [this](Brain& leaving) { m_scripted->humanDelete(leaving.handle()); },
        .kill = [this](Brain& leaving) { killHuman(leaving.handle()); },
    };
    brain.pushGoal(std::make_unique<MoveToExitFlagGoal>(
        MoveToFlagOrder{.flag = flag, .gait = gait, .angleDegrees = angle, .distance = distance, .radius = radius},
        std::move(services)));
}

void ScriptedStory::goalMoveToExitFlag(const script::ExitFlagCall& call) {
    onBrain(call.human,
            [this, call](Brain& brain) { leave(brain, call.flag, call.gait, call.angle, call.distance, call.radius); });
}

void ScriptedStory::goalTravelPath(const script::TravelPathCall& call) {
    onBrain(call.human, [this, call](Brain& brain) {
        const WorldPath* found = path(call.path);
        std::vector<double> points = found != nullptr ? found->points : std::vector<double>{};
        brain.pushGoal(std::make_unique<TravelPathGoal>(std::move(points), call.mode, call.reverse, call.gait,
                                                        call.radius, *m_scripted));
    });
}

void ScriptedStory::goalMelee(double human, double target) {
    onBrain(human, [this, target](Brain& brain) {
        if (brain.human().state() != human::TargetState::Standing || brain.human().fighter().health().depleted()) {
            return;
        }
        // The finding goal underneath, then the fight with the target over it when one is named.
        brain.pushGoal(std::make_unique<FindEnemyGoal>());
        if (Brain* enemy = target != 0.0 ? m_scripted->brain(target) : nullptr; enemy != nullptr && enemy != &brain) {
            static_cast<void>(brain.fight(*enemy));
        }
    });
}

void ScriptedStory::goalThrowObject(const script::ThrowObjectCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onBrain(call.human, [this, call](Brain& brain) {
        brain.pushGoal(std::make_unique<ThrowObjectGoal>([this](double handle) { return m_scripted->locate(handle); },
                                                         call.target, call.range, call.gait, call.callback,
                                                         m_scripted));
    });
}

void ScriptedStory::goalPlayDynIdle(const script::DynIdleCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onBrain(call.human, [this, call](Brain& brain) {
        brain.pushGoal(std::make_unique<PlayDynIdleGoal>(
            call.flag, std::vector<std::string>{call.startAnim, call.loopAnim, call.endAnim}, call.timeMs,
            *m_scripted));
    });
}

void ScriptedStory::bumTrigger(double human) {
    onBrain(human, [this](Brain& brain) {
        const auto* bum = dynamic_cast<const BumLogicGoal*>(brain.topGoal());
        if (bum == nullptr) {
            return;
        }
        static_cast<void>(m_scripted->playClip(brain, bum->order().type == 0 ? kBumReactAnim : kDynamicAnimId));
    });
}

void ScriptedStory::addTurfBox(int gang, double box) {
    onGang(gang, [box](Gang& found) {
        std::array<double, kTurfBoxes>& turf = found.orders().turf;
        if (const auto free = std::ranges::find(turf, 0.0); free != turf.end()) {
            *free = box;
        }
    });
}

void ScriptedStory::removeTurfBox(int gang, double box) {
    onGang(gang, [box](Gang& found) { std::ranges::replace(found.orders().turf, box, 0.0); });
}

void ScriptedStory::engageEnemy(int gang, double target) {
    onGang(gang, [this, target](Gang& found) {
        Brain* enemy = m_scripted->brain(target);
        if (enemy == nullptr) {
            return;
        }
        // A copy: a fight may change the members' list through the events it sends.
        const std::vector<Brain*> members(found.members().begin(), found.members().end());
        for (Brain* member : members) {
            if (isAi(*member) && member != enemy) {
                static_cast<void>(member->fight(*enemy));
            }
        }
    });
}

void ScriptedStory::setGangInvestigateResponse(int gang, int response) {
    onGang(gang, [response](Gang& found) {
        for (Brain* member : found.members()) {
            member->senses().investigate = response;
        }
    });
}

void ScriptedStory::setRespondPercentage(int gang, int percent) {
    onGang(gang, [percent](Gang& found) { found.orders().respondPercent = percent; });
}

void ScriptedStory::setHearRange(int gang, bool help, float range) {
    onGang(gang, [help, range](Gang& found) {
        for (Brain* member : found.members()) {
            if (help) {
                member->senses().helpHearRange = range < 0.0F ? kDefaultHelpHearRange : range;
            } else {
                member->senses().hearRange = range < 0.0F ? kDefaultHearRange : range;
            }
        }
    });
}

void ScriptedStory::enableAttackStrategies(int gang, bool on) {
    onGang(gang, [on](Gang& found) { found.orders().attackStrategies = on; });
}

void ScriptedStory::setLeader(int gang, double human) {
    onGang(gang, [human](Gang& found) { found.orders().leader = human; });
}

double ScriptedStory::leader(int gang) const {
    const Gang* found = m_scripted->owner().gangs().find(gang);
    const Brain* lead = found != nullptr ? found->leader() : nullptr;
    return lead != nullptr ? lead->handle() : 0.0;
}

void ScriptedStory::gangExitWorld(int gang, double exit, std::string_view callback, bool deleteGang) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onGang(gang, [this, exit, name = std::string(callback), deleteGang](Gang& found) {
        found.orders().exiting = true;
        found.orders().exitCallback = name;
        found.orders().keepWhenEmpty = !deleteGang;
        const std::vector<Brain*> members(found.members().begin(), found.members().end());
        for (Brain* member : members) {
            if (isAi(*member)) {
                leave(*member, exit, kExitGait, 0.0F, 0.0F, kExitRadius);
            }
        }
    });
}

void ScriptedStory::startSpawner(int gang, std::string_view name, int mode, int value) {
    m_scripted->humanHost().spawners().start(gang, name, mode, value);
}

void ScriptedStory::canUseWorldFlags(int gang, bool on, int percent) {
    onGang(gang, [on, percent](Gang& found) {
        for (Brain* member : found.members()) {
            member->senses().worldFlags = on;
            member->senses().worldFlagPercent = on ? percent : 0;
        }
    });
}

void ScriptedStory::setTactic(const script::TacticCall& call) {
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    if (m_scripted->defer([this, call] { setTactic(call); })) {
        return;
    }
    std::vector<double> points;
    if (call.kind == script::TacticKind::TravelPath) {
        if (const WorldPath* found = path(call.flags.at(0)); found != nullptr) {
            points = found->points;
        }
    }
    const TacticServices services{
        .flags = m_scripted, .scripts = m_scripted, .formations = &m_scripted->owner().formations()};
    if (std::unique_ptr<Tactic> tactic = makeStoryTactic(call, std::move(points), services); tactic != nullptr) {
        m_scripted->owner().gangs().setTactic(call.gang, std::move(tactic));
    }
}

bool ScriptedStory::startWarriorCommand(double chief, int command, bool /*forced*/) {
    Brain* lead = m_scripted->brain(chief);
    if (lead == nullptr) {
        return false;
    }
    m_warriorCommand = command;
    Gang* crew = lead->gang();
    if (crew == nullptr) {
        return true;
    }
    // The crew's last orders end, then each AI member takes the command's.
    m_scripted->owner().gangs().setTactic(crew->id(), nullptr);
    const std::vector<Brain*> members(crew->members().begin(), crew->members().end());
    for (Brain* member : members) {
        if (member == lead || !isAi(*member)) {
            continue;
        }
        member->flush();
        if (command == kCommandFollow || command == kCommandDefend) {
            static_cast<void>(
                goalTrackHuman(*member, *m_scripted, m_scripted->owner().formations(), chief, kCrewTrackDistance));
        } else if (command == kCommandAttack) {
            member->pushGoal(std::make_unique<FindEnemyGoal>());
        }
    }
    return true;
}

int ScriptedStory::musicMood() const {
    const Brain* player = m_scripted->player();
    if (player == nullptr) {
        return 0;
    }
    Brains& brains = m_scripted->owner();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        Brain& brain = brains.at(i);
        if (isAi(brain) && brain.target() == player && Brain::fightable(brain) &&
            (brain.findGoal(GoalType::Fight) != nullptr || brain.findGoal(kMeleeGoal) != nullptr)) {
            return 1;
        }
    }
    return 0;
}

double ScriptedStory::playerOne() const {
    const Brain* player = m_scripted->player();
    return player != nullptr ? player->handle() : 0.0;
}

} // namespace coney::ai
