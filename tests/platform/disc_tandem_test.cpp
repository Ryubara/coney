// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 starts a tandem at an enemy his ally holds from behind
// (docs/research/combat-moves.md#tandem, issue #412), in a level as `coney --play-level level99` plays it
// (support/disc_session.h): two fighters of the debug menus' spawn stand ahead of player 1, the enemy facing him and,
// behind the enemy, an ally (moved to a gang of the player's kind) whose brain presses circle (the grab, command
// `0xd`) once. With the enemy held from his rear, a driver walks player 1 toward him with the left stick at 60 %
// (turned by the camera, as a player steers), lets go of the stick within 2 m and presses square, or holds circle for
// 12 steps (command `0xe`, the held circle the page names as a trigger). The three play one set's clips (the ally the
// set's + 0, the enemy + 1, player 1 + 2), and the enemy takes the attacker's intro and end damage (400 empty-handed)
// as his intro hands over to his end clip; the ally is let go into the fight stance. It runs only when the
// environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "ai/action.h"
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "animation/anim_math.h"
#include "combat/commands.h"
#include "combat/tandem.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The frame of play the fighters are spawned on (the intro scene long skipped), and where: the enemy this far ahead of
// player 1, the ally this far behind the enemy, m.
constexpr std::uint64_t kSpawnAt = 300;
constexpr float kEnemyAhead = 5.0F;
constexpr float kAllyBehind = 1.0F;
// The left stick's deflection on the walk to the enemy: a walk, under the run's 0.95.
constexpr float kWalkStick = 0.6F;
// How near the enemy player 1 lets go of the stick, m: inside square's 3 m and the intro's reach of 1.27-1.72 m.
constexpr float kNear = 2.0F;
// How long anything here may take: 30 s of frames.
constexpr std::uint64_t kLimit = 900;

// The ally's brain's one press: circle tapped (command 0xd, as the AI's attack action writes it), then done.
class PressAction final : public coney::ai::Action {
  public:
    explicit PressAction(coney::combat::CommandId command) : m_command(command) {}
    coney::ai::ActionStatus update(coney::ai::Brain& brain) override {
        brain.press(m_command);
        return coney::ai::ActionStatus::Done;
    }

  private:
    coney::combat::CommandId m_command;
};

// What the test wants of the driver now.
enum class Want : std::uint8_t {
    Nothing, // the pad at rest
    WalkTo,  // the left stick toward the goal at kWalkStick until within kNear of it
    Press,   // `button` held for `holdFrames` frames, then the pad at rest
};

// Player 1's pad: it steers him toward the goal through the shown camera's heading and presses what the test asks for.
// The intro scene is skipped with cross at frame 100.
class TandemDriver final : public coney::InputSource {
  public:
    explicit TandemDriver(std::function<const coney::human::Player*()> player) : m_player(std::move(player)) {}

    coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        if (frame == 100) {
            pad.buttons = coney::pad::kCross;
        }
        const coney::human::Player* player = m_player();
        switch (want) {
        case Want::Nothing:
            break;
        case Want::WalkTo:
            if (player != nullptr) {
                const coney::anim::Vec3 feet = player->human().position();
                const float dx = goal.x - feet.x;
                const float dy = goal.y - feet.y;
                if (std::hypot(dx, dy) > kNear) {
                    steer(pad, *player, dx, dy);
                } else {
                    arrived = true;
                }
            }
            break;
        case Want::Press:
            pad.buttons = button;
            if (--holdFrames <= 0) {
                want = Want::Nothing;
            }
            break;
        }
        return samples;
    }

    Want want = Want::Nothing;
    std::uint16_t button = 0;
    int holdFrames = 0;
    coney::anim::Vec3 goal{};
    bool arrived = false;

  private:
    // Sets the left stick toward (dx, dy) at kWalkStick, turned by the shown camera's heading: up is the camera's view
    // along the ground, right its right.
    static void steer(coney::PadSample& pad, const coney::human::Player& player, float dx, float dy) {
        const coney::human::PlayerSnapshot& shot = player.current();
        float fx = shot.cameraTarget.x - shot.cameraEye.x;
        float fy = shot.cameraTarget.y - shot.cameraEye.y;
        const float forward = std::hypot(fx, fy);
        const float wanted = std::hypot(dx, dy);
        if (forward < 1e-4F || wanted < 1e-4F) {
            return;
        }
        fx /= forward;
        fy /= forward;
        const float up = ((dx * fx) + (dy * fy)) / wanted;
        const float right = ((dx * fy) - (dy * fx)) / wanted;
        pad.sticks[2] = raw(right * kWalkStick);
        pad.sticks[3] = raw(-up * kWalkStick);
    }

    // The raw byte for `value` in [-1, 1] as an input script writes it, past each axis's dead zone (core/pad.h).
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }

    std::function<const coney::human::Player*()> m_player;
};

} // namespace

