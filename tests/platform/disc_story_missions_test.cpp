// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the story's second to seventh missions (`level80`, `level87`, `level34`,
// `level2`, `level3`, `level5`) play as `--play-level NAME --checkpoint N` plays them, at every checkpoint: the level's
// scripts run for a while in gameplay over the play mode, headless, with no script error, and player 1 stands under
// the pad's control, moving when the stick is pushed. They run only when the environment variable CONEY_DISC names
// the disc and skip otherwise; they print counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <functional>
#include <memory>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "animation/anim_math.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/options.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/scene_stage.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "support/live_pad.h"
#include "warriors/created_humans.h"
#include "world/sector_budget.h"

namespace {

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

// What one level's run found.
struct MissionRun {
    std::uint64_t scriptErrors = 0;
    std::size_t missingBindings = 0;
    bool loaded = false;
    bool standing = false;
    float travelled = 0.0F;
    std::size_t humans = 0;
    std::vector<std::string> errors; // the scripts' error lines
    std::vector<std::string> log;    // every printed line
};

// The game's random table from the disc's executable; empty when it cannot be read.
std::vector<std::uint32_t> randomTable(const coney::io::Wad& wad) {
    auto words = coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                                coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize);
    return words ? std::move(*words) : std::vector<std::uint32_t>{};
}

// The level loader `--play-level` gives gameplay: the play mode at the scripts' player start, with the scripts' AI and
// character configuration.
coney::GameplayMode::LevelLoader playLoader(coney::platform::RenderEngine& renderer, const coney::io::Wad& wad,
                                            coney::world::SectorBudget& budget, coney::LevelScripts& scripts,
                                            const std::function<void(std::string_view)>& print,
                                            std::optional<coney::StartPlace> place = std::nullopt) {
    return [&renderer, &wad, &budget, &scripts, print,
            place](const coney::LevelStart& start,
                   const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
        std::optional<coney::human::PlayerStart> playerStart;
        coney::platform::PlayerSetup setup;
        setup.ai = coney::ai::aiConfigFrom(scripts.recorded());
        setup.types = coney::characters::CharacterTypes::fromRecorded(scripts.recorded());
        if (start.player) {
            const coney::HumanCreation& player = *start.player;
            const std::array<float, 3> p =
                player.teleported ? player.teleported->position : player.position.value_or(std::array<float, 3>{});
            playerStart = coney::human::PlayerStart{
                .position = coney::anim::Vec3{p[0], p[1], p[2]},
                .headingDegrees = player.teleported ? player.teleported->headingDegrees : player.headingDegrees};
            setup.model = player.model.empty() ? std::string(coney::human::kPlayerModel) : player.model;
            setup.type = player.type;
            setup.snapToGround = !player.teleported;
        }
        auto mode = coney::platform::PlayLevelMode::create(renderer, wad, start.level, budget, print, playerStart,
                                                           setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        // `--start`: player 1 somewhere else from the first step.
        if (place) {
            (*mode)->startAt(*place);
        }
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
}

// Suspends the brain (BrSuspend) of every human created so far who is not in player 1's gang, so that a check of the
// pad is not cut short by the AI's grabs and attacks (docs/research/ai.md#coney). With `done`, a human already in it
// is skipped and each one suspended is added, so that it can run every update as spawners add humans.
void suspendOtherGangs(coney::LevelScripts& scripts, std::set<double>* done = nullptr) {
    const coney::HumanCreation* player = scripts.humans().player(1);
    if (player == nullptr) {
        return;
    }
    for (const coney::HumanCreation& human : scripts.humans().all()) {
        if (human.handle != player->handle && (human.gang != player->gang || human.gang < 0) &&
            (done == nullptr || done->insert(human.handle).second)) {
            const std::array<coney::script::Value, 2> suspend{coney::script::Value(human.handle),
                                                              coney::script::Value(true)};
            (void)scripts.scripts().call("BrSuspend", suspend);
        }
    }
}

// Plays `level` at `checkpoint` as `--play-level` does (the preloads, the level's script, gameplay over the play mode
// with the scripts' AI and character configuration) for 20 s, the stick pushed forward for the last 2 s; or, with
// `script`, that input script from `place` (as `--start` gives it).
MissionRun playMission(const coney::io::Wad& wad, std::string_view level, int checkpoint,
                       std::string_view script = "540 stick left 0 70\n",
                       std::optional<coney::StartPlace> place = std::nullopt) {
    MissionRun run;
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return run;
    }
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const std::function<void(std::string_view)> print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as the story reaches the level, with the game's random table.
    coney::LevelScriptOptions options;
    const std::vector<std::uint32_t> table = randomTable(wad);
    if (!table.empty()) {
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(wad), level, checkpoint, print, options);
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(),
                                 playLoader(renderer, wad, budget, scripts, print, place), print);
    gameplay.setLevel(std::string(level));

    // By default 18 s with the pad at rest, then the stick 70 % forward for 2 s.
    auto input = coney::parseInputScript(script);
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, 540);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    run.loaded = play != nullptr;
    if (play == nullptr) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
        return run;
    }
    // The walk checks the pad alone: the other gangs' brains are suspended (BrSuspend) so that none grabs or downs
    // the player in those 2 s (the AI grabs and tackles, docs/research/ai.md#coney).
    suspendOtherGangs(scripts);
    const float before = play->stats().travelled;
    stack.runUntilEmpty(timer, {}, 60);
    run.travelled = play->stats().travelled - before;
    const coney::human::Human& human = play->player().human();
    run.standing = !human.airborne() && !human.fighter().health().depleted();
    run.scriptErrors = scripts.scripts().errors();
    run.humans = scripts.humans().all().size();
    for (const std::string& line : log) {
        if (line.starts_with("script error")) {
            run.errors.push_back(line);
        }
        if (line.find("is not a binding Coney has") != std::string::npos) {
            std::printf("    %s\n", line.c_str());
            ++run.missingBindings;
        }
    }
    run.log = std::move(log);
    return run;
}

} // namespace

