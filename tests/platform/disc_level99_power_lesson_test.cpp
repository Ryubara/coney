// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that level99's power-move lesson (checkpoint 1, lesson 6) plays as the
// original's does, through the player's path (`--play-level level99 --checkpoint 1`, support/disc_session.h) under
// the lessons' pad script: Rembrandt's extension 59 makes him carry `dyn_s_spraycan` from its first updates until its
// event 0x31 about 85 updates later (docs/research/combat-grabs.md#power-move-props); its spray loop is held only
// between its frames 14 and 27 and nobody holds a sound afterwards (docs/research/sound-events.md#spray-loop); and the
// scene `l99_c7` that follows takes in the bum he was holding in role 6 (`rudy`), whom its clip moves away from his
// start mark (docs/research/scenes.md#humans). It runs only when the environment variable CONEY_DISC names the disc and
// skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "audio/game_sound.h"
#include "audio/human_sound_events.h"
#include "audio/sound_engine.h"
#include "fileio/wad.h"
#include "human/holdable.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "platform/scene_stage.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/level99_lessons.h"

namespace {

// Rembrandt's power move's extension: anim id 59 (`gen_grab_front_power10_p2`, 94 updates at rate 0.75).
constexpr std::uint32_t kExtension = 59;
// The role l99_c7 binds the lesson's victim to (`rudy`).
constexpr std::size_t kRudyRole = 6;
// How far, in frames of play, the lessons may take to reach 59, and the scene to start and to end after it.
constexpr std::uint64_t kExtensionBy = 2600;
constexpr std::uint64_t kSceneWait = 600;
constexpr std::uint64_t kSceneLength = 1800;

// The lessons' pad up to lesson 6's last presses: the later lines (the l99_c7 skip) are left out, so the scene plays.
std::string lessonsToSix() {
    const std::string_view all = coney::test::kLessonsScript;
    return std::string(all.substr(0, all.find("2310 ")));
}

// The interface cue the flash's use plays (docs/research/sound-events.md, "Using a flash"): a one-shot 2D sound.
constexpr std::size_t kFlashCue = 23;

// Plays the flash's cue now and says whether it started, as a user would hear it: the cue is configured, the engine
// admitted it (a stuck sound must not starve it, #224) and it is playing.
bool cueSounds(coney::audio::SoundEngine& engine) {
    const std::uint64_t refusedBefore = engine.stats().refused;
    const coney::audio::SoundHandle handle = engine.playInterfaceSound(kFlashCue);
    return engine.interfaceSound(kFlashCue) != 0 && handle.valid() && engine.stats().refused == refusedBefore;
}

// The handle of the cast human `human` (0 when he has no brain).
double handleOf(const coney::platform::PlayLevelMode& play, const coney::human::Human& human) {
    const coney::ai::Brain* brain = play.fighters().brainOf(human);
    return brain != nullptr ? brain->handle() : 0.0;
}

} // namespace

