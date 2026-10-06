// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc: (1) the level scripts, run alone as `--play-level` runs them, put player 1
// where the research says for a few known checkpoints (values written below, from docs/references/level-starts.md),
// as the character his type names, the hub's Warchief and the Rumble arenas' player on their flags; (2) STORY from the
// main menu reaches Rembrandt standing at level99's checkpoint 1 under the pad's control, and QUICK RUMBLE, through
// the Rumble menu's four screens, reaches a Baseball Fury standing on the Fight Pen's flag, headless, through the
// original's modes with the game's own scripts; (3) the hub's chat events run without a script error; (4) the game's
// random table is read from the disc's executable. They run only when the environment variable CONEY_DISC
// names the disc and skip otherwise; they print counts and positions only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_system.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "gamemodes/rumble_menu_mode.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/created_humans.h"
#include "warriors/game_state.h"
#include "world/sector_budget.h"
#include "world_objects/flags.h"

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

// A binding host that ignores the menus' requests: the hub run below has no front end.
class QuietHost final : public coney::script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

// One start the research lists: the level, the checkpoint, player 1's name, type, model, position and heading.
struct KnownStart {
    std::string_view level;
    int checkpoint;
    std::string_view name;
    int type;
    std::string_view model;
    std::array<float, 3> position;
    float heading;
};

// A few entries of docs/references/level-starts.md, across levels, characters and an interior below the street, with
// the model each type's CfgChar names (docs/references/characters.md).
constexpr std::array kKnownStarts{
    KnownStart{"level99", 1, "Rembrandt", 32, "warr_re_cv", {-284.4F, 120.4F, 0.3F}, 0.0F},
    KnownStart{"level99", 2, "Rembrandt", 30, "warr_re", {46.0F, 34.2F, 0.3F}, 154.0F},
    KnownStart{"level2", 3, "Cleon", 1, "warr_cl", {445.3F, -45.1F, 8.0F}, 297.0F},
    KnownStart{"level3", 4, "Snow", 33, "warr_sn", {-268.5F, 353.6F, 16.6F}, 1.0F},
    KnownStart{"level5", 2, "Ajax", 11, "warr_aj", {14.4F, 2.4F, -200.0F}, 90.0F},
};

// Whether `a` and `b` are the same place to a centimetre.
bool samePlace(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return std::abs(a[0] - b[0]) < 0.01F && std::abs(a[1] - b[1]) < 0.01F && std::abs(a[2] - b[2]) < 0.01F;
}

// Gameplay's level loader as main sets it up: the play mode with player 1 where the scripts left him, as the character
// his type names, not snapped after a teleport.
coney::GameplayMode::LevelLoader playLoader(coney::platform::RenderEngine& renderer, const coney::io::Wad& wad,
                                            coney::world::SectorBudget& budget) {
    return [&renderer, &wad,
            &budget](const coney::LevelStart& start,
                     const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
        std::optional<coney::human::PlayerStart> playerStart;
        coney::platform::PlayerSetup setup;
        if (start.player) {
            const coney::HumanCreation& player = *start.player;
            const std::array<float, 3> p =
                player.teleported ? player.teleported->position : player.position.value_or(std::array<float, 3>{});
            playerStart = coney::human::PlayerStart{
                .position = coney::anim::Vec3{p[0], p[1], p[2]},
                .headingDegrees = player.teleported ? player.teleported->headingDegrees : player.headingDegrees};
            setup.model = player.model.empty() ? std::string(coney::human::kPlayerModel) : player.model;
            setup.snapToGround = !player.teleported;
        }
        auto mode = coney::platform::PlayLevelMode::create(
            renderer, wad, start.level, budget, [](std::string_view) {}, playerStart, setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
}

} // namespace

TEST_CASE("the disc's level scripts put player 1 at the researched start of each checkpoint", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    for (const KnownStart& known : kKnownStarts) {
        INFO(std::string(known.level) << " checkpoint " << known.checkpoint);
        const coney::LevelScriptRun run =
            coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), known.level, known.checkpoint, {});
        REQUIRE(run.start.player.has_value());
        const coney::HumanCreation player = run.start.player.value_or(coney::HumanCreation{});
        CHECK(player.name == known.name);
        CHECK(player.type == known.type);
        CHECK(player.model == known.model);
        CHECK(run.scriptErrors == 0);
        CHECK(player.headingDegrees == known.heading);
        REQUIRE(player.position.has_value());
        const std::array<float, 3> position = player.position.value_or(std::array<float, 3>{});
        for (std::size_t axis = 0; axis < position.size(); ++axis) {
            CHECK(std::abs(position.at(axis) - known.position.at(axis)) < 0.01F);
        }
        std::printf("  %.*s checkpoint %d: %zu humans, %llu script errors, %llu skipped calls\n",
                    static_cast<int>(known.level.size()), known.level.data(), known.checkpoint, run.humans,
                    static_cast<unsigned long long>(run.scriptErrors),
                    static_cast<unsigned long long>(run.skippedCalls));
    }
}

