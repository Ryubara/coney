// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc the debug menus' Animation player (docs/guides/debug-menu.md#animation-player):
// in level99 as `coney --play-level level99` plays it (support/disc_session.h), a clip chosen on the page plays once on
// player 1 for its whole length at its rate and his idle comes back; in the sandbox's fight yard with level99's
// configuration, every set lists its clips, a looping clip at twice the speed plays until stopped, and clips of the
// generic set and of another character play on him. Headless, on the fixed step with no clock. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/anim_catalog.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "debug/tunables.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/game_session_debug.h"
#include "platform/play_level_controls.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/optional_value.h"
#include "support/test_paths.h"

namespace {

using coney::debug::AnimClipChoice;

// The updates a clip of `seconds` at `rate` plays once: until less than 0.1 ms of it is left, on the 1/30 s step.
int expectedUpdates(const AnimClipChoice& clip, float speed = 1.0F) {
    return static_cast<int>(std::ceil(((clip.seconds - 0.0001F) * 30.0F) / (clip.rate * speed)));
}

// The clip of anim `id` in `clips`; nothing when the set has none.
std::optional<AnimClipChoice> clipOf(const std::vector<AnimClipChoice>& clips, std::uint32_t id) {
    for (const AnimClipChoice& clip : clips) {
        if (clip.id == id) {
            return clip;
        }
    }
    return std::nullopt;
}

// The item labelled `label` on `page`, which must exist.
const coney::debug::MenuItem& itemOn(const coney::debug::MenuPage& page, std::string_view label) {
    const coney::debug::MenuItem* item = page.find(label);
    INFO(label);
    REQUIRE(item != nullptr);
    return *item;
}

// Whether `human` plays his own animation again, as the game chooses it: standing idle, or in the fight yard, locked
// onto the target ahead of him, the combat walk.
bool ownAnimation(const coney::human::Human& human) {
    const coney::human::AnimState state = human.animator().state();
    return state == coney::human::AnimState::Idle || state == coney::human::AnimState::CombatWalk;
}

// The anim id S1 (`ANIM_ATTACK_S1`): a one-off clip every Warrior has, under a second.
constexpr std::uint32_t kAttackS1 = 12;
// The frame of play the clip is chosen at: the intro scene skipped and the player free to move.
constexpr std::uint64_t kChooseAt = 300;
constexpr std::string_view kPad = "100 tap cross\n";

} // namespace

