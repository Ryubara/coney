// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the last lesson of `level99`'s combat tutorial is a fight: the level plays
// as `--play-level level99 --checkpoint 1` plays it, headless; once the intro is skipped the script's own set-up of
// the lesson is called as the research's saved state was made (`AddFenceWarriors1`, the fence gang made friends with
// the Warriors, `P1.SetupWarriors`, docs/research/ai-fights.md#level99-save), the scene `l99_c5` plays and its return
// function `P1.SendWarriors` gives each of the three sparring Warriors `GoalFight` on a player who stands still
// (the recording `warriors_passive`, docs/research/ai-fights.md#level99-fight). Then each Warrior must close on the
// player and attack him, running in with the EngageEnemy goal the Melee goal pushes
// (docs/research/ai-fights.md#fight-approach), and a charge (its run-in's attack of a charge kind) starts within the
// run-in distance; the sparring gang must be id 4 with no tactic and threat response 2, the two fence gangs under a
// cheering crowd. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// counts, distances and speeds only (LEGAL.md).
//
// Only the first Warrior charges, as in the original: his charge makes the player busy, and the other two, a little
// behind him because they steer round each other, stop at the first re-plan that finds the player busy within 3.75 m
// (EngageEnemy step 9, docs/research/ai-fights.md#level99-fight). So their run-ins end beyond the 1.6 m run-in
// distance.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/attack_action.h"
#include "ai/attack_choice.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/fidget_action.h"
#include "ai/fight_goal.h"
#include "ai/gangs.h"
#include "ai/goal.h"
#include "ai/tactic_crowd.h"
#include "ai/taunt_action.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "support/action_starts.h"
#include "support/each_case.h"
#include "world/sector_budget.h"