TEST_CASE("the disc's STORY reaches Rembrandt standing in level99 under the pad's control", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // What main sets up: the chunk handlers, the headless renderer, the sheet loader, the sector budget.
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(handlers);
    coney::platform::addSpriteSheetHandlers(handlers);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::platform::RenderEngine& renderer = **engine;
    const coney::io::Wad& theWad = *wad;
    coney::gui::GlobalStrings strings;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);

    // STORY with a new profile through the real screens (tests/support/story_new_profile.txt, as `coney --input-script`
    // plays it); then, in level99, the left stick at 35 % forward for two seconds and at a 30/65 diagonal for two more,
    // then let go.
    auto script = coney::loadInputScript(std::string(CONEY_TEST_SUPPORT_DIR) + "/story_new_profile.txt");
    auto walk = coney::parseInputScript("400 stick left 0 35\n"
                                        "460 stick left 30 65\n"
                                        "520 stick left 0 0\n");
    REQUIRE(script.has_value());
    REQUIRE(walk.has_value());
    std::vector<coney::InputEvent> events = script.value_or(std::vector<coney::InputEvent>{});
    for (const coney::InputEvent& event : walk.value_or(std::vector<coney::InputEvent>{})) {
        events.push_back(event);
    }
    coney::ScriptedInput input(std::move(events));
    coney::GameModeStack stack;
    stack.setInput(&input);
    std::vector<std::string> log;
    coney::StartUpFlow flow(
        renderer, stack,
        [&theWad, &handlers](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
            return coney::platform::loadSpriteSheetResource(theWad, handlers, name, false);
        },
        strings, coney::LegalScreenSettings{}, [&log](std::string_view line) { log.emplace_back(line); },
        coney::script::wadScriptSource(*wad),
        [&renderer, &theWad, &budget](const coney::LevelStart& start, const coney::ScriptedCast& cast)
            -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
            std::optional<coney::human::PlayerStart> playerStart;
            if (start.player && start.player->position) {
                const std::array<float, 3> p = start.player->position.value_or(std::array<float, 3>{});
                playerStart = coney::human::PlayerStart{.position = coney::anim::Vec3{p[0], p[1], p[2]},
                                                        .headingDegrees = start.player->headingDegrees};
            }
            auto mode = coney::platform::PlayLevelMode::create(
                renderer, theWad, start.level, budget, [](std::string_view) {}, playerStart, {}, &cast);
            if (!mode) {
                return std::unexpected(std::move(mode.error()));
            }
            return std::unique_ptr<coney::GameMode>(std::move(*mode));
        });
    flow.start();
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // Through the menus into the level: the profile screens, a new profile, and by frame 370 gameplay is on top with
    // level99 loaded and its intro movie asked for (skipped: no movie player yet).
    stack.runUntilEmpty(timer, {}, 370);
    for (const std::string& line : log) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(stack.topId() == coney::GameplayMode::kId);
    CHECK(flow.missionComplete().launches() == 2);
    CHECK(flow.state().checkPoint == 1.0);
    CHECK(flow.profiles().count() == 1);
    CHECK(flow.profileManager().session().newGame);
    CHECK(std::ranges::count(flow.services().movies(), std::string("L99_IN")) == 1);
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(flow.gameplay().level());
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return;
    }
    CHECK(play->sceneName() == "level99");
    // Rembrandt stands where level99.lua's HuCreate put him, on the ground below it (z 0.25 at runtime).
    const coney::anim::Vec3 feet = play->player().human().position();
    CHECK(std::abs(feet.x - -284.4F) < 0.01F);
    CHECK(std::abs(feet.y - 120.4F) < 0.01F);
    CHECK(std::abs(feet.z - 0.25F) < 0.05F);
    CHECK(!play->player().human().airborne());

    // The pad moves him, and he stands again when the stick is let go.
    stack.runUntilEmpty(timer, {}, 200);
    CHECK(play->stats().travelled > 3.0F);
    CHECK(play->player().human().speed() == 0.0F);
    CHECK(!play->player().human().airborne());
    CHECK(flow.scripts().errors() == 0);
    std::printf("  story: %zu log lines, %zu humans created, %llu Lua states, %llu skipped calls; %s", log.size(),
                flow.humans().all().size(), static_cast<unsigned long long>(flow.scripts().generation()),
                static_cast<unsigned long long>(flow.scripts().skippedCalls()), play->summary().c_str());
}