TEST_CASE("the disc's level99: a clip chosen on the Animation player plays its full length on player 1, then his "
          "idle comes back",
          "[disc][story][debug][anim]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    // The debug menus as `coney` wires them, with the play mode as `coney --play-level` gives it to them.
    coney::debug::DebugServices services;
    coney::platform::connectDebugServices(services, [&run] { return &run.session(); });
    coney::platform::PlayLevelControls controls;
    services.play = [&run, &controls] { return controls.controlsOf(run.session().play()); };
    coney::debug::TunableRegistry tunables;
    coney::debug::DebugSession debug(tunables, services, nullptr);
    REQUIRE(run.runToPlayFrame(kChooseAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    controls.setMode(play);
    const coney::human::Human& human = play->player().human();

    // The page: the player's set, the Attack category, and S1's item.
    const auto page = debug.model().openPage("Animation player");
    REQUIRE(page != nullptr);
    const std::vector<std::string> sets = controls.animSets();
    REQUIRE(sets.size() > 2);
    const coney::debug::MenuItem& setItem = itemOn(*page, sets.front());
    const auto setPage = setItem.open();
    REQUIRE(setPage != nullptr);
    const auto attacks = itemOn(*setPage, "Attack").open();
    REQUIRE(attacks != nullptr);
    auto clips = controls.animClips(0);
    REQUIRE(clips.has_value());
    const std::optional<AnimClipChoice> found = clipOf(*clips, kAttackS1);
    REQUIRE(found.has_value());
    const AnimClipChoice s1 = coney::test::got(found);
    const std::string label = std::format("{} {} {:.2f} s", s1.id, s1.clip, s1.seconds);
    itemOn(*attacks, label).run();
    REQUIRE(controls.animPlayback().playing);
    CHECK(controls.animPlayback().id == kAttackS1);
    CHECK(human.animator().state() == coney::human::AnimState::Debug);

    // It plays to its end, the pad doing nothing to him, then his own idle.
    int updates = 0;
    while (controls.animPlayback().playing && updates < 600) {
        run.run(1);
        ++updates;
    }
    std::printf("anim player: S1 played %d updates (expected %d) in level99; %zu clips in the player's set, %zu sets\n",
                updates, expectedUpdates(s1), clips->size(), sets.size());
    CHECK(updates >= expectedUpdates(s1));
    CHECK(updates <= expectedUpdates(s1) + 1);
    CHECK(human.animator().state() == coney::human::AnimState::Idle);
    CHECK(human.animator().animId() == coney::human::AnimSlots::player().ids[coney::human::kSlotIdle]);
}

TEST_CASE("the sandbox: the Animation player's sets list their clips, a loop plays until stopped, and other sets' "
          "clips play on the player",
          "[disc][debug][anim]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun scripts =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(scripts.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(scripts.recorded);
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    auto made = coney::platform::PlayLevelMode::createInSandbox(
        **engine, *wad, std::move(*world), std::nullopt, [](std::string_view) {}, setup);
    REQUIRE(made.has_value());
    coney::platform::PlayLevelMode& mode = **made;
    coney::platform::PlayLevelControls controls(&mode);
    coney::GameModeStack modes;
    coney::GameTimer timer;
    timer.setFixedStep(true);
    modes.push(mode);
    (void)modes.runUntilEmpty(timer, {}, 15);
    const coney::human::Human& human = mode.player().human();

    // The player's set, the generic set and another character's own: each lists clips by anim id.
    const std::vector<std::string> sets = controls.animSets();
    REQUIRE(sets.size() > 2);
    auto own = controls.animClips(0);
    REQUIRE(own.has_value());
    auto generic = controls.animClips(1);
    REQUIRE(generic.has_value());
    auto other = controls.animClips(2);
    REQUIRE(other.has_value());
    std::printf("anim player: %zu sets; %zu clips in the player's, %zu generic, %zu in %s's own, %zu categories\n",
                sets.size(), own->size(), generic->size(), other->size(), sets[2].c_str(),
                coney::debug::byCategory(*own).size());
    CHECK(own->size() > 500);
    CHECK(generic->size() > 500);
    CHECK_FALSE(other->empty());
    CHECK_FALSE(controls.animClips(sets.size()).has_value());

    // S1 looping at twice the speed and in place: still playing after three times its length, then stopped.
    const std::optional<AnimClipChoice> found = clipOf(*own, kAttackS1);
    REQUIRE(found.has_value());
    const AnimClipChoice s1 = coney::test::got(found);
    const coney::anim::Vec3 feet = mode.playerFeet();
    REQUIRE(
        controls.playAnim(0, kAttackS1, coney::debug::AnimPlayOptions{.loop = true, .speed = 2.0F, .inPlace = true}));
    (void)modes.runUntilEmpty(timer, {}, static_cast<std::uint64_t>(3 * expectedUpdates(s1, 2.0F)));
    CHECK(controls.animPlayback().playing);
    CHECK(controls.animPlayback().loop);
    CHECK(std::abs(mode.playerFeet().x - feet.x) < 0.01F);
    CHECK(std::abs(mode.playerFeet().y - feet.y) < 0.01F);
    controls.stopAnim();
    CHECK_FALSE(controls.animPlayback().playing);
    (void)modes.runUntilEmpty(timer, {}, 10);
    CHECK(ownAnimation(human));

    // The first clip of the generic set and of the other character, each once to its end.
    for (const auto& [set, clips] : {std::pair{std::size_t{1}, &*generic}, std::pair{std::size_t{2}, &*other}}) {
        REQUIRE_FALSE(clips->empty());
        const AnimClipChoice& clip = clips->front();
        REQUIRE(controls.playAnim(set, clip.id, {}));
        int updates = 0;
        while (controls.animPlayback().playing && updates < 3000) {
            (void)modes.runUntilEmpty(timer, {}, 1);
            ++updates;
        }
        INFO(set);
        CHECK(updates >= expectedUpdates(clip));
        CHECK(updates <= expectedUpdates(clip) + 1);
        CHECK(ownAnimation(human));
    }
    // An id a set has no clip for is refused.
    CHECK_FALSE(controls.playAnim(0, 100000, {}));
}