TEST_CASE("the disc's story missions 2, 3, 5 and 6 play each checkpoint without a script error", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Each level and its checkpoints (docs/references/level-starts.md).
    constexpr std::array<std::pair<std::string_view, int>, 4> kLevels{
        {{"level80", 4}, {"level87", 5}, {"level2", 4}, {"level3", 5}}};
    for (const auto& [level, checkpoints] : kLevels) {
        for (int checkpoint = 1; checkpoint <= checkpoints; ++checkpoint) {
            INFO(level << " checkpoint " << checkpoint);
            const MissionRun run = playMission(*wad, level, checkpoint);
            REQUIRE(run.loaded);
            for (const std::string& error : run.errors) {
                UNSCOPED_INFO(error);
            }
            CHECK(run.scriptErrors == 0);
            CHECK(run.standing);
            CHECK(run.travelled > 1.0F);
            std::printf("  %.*s checkpoint %d: %zu humans created, %llu script errors, %zu missing bindings, %.1f m "
                        "walked\n",
                        static_cast<int>(level.size()), level.data(), checkpoint, run.humans,
                        static_cast<unsigned long long>(run.scriptErrors), run.missingBindings, run.travelled);
        }
    }
}

TEST_CASE("the disc's level34 plays each checkpoint without a script error or a missing binding", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Mission 4 has five checkpoints.
    constexpr int kCheckpoints = 5;
    for (int checkpoint = 1; checkpoint <= kCheckpoints; ++checkpoint) {
        INFO("level34 checkpoint " << checkpoint);
        const MissionRun run = playMission(*wad, "level34", checkpoint);
        REQUIRE(run.loaded);
        for (const std::string& error : run.errors) {
            UNSCOPED_INFO(error);
        }
        CHECK(run.scriptErrors == 0);
        CHECK(run.missingBindings == 0);
        CHECK(run.standing);
        CHECK(run.travelled > 1.0F);
        std::printf("  level34 checkpoint %d: %zu humans created, %llu script errors, %zu missing bindings, %.1f m "
                    "walked\n",
                    checkpoint, run.humans, static_cast<unsigned long long>(run.scriptErrors), run.missingBindings,
                    run.travelled);
    }
}