TEST_CASE("the disc's level99: the power lesson's 59 carries the spray can, stops its spray, and l99_c7 takes Rudy",
          "[disc][story][combat][scenes][audio]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, lessonsToSix());
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    const coney::audio::HumanSoundEvents& sounds = run.audio().game().humanSounds();

    // 1. Lessons 1-6, to Rembrandt's extension.
    REQUIRE(run.runUntil(
        [&run] {
            const coney::platform::PlayLevelMode* play = run.session().play();
            return play != nullptr && play->player().human().animator().animId() == kExtension;
        },
        kExtensionBy));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);
    const coney::human::Human& rembrandt = play->player().human();
    const std::uint64_t startedAt = run.playFrames();

    coney::audio::SoundEngine* engine = run.audio().sounds().engine();
    REQUIRE(engine != nullptr);
    if (engine == nullptr) {
        return;
    }

    // 2. Through 59: the frames he carries the can, and those a sound is held; the human he holds. A one-shot cue
    // played while the spray runs sounds too.
    bool cueDuringSpray = false;
    int carried = 0;
    int firstCarried = -1;
    int lastCarried = -1;
    int held = 0;
    int lastHeld = -1;
    bool objectMatched = true;
    const coney::human::Holdable* victim = rembrandt.fighter().held();
    for (int update = 0; update < 100 && rembrandt.animator().animId() == kExtension; ++update) {
        if (rembrandt.carriedProp().has_value()) {
            ++carried;
            firstCarried = firstCarried < 0 ? update : firstCarried;
            lastCarried = update;
            CHECK(rembrandt.carriedProp().value_or(coney::human::CarriedProp{}).typeName == "dyn_s_spraycan");
            objectMatched = objectMatched && play->carriedPropOf(rembrandt).has_value();
        }
        if (sounds.heldSoundCount() > 0) {
            ++held;
            lastHeld = update;
            if (held == 5) {
                cueDuringSpray = cueSounds(*engine);
            }
        }
        if (victim == nullptr) {
            victim = rembrandt.fighter().held();
        }
        run.run(1);
    }
    std::printf("  level99 lesson 6: 59 from play frame %llu; the can carried %d updates (%d to %d), a sound held %d "
                "updates (last %d)\n",
                static_cast<unsigned long long>(startedAt), carried, firstCarried, lastCarried, held, lastHeld);
    CHECK(firstCarried >= 0);
    CHECK(firstCarried <= 2);
    // Carried until 59's event 0x31 (frame 64, update 85) or l99_c7's play, which removes it first when it comes
    // sooner: in the original the play came in the update after 0x31; Coney's, a few updates before it.
    CHECK(lastCarried >= 74);
    CHECK(lastCarried <= 85);
    CHECK(objectMatched);
    // The loop holds from frame 14 (update 19) to frame 27 (update 36): 17 updates, give or take the update an event
    // lands on.
    CHECK(held >= 14);
    CHECK(held <= 20);
    CHECK(lastHeld <= 40);
    CHECK(cueDuringSpray);
    // After the finisher a one-shot cue sounds (the flash's use, #234, was silent while the loop ran on).
    CHECK(cueSounds(*engine));
    CHECK_FALSE(rembrandt.carriedProp().has_value());
    CHECK_FALSE(play->carriedPropOf(rembrandt).has_value());
    REQUIRE(victim != nullptr);

    // 3. The scene l99_c7: the human he was holding is taken in as role 6.
    const coney::ai::AiHuman* rudy = nullptr;
    for (const coney::ai::AiHuman& fighter : play->fighters().humans()) {
        if (!fighter.removed && static_cast<const coney::human::Holdable*>(fighter.human.get()) == victim) {
            rudy = &fighter;
        }
    }
    REQUIRE(rudy != nullptr);
    const double rudyHandle = handleOf(*play, *rudy->human);
    REQUIRE(rudyHandle != 0.0);
    const coney::platform::SceneStage& stage = play->stage();
    REQUIRE(run.runUntil([&stage, rudyHandle] { return stage.frameOf(rudyHandle, 1.0F).has_value(); }, kSceneWait));
    REQUIRE(stage.frameOf(rudyHandle, 1.0F).has_value());
    const coney::scenes::RoleFrame first = stage.frameOf(rudyHandle, 1.0F).value_or(coney::scenes::RoleFrame{});
    CHECK(first.role == kRudyRole);
    CHECK(sounds.heldSoundCount() == 0);
    CHECK(cueSounds(*engine)); // and with the scene's soundtrack playing
    CHECK_FALSE(rembrandt.fighter().inPair());
    CHECK_FALSE(rudy->human->fighter().inPair());

    // 4. His role clip moves him: the farthest he gets from where the scene first put him.
    float farthest = 0.0F;
    std::uint64_t frames = 0;
    while (frames < kSceneLength && stage.holds(rudyHandle)) {
        if (const std::optional<coney::scenes::RoleFrame> now = stage.frameOf(rudyHandle, 1.0F)) {
            farthest = std::max(farthest, std::hypot(now->feet.x - first.feet.x, now->feet.y - first.feet.y));
        }
        run.run(1);
        ++frames;
    }
    std::printf("  level99 l99_c7: Rudy in role %zu, moved up to %.2f m from his start in %llu frames; %zu sounds "
                "held\n",
                first.role, static_cast<double>(farthest), static_cast<unsigned long long>(frames),
                sounds.heldSoundCount());
    CHECK(farthest > 5.0F);
    CHECK(sounds.heldSoundCount() == 0);
}
