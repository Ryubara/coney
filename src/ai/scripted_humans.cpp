// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/scripted_humans.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/route_planner.h"
#include "ai/scripted_brains.h"
#include "ai/scripted_goals.h"
#include "animation/anim_math.h"
#include "core/crowd_limits.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "raycast/collision_mesh.h"

namespace coney::ai {

namespace {

// Coney's NilHandle.
constexpr double kNilHandle = 0.0;
// `HuSetCarriedItem` sets the drop chance to this (`+0x278`).
constexpr int kCarriedChance = 100;
// A gang keeps at most this many spawners (`+0x640`).
// The gangs a spawner makes to respond to a crime are named this and a number.
constexpr std::string_view kResponderName = "Responder";

// `HuTeleportNearHuman`'s search (docs/research/characters-script.md#teleport-near): the point's distance from the
// other human, which is also the least gap between the two (12.96 squared); the headings tried, π/16 apart; the drop's
// ray starts this far above the other's capsule top and leaves the point this far above what it hits; and
// Human_GetCapsuleHeight (0x00219890), 2 m per unit of scale. The list's size is a lifted limit
// (crowd::teleportListCap()).
constexpr float kNearReach = 3.6F;
constexpr std::uint32_t kNearHeadings = 32;
constexpr float kNearRayAbove = 3.0F;
constexpr float kNearRayGap = 0.01F;
constexpr float kCapsuleHeightPerScale = 2.0F;

// A human near the other one, with his squared distance to him (Humans_FindAhead's key).
struct NearHuman {
    const human::Human* human = nullptr;
    float key = 0.0F;
};

// The humans within `reach` of `near` in 3D, `near` left out, cut in the brains' order at the list's size, largest key
// first (Humans_FindAhead 0x002274a8, SortByKeyDescending 0x003868f8). Stand-in: the brains' order stands for the
// original's table order. Stand-in (limit): the original's list of 32; past 60 humans it holds every human in reach
// (crowd::teleportListCap(), docs/engine/crowd-rules.md#lists).
// @coney-only helper of ScriptedHumans::teleportNear(), which carries Human_TeleportNear's @orig
std::vector<NearHuman> humansNear(const Brains& brains, const Brain& near, float reach) {
    std::vector<NearHuman> list;
    const anim::Vec3 centre = near.human().position();
    const std::size_t cap = crowd::teleportListCap(brains.size(), brains.engineLimits());
    for (std::size_t k = 0; k < brains.size() && list.size() < cap; ++k) {
        const Brain& brain = brains.at(k);
        if (&brain == &near) {
            continue;
        }
        const anim::Vec3 offset = anim::subtract(brain.human().position(), centre);
        if (const float key = anim::dot(offset, offset); key <= reach * reach) {
            list.push_back(NearHuman{.human = &brain.human(), .key = key});
        }
    }
    std::ranges::stable_sort(list, [](const NearHuman& a, const NearHuman& b) { return a.key > b.key; });
    return list;
}

// The first of the 32 headings round `centre` (from the game time's low five bits) whose point 3.6 m out no nav edge
// and no human of `around` refuses, moved in by the humans standing near it; nothing when every one is refused.
// `clearance` is the other's capsule radius plus twice the mover's (`moverRadius`).
// @coney-only helper of ScriptedHumans::teleportNear(), which carries Human_TeleportNear's @orig
std::optional<anim::Vec3> freePointNear(const Brains& brains, anim::Vec3 centre, float clearance, float moverRadius,
                                        std::span<const NearHuman> around) {
    const std::uint32_t first = static_cast<std::uint32_t>(brains.nowMs()) & (kNearHeadings - 1);
    for (std::uint32_t tried = 0; tried < kNearHeadings; ++tried) {
        const std::uint32_t k = (first + tried) & (kNearHeadings - 1);
        // Vec_FromHeading: (sin h, cos h), clockwise from +y.
        const float heading = static_cast<float>(k) * std::numbers::pi_v<float> / static_cast<float>(kNearHeadings);
        const anim::Vec3 direction{std::sin(heading), std::cos(heading), 0.0F};
        anim::Vec3 point = anim::add(centre, anim::scale(direction, kNearReach));
        // A nav edge on the way refuses the heading. Stand-in: lineClear() gives no fraction along the line, so the
        // refusal stands for the original's test of that fraction (at most 1) less the mover's radius against the
        // squared clearance, which refuses for every human of scale 0.85 or more.
        if (const RoutePlanner* planner = brains.planner(); planner != nullptr && !planner->lineClear(centre, point)) {
            continue;
        }
        bool refused = false;
        for (const NearHuman& other : around) {
            const float otherRadius = other.human->capsuleRadius();
            const float dx = other.human->position().x - point.x;
            const float dy = other.human->position().y - point.y;
            // A squared distance against a plain sum of the radii, as the original compares them.
            if ((dx * dx) + (dy * dy) >= otherRadius + moverRadius) {
                continue;
            }
            const float limit = clearance + otherRadius;
            if (other.key < limit * limit) {
                refused = true;
                break;
            }
            point = anim::add(centre, anim::scale(direction, std::sqrt(other.key) - otherRadius - moverRadius));
        }
        if (!refused) {
            return point;
        }
    }
    return std::nullopt;
}

} // namespace

void ScriptedHumans::onBrain(double handle, const std::function<void(Brain&)>& body) {
    // The analyzer reports the copy of `body` as leaked, but the std::function made from the lambda owns it and its
    // destructor frees it (the analyzer loses track inside libstdc++'s _M_manager).
    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
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
        // Only HuCreate's player argument 1 makes a player; any other (Ash's 2) is the hand-over priority, so he is no
        // player until a second pad joins, which Coney never has (docs/research/scripting.md#level99-p2-vermin-fence).
        if (const std::optional<ScriptedBrains::HeldHuman> held = m_scripted->heldHuman(handle)) {
            return script::HumanStatus{.alive = true,
                                       .player = held->playerIndex == 1,
                                       .arrested = false,
                                       .healthPercent = 100.0F,
                                       .gangType = 0xffff,
                                       .heldObject = 0.0,
                                       .hat = 0.0,
                                       .soundCommands = true,
                                       .hidden = false,
                                       .inCombat = false,
                                       .flags = 0};
        }
        return std::nullopt;
    }
    const human::Human& human = brain->human();
    return script::HumanStatus{.alive = human.aliveAndUp(),
                               .player = brain->type() == BrainType::Player,
                               .arrested = human.script().arrested,
                               .healthPercent = human.healthPercent(),
                               .gangType = brain->gang() != nullptr ? brain->gang()->kind() : 0xffff,
                               .heldObject = human.script().heldObject,
                               .hat = human.script().hat,
                               .soundCommands = human.script().soundCommands,
                               .hidden = human.hidden(),
                               .inCombat = !brain->attackSlots().empty() || brain->target() != nullptr,
                               .flags = human.flags()};
}