TEST_CASE("the disc's hub and Rumble arenas put player 1 on their flags", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The hub: the Warchief (Cleon in chapter 1) made at fWchiefStart_1 facing 222, then, with the tutorial locked,
    // teleported onto fWchiefStart_5 with its heading (docs/research/flags.md#player-starts).
    const coney::LevelScriptRun hub =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level95", 1, {});
    REQUIRE(hub.start.player.has_value());
    const coney::HumanCreation warchief = hub.start.player.value_or(coney::HumanCreation{});
    CHECK(warchief.type == 1);
    CHECK(warchief.model == "warr_cl");
    CHECK(samePlace(warchief.position.value_or(std::array<float, 3>{}), {-188.6F, 95.0F, -194.3F}));
    CHECK(warchief.headingDegrees == 222.0F);
    const coney::world_objects::Placement door = warchief.teleported.value_or(coney::world_objects::Placement{});
    CHECK(samePlace(door.position, {-185.2F, 112.7F, -193.7F}));
    CHECK(door.headingDegrees == 182.0F);
    std::printf("  level95 checkpoint 1: %zu humans, %zu flags, %llu script errors, %llu skipped calls\n", hub.humans,
                hub.flags, static_cast<unsigned long long>(hub.scriptErrors),
                static_cast<unsigned long long>(hub.skippedCalls));

    // Two arenas with the Rumble menu's default set-up: P11, the first Baseball Fury (type 91), made at fP1[1] facing
    // 270, then teleported onto it.
    struct Arena {
        std::string_view level;
        std::array<float, 3> flag;
        float heading;
    };
    for (const Arena& arena :
         {Arena{"level102", {-9.1F, 8.9F, -11.1F}, 128.0F}, Arena{"level103", {-66.4F, 23.2F, 0.3F}, 95.0F}}) {
        INFO(std::string(arena.level));
        coney::LevelScriptOptions options;
        options.rumbleArena = coney::rumbleArenaOf(arena.level);
        const coney::LevelScriptRun run =
            coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), arena.level, 1, {}, options);
        REQUIRE(run.start.player.has_value());
        const coney::HumanCreation player = run.start.player.value_or(coney::HumanCreation{});
        CHECK(player.name == "P11");
        CHECK(player.type == 91);
        CHECK(player.model == "fury_so1");
        CHECK(player.headingDegrees == 270.0F);
        const coney::world_objects::Placement placed = player.teleported.value_or(coney::world_objects::Placement{});
        CHECK(samePlace(placed.position, arena.flag));
        CHECK(placed.headingDegrees == arena.heading);
        CHECK(run.scriptErrors == 0);
        std::printf("  %.*s brawl: %zu humans, %zu flags, %llu script errors, %llu skipped calls\n",
                    static_cast<int>(arena.level.size()), arena.level.data(), run.humans, run.flags,
                    static_cast<unsigned long long>(run.scriptErrors),
                    static_cast<unsigned long long>(run.skippedCalls));
    }
}