namespace {

// The pad: the intro scene skipped, then nothing (the player stands still, as in `warriors_passive`).
constexpr std::string_view kPad = "0 stick left 0 0\n100 tap cross\n";
// The frame the lesson's set-up is called on, the intro long skipped.
constexpr std::uint64_t kCallAt = 300;
// How long the scene `l99_c5` (16.7 s) may take to start and end, in frames.
constexpr std::uint64_t kSceneWait = 1500;
// How long the fight is watched after the scene, in frames (10 s at 30 per second).
constexpr int kFightFrames = 300;
// The taunts are counted over as long as the original's recording of the fight ran: 1,198 updates from the three
// run-ins (docs/research/ai-fights.md#fight-reposition).
constexpr int kTauntFrames = 1198;
// The taunt's line, `cheer2`.
constexpr int kTauntLine = 0x11;
// The fewest taunts every AI sequence gives over those updates. The original's three said 11 (10 in the recorded run,
// which started 6 updates later: the two waiters 7 and 3 from the band 4-4.75 m out, the faced one none); Coney's eight
// sequences give 4, 7, 5, 8, 7, 6, 9 and 4 with the band-keeping move and the charging fighter of #606 (the fewest, 4,
// is 2 above this bound; before #606 sequence 2 sat exactly at it), and gave 0-8 while its waiters stood where their
// last attack left them, inside the taunt's 2.75 m (docs/research/ai-fights.md#runtime-taunts).
constexpr int kLeastTaunts = 2;
// How near every Warrior comes to the player: 0.75 × his far melee range (5 m), the distance within which the run-in
// may stop for a busy target (EngageEnemy step 9, `0x002afa48`, docs/research/ai-fights.md#engage-enemy). Beyond it the
// run-in stops only for a target this Warrior may not attack, and step 5 has just made the player attackable.
constexpr float kCloseRange = 3.75F;
// The run-in distance, m: the charge starts only within it (EngageEnemy step 11, `0x002afa48`,
// docs/research/ai-fights.md#engage-charge).
constexpr float kRunIn = 1.6F;
// What one update at the charge's speed (up to 11.7 m/s) can take beyond that before the charge is seen, m.
constexpr float kChargeSeenSlack = 0.4F;
// The sparring Warriors' gang (docs/research/ai-fights.md#level99-fight).
constexpr std::string_view kSparringGang = "CombatWarriors";
// Its id: the gangs are made in record order, and it is the fifth (docs/research/ai-fights.md#level99-fight).
constexpr int kSparringGangId = 4;

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

// The id of the gang named `name`, or -1.
int gangNamed(const coney::ai::Gangs& gangs, std::string_view name) {
    for (std::size_t id = 0; id < coney::ai::kGangSlots; ++id) {
        const coney::ai::Gang* gang = gangs.find(static_cast<int>(id));
        if (gang != nullptr && gang->name() == name) {
            return static_cast<int>(id);
        }
    }
    return -1;
}

// What is watched of one sparring Warrior while the fight runs.
struct Watched {
    const coney::ai::Brain* brain = nullptr;
    float startDistance = 0.0F;
    float nearest = 1e9F;
    int attacks = 0;         // attack actions started (its next-attack time moved on)
    bool engaged = false;    // seen running in with an EngageEnemy goal on top
    float engageEnd = -1.0F; // the distance when its first EngageEnemy left the top of its stack; -1 before
    float fastest = 0.0F;    // its highest speed during its run-in (EngageEnemy), m/s
    bool tackled = false;    // held the player while its run-in was on top: the charge as a tackle (then a mount)
    float chargeAt = -1.0F;  // the distance when its charge was first seen (the run or the hold); -1 for none
    std::uint64_t lastNextAttack = 0;
    coney::test::ActionStarts taunts;           // reposition taunts started (a `cheer2` fight fidget)
    coney::test::ActionStarts gloats;           // gloats over the downed player started (a taunt action, kind 3 or 4)
    int fightStarts = 0;                        // fight goals started (a new one on top, its update count afresh)
    const coney::ai::Goal* lastFight = nullptr; // the fight goal last seen on top
    std::uint32_t lastFightUpdates = 0;         // its update count then
};

// Counts `watched`'s fight goal starts: a fight goal on top that is another than the last seen, or whose update count
// went back (a new one where the last was freed). One a block covered is the same goal, its count running on.
void countFightStart(Watched& watched) {
    const auto* fight = dynamic_cast<const coney::ai::FightGoal*>(watched.brain->topGoal());
    if (fight == nullptr) {
        return;
    }
    if (fight != watched.lastFight || fight->updates() < watched.lastFightUpdates) {
        ++watched.fightStarts;
    }
    watched.lastFight = fight;
    watched.lastFightUpdates = fight->updates();
}

// Counts `watched`'s reposition taunt when one has just started at the front of its actions: the fight fidget that
// says `cheer2` (the original's FidgetAction_Start says it, 0x002fa090).
void countTaunt(Watched& watched) {
    const auto* fidget = dynamic_cast<const coney::ai::FidgetAction*>(watched.brain->frontAction());
    const bool taunting = fidget != nullptr && fidget->started() && fidget->line() == kTauntLine;
    watched.taunts.see(taunting ? fidget : nullptr);
}

// Counts `watched`'s gloat over the downed player when one has just started at the front of its actions: the taunt
// action of kind 3 (a cheer) or 4 (the downed man's own opponent) that event 0x15 queues (Brain_DefaultOnEvent,
// 0x00292d80; the original's call of TauntAction_Init is 0x00293428).
void countGloat(Watched& watched) {
    const auto* taunt = dynamic_cast<const coney::ai::TauntAction*>(watched.brain->frontAction());
    const bool gloating =
        taunt != nullptr && (taunt->kind() == coney::ai::kCheerTaunt || taunt->kind() == coney::ai::kTauntAtTaunt);
    watched.gloats.see(gloating ? taunt : nullptr);
}

// One line of the fight's trace for update `update`: the player's place and health, and each sparring Warrior's place,
// heading, top goal, front action and counts, the floats in hexadecimal so that a difference in the last bit shows.
std::string traceLine(int update, const coney::platform::PlayLevelMode& level, const std::vector<Watched>& warriors) {
    const coney::human::Human& player = level.player().human();
    const coney::anim::Vec3 p = player.position();
    std::string line =
        std::format("{} player {:a} {:a} {:a} health {}", update, p.x, p.y, p.z, player.fighter().health().value());
    for (const Watched& watched : warriors) {
        const coney::human::Human& human = watched.brain->human();
        const coney::anim::Vec3 at = human.position();
        const coney::ai::Goal* top = watched.brain->topGoal();
        const coney::ai::Action* front = watched.brain->frontAction();
        line += std::format(" | {:a} {:a} {:a} heading {:a} goal {} action {} attacks {} taunts {} gloats {}", at.x,
                            at.y, at.z, human.heading(),
                            top != nullptr ? coney::ai::goalTypeName(top->type()) : std::string_view("none"),
                            front != nullptr ? typeid(*front).name() : "none", watched.attacks, watched.taunts.count(),
                            watched.gloats.count());
    }
    return line;
}

// Every AI sequence the lesson can take: the brains' variation (`--ai-seed`) 0-7, each a test case of its own so that
// ctest runs them in parallel (docs/guides/testing.md#one-test-per-item).
constexpr std::array<std::uint32_t, 8> kSeeds{0, 1, 2, 3, 4, 5, 6, 7};

// The lesson's fight under one AI seed: the Warriors close on the player and attack. When `trace` is given, a line of
// the fight for each update is added to it (traceLine()), so two runs of one seed can be compared.
void playSparringFight(std::uint32_t seed, std::vector<std::string>* trace = nullptr) {
    CAPTURE(seed);
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as --play-level runs them, with the game's random table.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, print, options);
    coney::GameplayMode::LevelLoader loader =
        [&renderer, &wad, &budget, &scripts,
         &print](const coney::LevelStart& start,
                 const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
        std::optional<coney::human::PlayerStart> playerStart;
        coney::platform::PlayerSetup setup;
        setup.ai = coney::ai::aiConfigFrom(scripts.recorded());
        setup.types = coney::characters::CharacterTypes::fromRecorded(scripts.recorded());
        if (start.player) {
            const coney::HumanCreation& player = *start.player;
            const std::array<float, 3> p = player.position.value_or(std::array<float, 3>{});
            playerStart = coney::human::PlayerStart{.position = coney::anim::Vec3{p[0], p[1], p[2]},
                                                    .headingDegrees = player.headingDegrees};
            setup.model = player.model.empty() ? std::string(coney::human::kPlayerModel) : player.model;
            setup.type = player.type;
        }
        auto mode = coney::platform::PlayLevelMode::create(renderer, *wad, start.level, budget, print, playerStart,
                                                           setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        (*mode)->useHud(scripts.hud());
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel("level99");
    gameplay.setAiVariation(seed);
    coney::scenes::SceneSystem* sceneSystem = nullptr;
    gameplay.setSceneMaker([&wad, &sceneList, &sceneSystem] {
        auto made = std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                                 coney::scenes::SceneSystem::ScriptCall{});
        sceneSystem = made.get();
        return made;
    });

    auto events = coney::parseInputScript(kPad);
    REQUIRE(events.has_value());
    coney::ScriptedInput pad(std::move(*events));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    const auto play = [&gameplay] { return dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level()); };
    std::uint64_t frame = 0;
    const auto step = [&] {
        stack.runUntilEmpty(timer, {}, 1);
        ++frame;
    };

    // 1. The level starts and the intro is skipped.
    while (frame < kCallAt) {
        step();
    }
    REQUIRE(play() != nullptr);
    REQUIRE(sceneSystem != nullptr);

    // 2. The lesson's set-up as the research's state was made: the fence Warriors, their gang friends with the
    // Warriors', then the scene and its return function, played to the scene's end.
    REQUIRE(scripts.scripts().call("AddFenceWarriors1", {}));
    const int fenceGang = gangNamed(play()->fighters().brains().gangs(), "FenceWarriors1");
    if (fenceGang >= 0) {
        const std::array<coney::script::Value, 2> friends{coney::script::Value(0.0),
                                                          coney::script::Value(static_cast<double>(fenceGang))};
        CHECK(scripts.scripts().call("GangMakeFriends", friends));
    }
    REQUIRE(scripts.scripts().call("P1.SetupWarriors", {}));
    bool played = false;
    bool playing = false;
    while (frame < kCallAt + kSceneWait && !played) {
        step();
        const bool active = sceneSystem->cinematicActive();
        played = playing && !active;
        playing = active;
    }
    REQUIRE(played);

    // 3. The fight, the player standing still: each sparring Warrior's nearest approach and attacks.
    const coney::platform::PlayLevelMode& level = *play();
    const int sparring = gangNamed(level.fighters().brains().gangs(), kSparringGang);
    REQUIRE(sparring >= 0);
    const coney::ai::Gang* gang = level.fighters().brains().gangs().find(sparring);
    REQUIRE(gang != nullptr);
    // Who fights (docs/research/ai-fights.md#level99-fight): the sparring gang is id 4, has no tactic and keeps the
    // threat response 2 every brain is made with, so its GoalFight runs in full; the fence gangs cheer under a crowd
    // tactic.
    CHECK(sparring == kSparringGangId);
    CHECK(gang->tactic() == nullptr);
    for (const coney::ai::Brain* member : gang->members()) {
        CHECK(member->threatResponse() == coney::ai::kDefaultThreatResponse);
    }
    // Their gesture slots (docs/research/ai-fights.md#fight-reposition): gang brains of reaction kind 2 with help level
    // 2, so they may take both of kind 2's slots, as the original's sparring Warriors did (scenario warriors_gestures).
    for (const coney::ai::Brain* member : gang->members()) {
        std::printf("  level99 sparring Warrior (seed %u): reaction kind %d, help level %d\n", seed,
                    coney::ai::reactionKind(*member), coney::ai::helpLevel(*member));
        CHECK(coney::ai::reactionKind(*member) == 2);
        CHECK(coney::ai::helpLevel(*member) == 2);
    }
    int crowds = 0;
    for (std::size_t id = 0; id < coney::ai::kGangSlots; ++id) {
        const coney::ai::Gang* other = level.fighters().brains().gangs().find(static_cast<int>(id));
        if (other != nullptr && other->name().starts_with("FenceWarriors")) {
            const bool crowd = dynamic_cast<const coney::ai::TacticCrowd*>(other->tactic()) != nullptr;
            std::printf("  level99 fence gang %zu: %zu members, %s\n", id, other->members().size(),
                        crowd ? "crowd tactic" : "no crowd tactic");
            crowds += crowd ? 1 : 0;
        }
    }
    CHECK(crowds == 2);
    std::vector<Watched> warriors;
    const coney::anim::Vec3 player = level.player().human().position();
    for (const coney::ai::Brain* member : gang->members()) {
        const coney::anim::Vec3 at = member->human().position();
        warriors.push_back(Watched{.brain = member,
                                   .startDistance = std::hypot(at.x - player.x, at.y - player.y),
                                   .lastNextAttack = member->nextAttackMs()});
    }
    const int healthBefore = level.player().human().fighter().health().value();
    for (int i = 0; i < kFightFrames; ++i) {
        step();
        const coney::anim::Vec3 now = play()->player().human().position();
        for (Watched& watched : warriors) {
            const coney::anim::Vec3 at = watched.brain->human().position();
            const float distance = std::hypot(at.x - now.x, at.y - now.y);
            watched.nearest = std::min(watched.nearest, distance);
            const coney::ai::Goal* top = watched.brain->topGoal();
            const bool engagedNow = top != nullptr && top->type() == coney::ai::GoalType::EngageEnemy;
            // A charge is the run-in's own attack action of a charge kind (EngageEnemy step 11.3), or the tackle's
            // hold; the speed is only printed. Speed alone misreads a runner going fast for another reason: once a
            // charger who used to stall beside the player attacks on arrival (#606), a run-in Warrior still 4.5-5.3 m
            // off, in the run gait 4, reads 8.87 m/s for an update (seeds 5 and 7).
            if (engagedNow) {
                watched.fastest = std::max(watched.fastest, watched.brain->human().speed());
                watched.tackled =
                    watched.tackled || watched.brain->human().fighter().held() == &play()->player().human();
                const auto* attack = dynamic_cast<const coney::ai::AttackAction*>(watched.brain->frontAction());
                const bool charging = (attack != nullptr && coney::ai::isChargeKind(attack->kind())) || watched.tackled;
                if (charging && watched.chargeAt < 0.0F) {
                    watched.chargeAt = distance;
                }
            }
            // Where its run-in ended: the charge ends it on the player, a stop short of him.
            if (watched.engaged && !engagedNow && watched.engageEnd < 0.0F) {
                watched.engageEnd = distance;
            }
            watched.engaged = watched.engaged || engagedNow;
            if (watched.brain->nextAttackMs() != watched.lastNextAttack) {
                watched.lastNextAttack = watched.brain->nextAttackMs();
                ++watched.attacks;
            }
            countTaunt(watched);
            countGloat(watched);
            countFightStart(watched);
        }
        if (trace != nullptr) {
            trace->push_back(traceLine(i, *play(), warriors));
        }
    }
    // The rest of the recorded fight, for the taunts and the gloats alone.
    for (int i = kFightFrames; i < kTauntFrames; ++i) {
        step();
        for (Watched& watched : warriors) {
            countTaunt(watched);
            countGloat(watched);
            countFightStart(watched);
        }
        if (trace != nullptr) {
            trace->push_back(traceLine(i, *play(), warriors));
        }
    }
    const int healthAfter = play()->player().human().fighter().health().value();
    int closed = 0;
    int attacked = 0;
    int engaged = 0;
    int charged = 0;
    int taunts = 0;
    int gloats = 0;
    for (const Watched& watched : warriors) {
        taunts += watched.taunts.count();
        gloats += watched.gloats.count();
        std::printf("  level99 sparring Warrior (seed %u): %.2f m from the player when sent, %s at up to %.1f m/s, %s, "
                    "charge at %.2f m, run-in ended at %.2f m, nearest %.2f m, %d attacks, %d taunts, %d gloats, %d "
                    "fight goals started, "
                    "%zu goals\n",
                    seed, watched.startDistance, watched.engaged ? "ran in" : "did not run in", watched.fastest,
                    watched.tackled ? "tackled" : "no tackle", watched.chargeAt, watched.engageEnd, watched.nearest,
                    watched.attacks, watched.taunts.count(), watched.gloats.count(), watched.fightStarts,
                    watched.brain->goalCount());
        engaged += watched.engaged ? 1 : 0;
        closed += watched.nearest < kCloseRange ? 1 : 0;
        attacked += watched.attacks > 0 ? 1 : 0;
        if (watched.chargeAt >= 0.0F) {
            ++charged;
            // A charge, run or tackle, starts within the run-in distance.
            CHECK(watched.chargeAt <= kRunIn + kChargeSeenSlack);
        }
    }
    std::printf(
        "  level99 sparring fight (seed %u): %zu Warriors, %d closed in, %d attacked, %d charged, %d taunts and %d "
        "gloats in %d updates; player health %d -> %d\n",
        seed, warriors.size(), closed, attacked, charged, taunts, gloats, kTauntFrames, healthBefore, healthAfter);
    if (closed < 3 || attacked == 0) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    // What every AI sequence of the lesson keeps (docs/research/ai-fights.md#level99-fight). The three stand 8.6-9.0 m
    // off, beyond 1.1 × their far range, so each fight goal ends at once and the Melee goal sends each in with an
    // EngageEnemy run; each comes within 0.75 × far; the lesson's fight starts. How the run-ins end is the AI's
    // draws: in the recording one Warrior charged and two stopped short at 2.1-2.6 m, while other sequences give no
    // charge, a tackle charge (which mounts the player), or a stop at the charge distance when no charge kind is
    // picked, so neither is counted here.
    CHECK(warriors.size() == 3);
    CHECK(engaged == 3);
    CHECK(closed == 3);
    CHECK(attacked > 0);
    // The waiting Warriors taunt from the band round him.
    // Expected: ai-fights.md#runtime-taunts, confirmed (runtime)
    CHECK(taunts >= kLeastTaunts);
    CHECK(scripts.scripts().errors() == 0);
}

// A test case per seed.
[[maybe_unused]] const bool kSparringRegistered = coney::test::registerEach(
    kSeeds, "[disc][story][ai]",
    [](std::uint32_t seed) {
        return std::format(
            "the disc's level99: the sparring Warriors of the last lesson close on the player and attack, AI seed {}",
            seed);
    },
    [](std::uint32_t seed) { playSparringFight(seed); });

// The update at which two traces first differ, or -1 when they are the same.
int firstDifference(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    const std::size_t common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (a[i] != b[i]) {
            return static_cast<int>(i);
        }
    }
    return a.size() == b.size() ? -1 : static_cast<int>(common);
}