namespace {

// Plays the tandem on `wad` with player 1 pressing `button` for `holdFrames` frames once he stands near the enemy, and
// checks it as the header says; `name` labels the printed line.
void playTandem(const coney::io::Wad& wad, const char* name, std::uint16_t button, int holdFrames) {
    coney::test::DiscSession* session = nullptr;
    TandemDriver driver([&session]() -> const coney::human::Player* {
        coney::platform::PlayLevelMode* play = session != nullptr ? session->session().play() : nullptr;
        return play != nullptr ? &play->player() : nullptr;
    });
    coney::test::DiscSession run(wad, driver);
    session = &run;
    run.session().startAtLevel("level99", 1);
    REQUIRE(run.runUntilPlay());
    REQUIRE(run.runToPlayFrame(kSpawnAt));
    coney::platform::PlayLevelMode* play = run.session().play();
    REQUIRE(play != nullptr);

    // The enemy faces player 1; the ally stands behind him, facing his back.
    const float heading = play->playerHeadingDegrees();
    const coney::anim::Vec3 enemyAt = coney::debug::spotAhead(play->playerFeet(), heading, kEnemyAhead);
    const coney::anim::Vec3 allyAt = coney::debug::spotAhead(enemyAt, heading, kAllyBehind);
    coney::ai::AiHumans& fighters = play->fighters();
    REQUIRE(play->spawnFighter(enemyAt, heading + 180.0F));
    coney::human::Human& enemy = *fighters.humans().back().human;
    REQUIRE(play->spawnFighter(allyAt, heading + 180.0F));
    coney::human::Human& ally = *fighters.humans().back().human;
    coney::ai::Brain* enemyBrain = fighters.brainOf(enemy);
    coney::ai::Brain* allyBrain = fighters.brainOf(ally);
    REQUIRE(enemyBrain != nullptr);
    REQUIRE(allyBrain != nullptr);
    // The enemy stands still; the ally joins a gang of player 1's kind (the fighters' gang is its enemy) and keeps no
    // goals of its own, so its one press is the test's.
    enemyBrain->setEnabled(false);
    coney::ai::Gangs& gangs = fighters.brains().gangs();
    const coney::ai::Gang* playerGang = fighters.playerBrain().gang();
    const int allies = gangs.create(playerGang != nullptr ? playerGang->kind() : 0, "tandem ally");
    REQUIRE(allies >= 0);
    gangs.addMember(allies, *allyBrain);
    allyBrain->clearGoals();
    static_cast<void>(allyBrain->clearActions());
    run.run(5);
    REQUIRE(allyBrain->queueAction(std::make_unique<PressAction>(coney::combat::command::kCircleTapped)));
    REQUIRE(run.runUntil([&enemy] { return enemy.heldFromRear(); }, 120));
    REQUIRE(ally.fighter().held() == &enemy);

    // Player 1 walks up at 60 %, lets go of the stick and presses square.
    driver.goal = enemy.position();
    driver.want = Want::WalkTo;
    REQUIRE(run.runUntil([&driver] { return driver.arrived; }, kLimit));
    driver.want = Want::Nothing;
    run.run(3);
    REQUIRE(enemy.heldFromRear());
    const coney::human::Human& player = play->player().human();
    const int healthBefore = enemy.fighter().health().value();
    driver.button = button;
    driver.holdFrames = holdFrames;
    driver.want = Want::Press;

    // Each update's clips: the set from player 1's intro, then when each of the three starts and when the damage lands.
    int base = -1;
    int start = -1;
    int allyStart = -1;
    int enemyStart = -1;
    int enemyEnd = -1;
    int damageAt = -1;
    int allyFree = -1;
    int healthAfter = healthBefore;
    for (int update = 0; update < 240; ++update) {
        run.run(1);
        const auto playerClip = static_cast<int>(player.animator().animId());
        if (base < 0 && playerClip >= coney::combat::kTandem1 && playerClip <= coney::combat::kKnifeTandem + 5 &&
            (playerClip - coney::combat::kTandem1) % 6 == 2) {
            base = playerClip - 2;
            start = update;
        }
        if (base < 0) {
            continue;
        }
        if (allyStart < 0 && static_cast<int>(ally.animator().animId()) == base) {
            allyStart = update;
        }
        if (enemyStart < 0 && static_cast<int>(enemy.animator().animId()) == base + 1) {
            enemyStart = update;
        }
        if (enemyEnd < 0 && static_cast<int>(enemy.animator().animId()) == base + 4) {
            enemyEnd = update;
        }
        if (damageAt < 0 && enemy.fighter().health().value() != healthBefore) {
            damageAt = update;
            healthAfter = enemy.fighter().health().value();
        }
        if (allyFree < 0 && allyStart >= 0 && ally.fighter().held() == nullptr &&
            ally.fighter().tandemRole() == coney::human::TandemRole::None) {
            allyFree = update;
        }
    }
    std::printf(
        "level99 tandem by %s: set %d; player intro at %d, ally %d, enemy %d; enemy end clip at %d, damage at %d "
        "(%d -> %d); ally let go at %d\n",
        name, base, start, allyStart, enemyStart, enemyEnd, damageAt, healthBefore, healthAfter, allyFree);
    REQUIRE(base >= 0);
    CHECK((base == coney::combat::kTandem1 || base == coney::combat::kTandem2 || base == coney::combat::kTandem3));
    // The three start together (the ally on its own update after player 1's, the same step).
    CHECK(enemyStart == start);
    CHECK(std::abs(allyStart - start) <= 1);
    // 50 + 350 as the enemy's intro hands over to his end clip; all his health when that defeats him.
    REQUIRE(damageAt >= 0);
    CHECK(damageAt == enemyEnd);
    CHECK(healthBefore - healthAfter == std::min(400, healthBefore));
    // The ally is let go after the damage.
    CHECK(allyFree > damageAt);
}

} // namespace

TEST_CASE("the disc's level99: square, or circle held, at an enemy an ally holds from behind plays the tandem",
          "[disc][combat]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    SECTION("square pressed") { playTandem(*wad, "square", coney::pad::kSquare, 1); }
    SECTION("circle held for 12 steps (command 0xe)") { playTandem(*wad, "circle held", coney::pad::kCircle, 12); }
}