TEST_CASE("the disc's hub runs its chat events without a script error", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The hub's start as runLevelScriptAlone() makes it (the preloads, a fresh state, checkpoint 1, level95), then 20 s
    // of script frames: each events.SetupConversationEvent schedules events.ChatEvent(n) 5-10 s on with
    // ScheduleFuncArg1(name, n, ms), and ChatEvent(n) indexes events.ChatTable[n] (docs/research/scripting.md#errors-
    // in-a-fresh-state). With the number and the delay swapped, ChatTable[5001...] is nil and the call fails.
    coney::GameState state;
    coney::gui::GlobalStrings strings;
    coney::script::RecordedCalls recorded;
    coney::CreatedHumans humans;
    coney::world_objects::WorldFlags flags;
    QuietHost host;
    const coney::script::BindingContext context{&state, &strings, &host, &recorded, &humans, &flags};
    coney::script::ScriptSystem scripts(coney::script::wadScriptSource(*wad),
                                        [&context](coney::script::ScriptSystem& system, coney::script::LuaVm& vm) {
                                            coney::script::installBindings(system, vm, context);
                                        },
                                        {});
    scripts.create();
    scripts.runFiles(coney::script::kEnumPreloadScripts);
    scripts.runFiles(coney::script::kConfigPreloadScripts);
    scripts.create();
    state.checkPoint = 1;
    state.currentLevel = state.levels.find("level95").value_or(0);
    const coney::LevelStart start = coney::runLevelScript(scripts, state, humans, flags, "level95");
    CHECK(start.player.has_value());

    // 20 s at the fixed 1/30 s step.
    constexpr std::uint64_t kSteps = 600;
    for (std::uint64_t step = 1; step <= kSteps; ++step) {
        const std::uint64_t nowMs = step * 100 / 3;
        scripts.setTime(nowMs);
        scripts.update(nowMs, 1.0 / 30.0);
    }
    const coney::script::Value events = scripts.vm().global("events");
    REQUIRE(events.table() != nullptr);
    const double chats = events.table() != nullptr ? events.table()->field("NumEvents").number().value_or(0.0) : 0.0;
    CHECK(chats > 0.0);
    CHECK(scripts.errors() == 0);
    std::printf("  level95 after 20 s: %.0f chat events, %llu script errors, %zu scheduled calls\n", chats,
                static_cast<unsigned long long>(scripts.errors()), scripts.scheduled());
}

TEST_CASE("the disc's executable holds the game's random table", "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto table = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize);
    REQUIRE(table.has_value());
    const std::vector<std::uint32_t> words = table.value_or(std::vector<std::uint32_t>{});
    CHECK(words.size() == coney::GameRandom::kTableSize);
    std::vector<std::uint32_t> distinct = words;
    std::ranges::sort(distinct);
    const auto repeats = std::ranges::unique(distinct);
    distinct.erase(repeats.begin(), repeats.end());
    // A table of random numbers: nearly every entry differs (counts only).
    CHECK(distinct.size() > coney::GameRandom::kTableSize / 2);
    coney::GameRandom random;
    random.setTable(words);
    for (int i = 0; i < 100; ++i) {
        const std::int32_t door = random.range(1, 5);
        CHECK(door >= 1);
        CHECK(door <= 5);
    }
    std::printf("  random table: %zu entries, %zu distinct\n", words.size(), distinct.size());
}