void ScriptedHumans::setHearRange(double human, bool help, float range) {
    onBrain(human, [help, range](Brain& brain) {
        if (help) {
            brain.senses().helpHearRange = range;
        } else {
            brain.senses().hearRange = range;
        }
    });
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
    onBrain(human, [](Brain& brain) {
        // Human_WakeUp enables and resumes the brain of a human a script knocked out (HuSetConscious stops it, as
        // Brain_OnKnockedOut does, and his health back does not wake it); one knocked out by his health wakes as the
        // brains see his health back.
        const bool scriptedOut = brain.human().script().knockedOut;
        brain.human().revive();
        if (scriptedOut) {
            brain.wakeUp();
        }
    });
}

void ScriptedHumans::setNormalMode(double human, bool full) {
    onBrain(human, [this, full](Brain& brain) {
        // Humans_EndAllRage first: every player's rage ends, whoever the human is (docs/research/scenes.md#join-reset).
        // Coney has one player.
        if (Brain* player = m_scripted->player(); player != nullptr && player != &brain) {
            player->human().setRageMode(false);
        }
        brain.human().setNormalMode(full);
    });
}

void ScriptedHumans::setRageMode(double human, bool on) {
    onBrain(human, [on](Brain& brain) { brain.human().setRageMode(on); });
}

void ScriptedHumans::markReachable(double human, bool reachable) {
    onBrain(human, [reachable](Brain& brain) { brain.senses().reachable = reachable; });
}

bool ScriptedHumans::tagging(double human) const { return m_tagging && m_tagging(human); }