TEST_CASE("the disc's level34 crate stack breaks on the first square and goes", "[disc][story]") {
    // docs/research/objects.md#breakable-props: a dyn_masks crate stack breaks on the first blow of any kind, sends
    // message 6 once (F.Vandalize) and is removed at its next update. Player 1 starts 1.2 m north of the first crate
    // stack checkpoint 2 meets, facing it, and presses square four times, from his first updates: by update 60 the
    // rioters spawned nearby stand in front of him, and square goes to a human in front before any object.
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::StartPlace place;
    place.x = -26.4F;
    place.y = -75.8F;
    place.headingDegrees = 180.0F;
    const MissionRun run =
        playMission(*wad, "level34", 2, "5 tap square\n45 tap square\n85 tap square\n125 tap square\n", place);
    REQUIRE(run.loaded);
    CHECK(run.scriptErrors == 0);
    const auto count = [&run](std::string_view text) {
        return std::ranges::count_if(run.log,
                                     [text](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    CHECK(count("dyn_crate_stack") == 1);
    CHECK(count("hit, broke") == 1);
    CHECK(count("objects: prop") == 1);
    std::printf("  level34 checkpoint 2: %td crate stack hit, %td prop removed\n", count("dyn_crate_stack"),
                count("objects: prop"));
}

namespace {

// Square at a car (its window breaks), triangle (the theft starts) and the left stick turned anticlockwise at 0.9,
// 30 degrees an update, for 400 updates (docs/research/combat.md#stereo-theft: four stages of three turns), from
// update `from`.
std::string stereoTheft(int from) {
    std::string script = std::format("{} tap square\n{} tap triangle\n", from + 40, from + 120);
    constexpr int kTurnUpdates = 400;
    constexpr float kDeflection = 90.0F;
    for (int i = 0; i < kTurnUpdates; ++i) {
        const float angle =
            (std::numbers::pi_v<float> / 2.0F) + (static_cast<float>(i) * std::numbers::pi_v<float> / 6.0F);
        script += std::format("{} stick left {} {}\n", from + 150 + i, static_cast<int>(kDeflection * std::cos(angle)),
                              static_cast<int>(kDeflection * std::sin(angle)));
    }
    script += std::format("{} stick left 0 0\n", from + 150 + kTurnUpdates);
    return script;
}

// A place for startAt(): the feet at (x, y) facing `heading` degrees.
coney::StartPlace placeAt(float x, float y, float heading) {
    coney::StartPlace place;
    place.x = x;
    place.y = y;
    place.headingDegrees = heading;
    return place;
}

} // namespace

TEST_CASE("the disc's level34 radio objective: three car stereos stolen with the stick", "[disc][story]") {
    // docs/research/scripting.md#level34: the riot's radio objective counts three dyn_carstereo pick-ups (item 11)
    // from the three cars CarSpawnRadio gave a radio. From checkpoint 2, player 1 is placed beside each car in turn
    // (Coney's --start test aid stands for the walk) and steals its stereo with the pad; the third car stands past
    // the section 3 trigger, so the police scene l34_c3 plays before it. The Warrior who follows him into
    // `vCarPoizo` beside the first car starts Vermin's stereo scene, which warps player 1 to `fStereoWarp`; the first
    // theft waits for it to end, as in play.
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const std::function<void(std::string_view)> print = [&log](std::string_view line) { log.emplace_back(line); };
    coney::LevelScriptOptions options;
    const std::vector<std::uint32_t> table = randomTable(*wad);
    if (!table.empty()) {
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level34", 2, print, options);
    std::vector<std::string> trace;
    scripts.scripts().traceCalls([&trace](std::string_view line) { trace.emplace_back(line); });
    coney::GameplayMode gameplay(
        **engine, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(), scripts.flags(),
        scripts.recorded(), playLoader(**engine, *wad, budget, scripts, print, placeAt(-82.4F, -156.8F, 90.0F)), print);
    gameplay.setLevel("level34");
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    // The stereo scene in the first 600 updates; the three thefts at updates 600, 1200 and 2400; at 1800 the walk past
    // the section 3 trigger.
    constexpr int kFirst = 600;
    constexpr int kSecond = 1200;
    constexpr int kTrigger = 1800;
    constexpr int kThird = 2400;
    constexpr int kEnd = 3050;
    auto input = coney::parseInputScript(stereoTheft(kFirst) + stereoTheft(kSecond) + stereoTheft(kThird));
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    // The thefts alone are checked: every update the brains of the humans not in player 1's gang are suspended, as the
    // riot gangs' spawners place their rioters by the first car, who attack player 1 and cut his theft short.
    std::set<double> suspended;
    const auto run = [&](int updates) {
        for (int i = 0; i < updates; ++i) {
            stack.runUntilEmpty(timer, {}, 1);
            suspendOtherGangs(scripts, &suspended);
        }
    };
    const auto count = [](const std::vector<std::string>& lines, std::string_view text) {
        return std::ranges::count_if(lines,
                                     [text](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    run(kFirst);
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);
    CHECK(count(trace, "> P1.DoneStereoPoizo()") == 1);
    play->startAt(placeAt(-82.4F, -156.8F, 90.0F));
    run(kSecond - kFirst);
    play->startAt(placeAt(-114.5F, -180.8F, 180.0F));
    run(kTrigger - kSecond);
    play->startAt(placeAt(-57.97F, -250.7F, 180.0F));
    run(kThird - kTrigger);
    play->startAt(placeAt(-59.76F, -252.3F, 264.0F));
    run(kEnd - kThird);

    CHECK(scripts.scripts().errors() == 0);
    CHECK(count(log, "theft: stole the stereo of car") == 3);
    CHECK(count(trace, "> F.InventoryPickup(11)") == 3);
    // The third completes the objective: the scripts save it as done (Lua save float 3 = its target, 3).
    CHECK(count(trace, "SetLUASaveDataFloat(3, 3)") == 1);
    std::printf("  level34 radios: %td stereos stolen, %td item-11 pick-ups heard\n",
                count(log, "theft: stole the stereo of car"), count(trace, "> F.InventoryPickup(11)"));
}

namespace {

// The nearest AI human to `from` who is not a Warrior, still standing and not in a grab; null for none.
const coney::human::Human* nearestVictim(const coney::platform::PlayLevelMode& play, coney::anim::Vec3 from) {
    const coney::human::Human* best = nullptr;
    float bestDistance = 0.0F;
    for (const coney::ai::AiHuman& other : play.fighters().humans()) {
        if (other.removed || other.human == nullptr) {
            continue;
        }
        const coney::human::Human& human = *other.human;
        const coney::ai::Brain* brain = play.fighters().brainOf(human);
        if (brain == nullptr || (brain->gang() != nullptr && brain->gang()->kind() == coney::ai::kWarriorsKind) ||
            human.airborne() || human.fighter().health().depleted() || human.fighter().grabbed() ||
            human.fighter().holdState().has_value()) {
            continue;
        }
        const float distance = coney::anim::distance(human.position(), from);
        if (best == nullptr || distance < bestDistance) {
            best = &human;
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace

TEST_CASE("the disc's level34 muggings bonus counts a mugging done with the stick", "[disc][story]") {
    // docs/research/scripting.md#level34: the bonus counts the players' successful muggings (HuSetMugCallback, here
    // F.PedMugged). From checkpoint 2, player 1 is placed in front of the nearest pedestrian (Coney's --start test
    // aid stands for the walk), grabs him with circle, starts the mugging with triangle and holds the stick, 80 %
    // deflected, on the mug meter's target as a player reading the HUD does (docs/research/combat.md#mugging).
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const std::function<void(std::string_view)> print = [&log](std::string_view line) { log.emplace_back(line); };
    coney::LevelScriptOptions options;
    const std::vector<std::uint32_t> table = randomTable(*wad);
    if (!table.empty()) {
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level34", 2, print, options);
    std::vector<std::string> trace;
    scripts.scripts().traceCalls([&trace](std::string_view line) { trace.emplace_back(line); });
    coney::GameplayMode gameplay(**engine, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(),
                                 playLoader(**engine, *wad, budget, scripts, print), print);
    gameplay.setLevel("level34");

    coney::test::LivePad pad;
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    const auto step = [&] { stack.runUntilEmpty(timer, {}, 1); };
    // The scripts set the level up first.
    constexpr int kSettle = 90;
    for (int i = 0; i < kSettle; ++i) {
        step();
    }
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return;
    }
    const auto succeeded = [&log] {
        return std::ranges::count_if(log,
                                     [](const std::string& line) { return line.starts_with("mugging: succeeded"); });
    };
    // Up to 60 s: grab and mug until one mugging succeeds, then 3 s for its end clip and the callback.
    constexpr std::uint64_t kLimit = 1800;
    constexpr float kDeflection = 80.0F;
    constexpr int kRetry = 30;
    constexpr std::uint64_t kPressGap = 10;
    std::uint64_t doneAt = 0;
    std::uint64_t lastTry = 0;
    while (pad.nextFrame() < kLimit && (doneAt == 0 || pad.nextFrame() < doneAt + 90)) {
        const std::uint64_t at = pad.nextFrame();
        const coney::human::Human& me = play->player().human();
        const coney::human::Fighter& fighter = me.fighter();
        if (const auto& mugging = fighter.combat().mugging(); mugging) {
            // On the meter's target: the stick's own angle, atan2(x, y).
            const float target = mugging->targetDegrees() * std::numbers::pi_v<float> / 180.0F;
            pad.play(std::format("{} stick left {} {}\n", at, static_cast<int>(kDeflection * std::sin(target)),
                                 static_cast<int>(kDeflection * std::cos(target))));
        } else if (doneAt == 0 && fighter.held() != nullptr) {
            // Triangle, released between presses: the first spins him to the rear hold, the next starts the mugging.
            pad.play(at % kPressGap == 0 ? std::format("{} tap triangle\n", at)
                                         : std::format("{} stick left 0 0\n", at));
        } else if (doneAt == 0 && at >= lastTry + kRetry) {
            // In front of the nearest pedestrian, facing him; circle grabs.
            if (const coney::human::Human* victim = nearestVictim(*play, me.position()); victim != nullptr) {
                const coney::anim::Vec3 there = victim->position();
                const coney::anim::Vec3 ahead = coney::human::facing(victim->heading());
                coney::StartPlace place;
                place.x = there.x + (ahead.x * 0.9F);
                place.y = there.y + (ahead.y * 0.9F);
                place.z = there.z;
                place.headingDegrees = std::atan2(ahead.x, -ahead.y) * 180.0F / std::numbers::pi_v<float>;
                play->startAt(place);
                pad.play(std::format("{} stick left 0 0\n{} tap circle\n", at, at + 1));
            }
            lastTry = at;
        }
        step();
        if (doneAt == 0 && succeeded() > 0) {
            doneAt = pad.nextFrame();
        }
    }
    const auto count = [&trace](std::string_view text) {
        return std::ranges::count_if(trace,
                                     [text](const std::string& line) { return line.find(text) != std::string::npos; });
    };
    CHECK(scripts.scripts().errors() == 0);
    CHECK(succeeded() >= 1);
    CHECK(count("> F.PedMugged(") >= 1);
    std::printf("  level34 muggings: %td succeeded by frame %llu, %td mug callbacks heard\n", succeeded(),
                static_cast<unsigned long long>(doneAt), count("> F.PedMugged("));
}

TEST_CASE("the disc's level80 intro, skipped, gives the screen and the player back", "[disc][story]") {
    // docs/research/scenes.md#skipping: the intro's camera track calls the end function three times while it plays
    // (global.lua's NumCallBacks), and PreCashTheWorld fades back in only once those calls are spent. A skip that
    // dropped them left the screen black for the rest of the level (the owner's "level80 flickers black").
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const std::function<void(std::string_view)> print = [&log](std::string_view line) { log.emplace_back(line); };
    coney::LevelScriptOptions options;
    const std::vector<std::uint32_t> table = randomTable(*wad);
    if (!table.empty()) {
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level80", 1, print, options);
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(),
                                 playLoader(renderer, *wad, budget, scripts, print), print);
    gameplay.setLevel("level80");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    // Cross 3 s in (past the 2 s a scene waits before a button skips it), then 10 s more.
    constexpr int kSkipFrame = 90;
    constexpr int kFrames = 400;
    auto input = coney::parseInputScript(std::format("{} tap cross\n", kSkipFrame));
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    // Each frame after the skip, whether the screen fade is fully black.
    int frame = 0;
    int blackAfterSkip = 0;
    const auto sample = [&]() {
        const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
        if (play != nullptr && frame > kSkipFrame + 2 && play->stage().fadeLevel() >= 1.0F) {
            ++blackAfterSkip;
        }
        ++frame;
        return true;
    };
    stack.runUntilEmpty(timer, sample, kFrames);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);
    const coney::anim::Vec3 at = play->player().human().position();
    const bool teleported = std::ranges::any_of(
        log, [](const std::string& line) { return line.starts_with("gameplay: player 1 teleported"); });
    CHECK(scripts.scripts().errors() == 0);
    CHECK(play->stage().fadeLevel() == 0.0F);
    // Black only for the frames the end takes to fade back in (0.5 s), not for the rest of the level.
    CHECK(blackAfterSkip < 30);
    CHECK(teleported);
    std::printf("  level80 checkpoint 1, intro skipped at frame %d: %d fully black frames after it, fade level %.2f "
                "at frame %d, player 1 %s at (%.1f, %.1f)\n",
                kSkipFrame, blackAfterSkip, static_cast<double>(play->stage().fadeLevel()), kFrames,
                teleported ? "teleported" : "not teleported", static_cast<double>(at.x), static_cast<double>(at.y));
}

TEST_CASE("the disc's level5 plays each checkpoint without a script error", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Mission 7 has four checkpoints.
    constexpr int kCheckpoints = 4;
    for (int checkpoint = 1; checkpoint <= kCheckpoints; ++checkpoint) {
        INFO("level5 checkpoint " << checkpoint);
        const MissionRun run = playMission(*wad, "level5", checkpoint);
        REQUIRE(run.loaded);
        for (const std::string& error : run.errors) {
            UNSCOPED_INFO(error);
        }
        CHECK(run.scriptErrors == 0);
        // At checkpoint 2 the Hurricanes in the room fight him from the start, so he may be down when the stick moves.
        if (checkpoint != 2) {
            CHECK(run.standing);
            CHECK(run.travelled > 1.0F);
        }
        std::printf("  level5 checkpoint %d: %zu humans created, %llu script errors, %zu missing bindings, %.1f m "
                    "walked\n",
                    checkpoint, run.humans, static_cast<unsigned long long>(run.scriptErrors), run.missingBindings,
                    run.travelled);
    }
}