TEST_CASE("the disc's QUICK RUMBLE reaches a Baseball Fury standing on the Fight Pen's flag under the pad's control",
          "[disc][story]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(handlers);
    coney::platform::addSpriteSheetHandlers(handlers);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    if (!engine) {
        return;
    }
    coney::platform::RenderEngine& renderer = **engine;
    const coney::io::Wad& theWad = *wad;
    coney::gui::GlobalStrings strings;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);

    // START, the stick up most of the way (wrapping to QUICK RUMBLE), cross; cross on each of the Rumble menu's screens
    // (1 ON 1, one player, side 1's and side 2's default gangs, the Fight Pen); then in the arena the stick at 40 %
    // right and 70 % forward for two seconds, then let go.
    auto script = coney::parseInputScript("200 tap start\n"
                                          "212 stick left 0 70\n"
                                          "214 stick left 0 0\n"
                                          "225 tap cross\n"
                                          "280 tap cross\n"
                                          "290 tap cross\n"
                                          "300 tap cross\n"
                                          "305 tap cross\n"
                                          "310 tap cross\n"
                                          "360 stick left 40 70\n"
                                          "420 stick left 0 0\n");
    REQUIRE(script.has_value());
    coney::ScriptedInput input(std::move(*script));
    coney::GameModeStack stack;
    stack.setInput(&input);
    std::vector<std::string> log;
    coney::StartUpFlow flow(
        renderer, stack,
        [&theWad, &handlers](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
            return coney::platform::loadSpriteSheetResource(theWad, handlers, name, false);
        },
        strings, coney::LegalScreenSettings{}, [&log](std::string_view line) { log.emplace_back(line); },
        coney::script::wadScriptSource(*wad), playLoader(renderer, theWad, budget));
    flow.start();
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // Through the menus and the Rumble menu into the arena: by frame 350 gameplay is on top with level102 loaded.
    stack.runUntilEmpty(timer, {}, 350);
    for (const std::string& line : log) {
        UNSCOPED_INFO(line);
    }
    REQUIRE(stack.topId() == coney::GameplayMode::kId);
    CHECK(flow.rumbleMenu().started());
    // The fresh boot's 1 ON 1 set-up, as read at run time: mode 12, one player, Baseball Furies against Orphans.
    constexpr std::array<std::uint16_t, coney::RumbleSetup::kValues> kDefaultValues{
        3, 12, 1, 4, 2, 91, 94, 91, 92, 93, 94, 91, 92, 93, 225, 226, 224, 225, 226, 227, 228, 225, 226};
    CHECK(flow.state().rumble.values == kDefaultValues);
    CHECK(flow.state().rumble.gangNames[0] == "BASEBALL FURIES");
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(flow.gameplay().level());
    REQUIRE(play != nullptr);
    if (play == nullptr) {
        return;
    }
    CHECK(play->sceneName() == "level102");
    CHECK(play->model() == "fury_so1");
    // P11 stands on fP1[1], facing its heading (128 degrees), on the ground.
    const coney::anim::Vec3 feet = play->player().human().position();
    CHECK(std::abs(feet.x - -9.1F) < 0.01F);
    CHECK(std::abs(feet.y - 8.9F) < 0.01F);
    CHECK(std::abs(play->playerHeadingDegrees() - 128.0F) < 0.01F);
    CHECK(!play->player().human().airborne());

    // The pad moves him, and he stands again when the stick is let go.
    stack.runUntilEmpty(timer, {}, 100);
    CHECK(play->stats().travelled > 1.0F);
    CHECK(play->player().human().speed() == 0.0F);
    CHECK(flow.scripts().errors() == 0);
    std::printf("  quick rumble: %zu log lines, %zu humans created, %zu flags; %s", log.size(),
                flow.humans().all().size(), flow.flags().all().size(), play->summary().c_str());
}