void ScriptedHumans::setArrested(double human, bool arrested) {
    // The arrest's and the release's event (`ScriptHandler_MarshalMessage` case 0x11): `(self, other, 1 or 0)`.
    constexpr int kArrestEvent = 17;
    onBrain(human, [this, arrested](Brain& brain) {
        if (brain.human().script().arrested == arrested) {
            return;
        }
        if (!arrested) {
            if (m_arrestHook) {
                m_arrestHook(brain, false);
            }
            brain.human().setArrested(false);
            static_cast<void>(deliverEvent(brain, BrainEvent{.id = kArrestEvent, .other = nullptr, .value = 0}));
            return;
        }
        brain.human().setArrested(true);
        // Brain_OnArrested: its actions and target go, and its goals down to a FindEnemy goal (all when it has none).
        brain.clearActions();
        brain.setTarget(nullptr);
        if (brain.findGoal(GoalType::FindEnemy) == nullptr) {
            brain.clearGoals();
        } else {
            while (brain.topGoal() != nullptr && brain.topGoal()->type() != GoalType::FindEnemy) {
                brain.popGoal();
            }
        }
        static_cast<void>(deliverEvent(brain, BrainEvent{.id = kArrestEvent, .other = nullptr, .value = 1}));
        if (m_arrestHook) {
            m_arrestHook(brain, true);
        }
    });
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
        human::Human& body = brain.human();
        human::ScriptState& script = body.script();
        if (conscious) {
            // Human_WakeUp(h, 0): the knocked-out state cleared and the brain resumed (Brain_EnableAndResume); it
            // does not give BLOCKHUMANS back. Stand-in: its cross icon and move style 0x12 are not kept by Coney.
            script.knockedOut = false;
            brain.wakeUp();
        } else if (!body.knockedOut()) {
            // Human_KnockOut(h, 0), for a human not knocked out already (state 0x40000: by a script or by his health,
            // Human::knockedOut()). Stand-in: of Human_ReleaseAll only the pair is broken, the speech is not stopped,
            // the 14 s wake time is #1584's, and the cross icon and harmony statistic of a player's gang member are not
            // shown (docs/research/characters-script.md#set-conscious).
            body.fighter().breakPair();
            body.loseBlockHumans();
            body.endSlowMotion();
            // The brain stops (Brain_OnKnockedOut) and hears event 18 with his foe, unless he is down or dead already
            // (0x100050000); the mark is set whatever the state. Stand-in: down or dead (Human::downOrDead()) for the
            // dead and wounded states, as Brains::destroy() reads them.
            const bool brainToo = !body.downOrDead();
            script.knockedOut = true;
            if (brainToo) {
                Brain* foe = brain.knockOut();
                static_cast<void>(deliverEvent(brain, BrainEvent{.id = kEventDown, .other = foe}));
            }
        }
        // Player_SetStanceTimer after the knock-out writes nothing while its setting (0x00510994) is -1. Stand-in:
        // state 0x20000000 (build the anim state again) has no counterpart, as Coney picks the anim state every update.
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
        if (other == nullptr || other == &brain) {
            return;
        }
        human::Human& mover = brain.human();
        const human::Human& beside = other->human();
        const anim::Vec3 centre = beside.position();
        const float moverRadius = mover.capsuleRadius();
        const float clearance = beside.capsuleRadius() + (2.0F * moverRadius);
        // Nothing when the two are already less than 3.6 m apart in plan.
        const float apartX = centre.x - mover.position().x;
        const float apartY = centre.y - mover.position().y;
        if ((apartX * apartX) + (apartY * apartY) < kNearReach * kNearReach) {
            return;
        }
        const Brains& brains = m_scripted->owner();
        // Stand-in (limit): the humans near the other, listed up to crowd::teleportListCap() (the original's 32).
        const std::vector<NearHuman> around = humansNear(brains, *other, moverRadius + kNearReach);
        const std::optional<anim::Vec3> point = freePointNear(brains, centre, clearance, moverRadius, around);
        if (!point) {
            return;
        }
        // The drop: a ray straight down from above the other human's capsule leaves the point just above what it hits.
        anim::Vec3 at = *point;
        if (const raycast::CollisionMesh* world = brains.collision(); world != nullptr) {
            const float top = centre.z + (kCapsuleHeightPerScale * beside.collisionScale()) + kNearRayAbove;
            const raycast::Ray ray{.origin = raycast::Vec3{at.x, at.y, top},
                                   .direction = raycast::kDown,
                                   .length = top - centre.z + kNearRayAbove};
            // Stand-in: the ray meets the collision mesh only, not the world's objects (WorldManager_RayCast).
            if (const std::optional<raycast::RayHit> hit = world->rayCast(ray, {}, 0); hit) {
                at.z = top - (hit->t - kNearRayGap);
            }
        }
        // His own facing kept: only the transform's position is replaced (Human_SetTransform).
        mover.place(m_scripted->placeGround(brain.handle()), at, mover.heading() * 180.0F / std::numbers::pi_v<float>);
        brain.placed();
    });
}