// A 64-bit FNV-1a hash of a trace, printed so that runs in separate processes can be compared too.
std::uint64_t traceHash(const std::vector<std::string>& trace) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::string& line : trace) {
        for (const char c : line) {
            hash = (hash ^ static_cast<unsigned char>(c)) * 0x100000001b3ULL;
        }
        hash = (hash ^ static_cast<unsigned char>('\n')) * 0x100000001b3ULL;
    }
    return hash;
}

// A seeded run on the fixed step reproduces exactly (docs/guides/testing.md, test mode): one AI sequence played twice
// in one process, with another sequence played between, gives the same fight update for update, the counts the test
// keeps included (#1668: the taunt count of one sequence varied between runs while the fight itself did not).
TEST_CASE("the disc's level99: the sparring fight under one AI seed plays the same twice in one process, and after "
          "another seed",
          "[disc][story][ai]") {
    // Sequence 2 is one whose taunt count varied (4 or 5); 6 is played between.
    // Expected: ai-fights.md#runtime-taunts, confirmed (runtime)
    constexpr std::uint32_t kSeed = 2;
    constexpr std::uint32_t kOtherSeed = 6;
    std::vector<std::string> first;
    playSparringFight(kSeed, &first);
    std::vector<std::string> other;
    playSparringFight(kOtherSeed, &other);
    std::vector<std::string> again;
    playSparringFight(kSeed, &again);
    std::printf("%s", std::format("  level99 sparring fight traces (seed {}): {:016x}, then {:016x}; seed {} between: "
                                  "{:016x}\n",
                                  kSeed, traceHash(first), traceHash(again), kOtherSeed, traceHash(other))
                          .c_str());
    REQUIRE(first.size() == static_cast<std::size_t>(kTauntFrames));
    const int differs = firstDifference(first, again);
    if (differs >= 0) {
        const auto at = static_cast<std::size_t>(differs);
        UNSCOPED_INFO("first run:  " << (at < first.size() ? first[at] : std::string("(ended)")));
        UNSCOPED_INFO("second run: " << (at < again.size() ? again[at] : std::string("(ended)")));
    }
    CHECK(differs == -1);
}

// The counter of the taunts and gloats counts each action that starts, also one the brain made where it had just freed
// the last one, at the same address: counted by address, such a taunt was missed in some runs and not others, as the
// heap reused an address or did not (#1668). The kind's actions here are stand-ins, two objects whose address the test
// chooses.
TEST_CASE("the sparring test's counter counts a new action at the address of the one before it", "[ai]") {
    const int taunt = 0;
    const int other = 0;
    // Expected: ai-fights.md#fight-reposition, confirmed (code): taunts 1000-2000 ms apart, never on consecutive
    // updates
    coney::test::ActionStarts starts;
    // A taunt for three updates, a gap of one, then another taunt where the first lived.
    starts.see(&taunt);
    starts.see(&taunt);
    starts.see(&taunt);
    starts.see(nullptr);
    starts.see(&taunt);
    CHECK(starts.count() == 2);
    // Another taunt, at another address, the update after one ends.
    starts.see(&other);
    CHECK(starts.count() == 3);
    starts.see(&other);
    CHECK(starts.count() == 3);
}

} // namespace