void ScriptedHumans::lockPad(double human, bool locked) {
    onBrain(human, [locked](Brain& brain) { brain.human().script().padLocked = locked; });
}

void ScriptedHumans::enableController(double human, bool enable) {
    onBrain(human, [enable](Brain& brain) {
        // The record's pad flag, written through the pad call. **Coney's stand-in**: an AI brain has none, so its human
        // is left as it is (Coney keeps the flag only for the player's human).
        if (const Brain::PadControl& pad = brain.padControl(); pad) {
            pad(enable);
        }
    });
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
    onBrain(human, [this](Brain& brain) {
        human::ScriptState& script = brain.human().script();
        // Only an object whose type has a kind (+0x86, Human_GetHeldObjectKind) is dropped: an empty hand reads 0.
        // Coney's choice: with no kind query (no object database) every held object counts as having one.
        if (script.heldObject == kNilHandle || (m_heldKind && m_heldKind(script.heldObjectName) == 0)) {
            return;
        }
        // Human_DropHeld: the hand emptied; a player's hand is followed by the play mode, whose dropHeld() drops the
        // object when it sees the hand empty.
        const double held = std::exchange(script.heldObject, kNilHandle);
        script.heldObjectName.clear();
        if (brain.type() == BrainType::Player) {
            return;
        }
        // An AI's object: the play mode's drop (the held drop) puts it into the world and takes its move style off,
        // once, as Human_DropHeld removes it once; he then holds nothing. With no drop, letGoOfHeld() does both.
        if (m_heldDrop) {
            m_heldDrop(brain, held);
            brain.human().fighter().setAnimSet(0);
            brain.human().fighter().setHoldingObject(false);
        } else {
            brain.human().letGoOfHeld(false);
        }
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
            // Its move style comes off as at HuDropWeapon's drop (a player's, in the play mode's dropHeld()).
            if (brain->type() != BrainType::Player) {
                brain->human().letGoOfHeld(false);
            }
        }
    }
}

double ScriptedHumans::placeItemInHand(double human, std::string_view object,
                                       const std::function<double()>& nextHandle) {
    // Put the object in a free hand; a human that holds something already, or is in the middle of a pick-up (held flag
    // 0x4000, Human_HasHeld4000), takes nothing. Stand-in: the pick-up run (Human::pickingUp()) for the flag.
    const auto place = [item = std::string(object)](Brain& brain, double handle) {
        human::ScriptState& script = brain.human().script();
        if (script.heldObject != kNilHandle || brain.human().pickingUp()) {
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
    // Stand-in: the pick-up run for held flag 0x4000, as above.
    Brain* brain = m_scripted->brain(human);
    if (brain == nullptr || brain->human().script().heldObject != kNilHandle || brain->human().pickingUp()) {
        return kNilHandle;
    }
    // Stand-in: the handle is given now and the object itself made by the play mode's next step
    // (PlayLevelSimulation::placeItemInHand(), docs/research/objects.md#held).
    const double handle = nextHandle();
    return place(*brain, handle) ? handle : kNilHandle;
}

void ScriptedHumans::placeHatOnHead(double human, std::string_view hat, const std::function<double()>& nextHandle) {
    // The new hat replaces any he wears; the play mode knocks the old one off when it sees the change.
    const double handle = nextHandle();
    // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
    onBrain(human, [handle, name = std::string(hat)](Brain& brain) {
        human::ScriptState& script = brain.human().script();
        script.hat = handle;
        script.hatName = name;
    });
}

bool ScriptedHumans::useAnim(double human, int slot, std::string_view anim, bool loaded) {
    if (!loaded || slot < 0 || slot >= static_cast<int>(human::kUseAnimIds.size())) {
        return false;
    }
    const auto use = [slot, clip = std::string(anim)](Brain& brain) {
        human::ScriptState& script = brain.human().script();
        static_cast<void>(script.setAnimOverride(human::kUseAnimIds.at(static_cast<std::size_t>(slot)), clip));
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

bool ScriptedHumans::useAnyAnim(double human, std::uint32_t animId, std::string_view anim, bool loaded) {
    if (!loaded) {
        return false;
    }
    // While the cast is held the order waits for its human, and is taken as done.
    if (m_scripted->holding()) {
        // NOLINTNEXTLINE(bugprone-exception-escape): copying the captures can only fail on allocation
        onBrain(human, [animId, clip = std::string(anim)](Brain& brain) {
            static_cast<void>(brain.human().script().setAnimOverride(animId, clip));
        });
        return true;
    }
    Brain* brain = m_scripted->brain(human);
    return brain != nullptr && brain->human().script().setAnimOverride(animId, anim);
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
    m_scripted->changePlayerGang(gang);
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

void ScriptedHumans::goalTag(const script::TagCall& call) {
    onBrain(call.human, [this, call](Brain& brain) {
        // The flag is the human's from now until the goal ends.
        const double user = brain.handle();
        m_reservations[call.flag] = user;
        auto release = [this, flag = call.flag, user] {
            if (const auto found = m_reservations.find(flag); found != m_reservations.end() && found->second == user) {
                m_reservations.erase(found);
            }
        };
        TagServices services{.heldByOther =
                                 [this](double flag, double human) {
                                     const double holder = reservation(flag);
                                     return holder != 0.0 && holder != human;
                                 },
                             .startTag = m_tagStart,
                             .tagging = [this](double human) { return tagging(human); },
                             .blank = m_tagBlank};
        static_cast<void>(brain.pushGoal(std::make_unique<TagGoal>(call.flag, call.tag, call.extra, *m_scripted,
                                                                   std::move(services), std::move(release))));
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
            m_scripted->owner().formations().keepDefaults(static_cast<int>(set), *slots);
        }
    }
}

void ScriptedHumans::setDefaultFollowSlots(int set, std::span<const std::pair<float, float>> slots) {
    m_scripted->owner().formations().setDefaults(set, slots);
}

void ScriptedHumans::runRageHandlers() {
    // The humans raging before this pass's changes: the original's count of ragers (W_GameState + 0x268).
    int ragers = 0;
    for (const auto& [handle, brain] : m_scripted->bound()) {
        ragers += m_rageSeen[handle].raging ? 1 : 0;
    }
    for (const auto& [handle, brain] : m_scripted->bound()) {
        const combat::RageMeter& rage = brain->human().fighter().combat().rage();
        RageSeen& seen = m_rageSeen[handle];
        // Lua 4's true is 1 and its false nil, so a false flag is the argument left off.
        const std::array<double, 2> args{handle, 1.0};
        const std::span<const double> withFlag(args);
        const std::span<const double> withoutFlag = withFlag.first(1);
        if (rage.full() && !seen.full && !m_rage.onFull.empty()) {
            static_cast<void>(m_scripted->call(m_rage.onFull, withFlag));
        }
        // The flag is whether no other human rages: counted before this one is added, and after it is taken off.
        if (rage.raging() && !seen.raging) {
            if (!m_rage.onEnter.empty()) {
                static_cast<void>(m_scripted->call(m_rage.onEnter, ragers < 1 ? withFlag : withoutFlag));
            }
            ++ragers;
        }
        if (!rage.raging() && seen.raging) {
            --ragers;
            if (!m_rage.onExit.empty()) {
                static_cast<void>(m_scripted->call(m_rage.onExit, ragers < 1 ? withFlag : withoutFlag));
            }
        }
        seen = RageSeen{.full = rage.full(), .raging = rage.raging()};
    }
}

double ScriptedHumans::reservation(double flag) const {
    const auto found = m_reservations.find(flag);
    return found == m_reservations.end() ? kNilHandle : found->second;
}

} // namespace coney::ai
