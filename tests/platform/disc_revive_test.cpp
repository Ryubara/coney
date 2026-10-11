// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 revives a knocked-out Warrior with a flash, as the original does
// (docs/research/combat.md#revive, docs/research/hud.md#action-prompts): `level80` checkpoint 2 played in the game
// session as `coney --play-level level80 --checkpoint 2` plays it (support/disc_session.h), one of player 1's Warriors
// knocked out (his health gone, as a fight leaves him), and a driver that walks player 1 to him with the left stick at
// 60 % (turned by the camera, as a player steers), then taps d-pad left. Beside him with no flash no prompt shows; with
// a flash the revive prompt, `GSTRING.HUD` 4, shows; d-pad left plays 351 then 353 on player 1, spends the flash and
// heals the Warrior, who gets up with 354. It runs only when the environment variable CONEY_DISC names the disc and
// skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "combat/revive.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "gui/global_strings.h"
#include "hud/hud.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/inventory.h"

namespace {

// The left stick's deflection on the walk to the downed Warrior: a walk, under the run's 0.95.
constexpr float kWalkStick = 0.6F;
// How near the driver walks before letting go of the stick, m: inside the revive search's 3 m.
constexpr float kNear = 1.8F;
// How long anything here may take: 60 s of frames.
constexpr std::uint64_t kLimit = 1800;

// What the test wants of the driver now.
enum class Want : std::uint8_t {
    Nothing,   // the pad at rest
    SkipScene, // cross tapped every second, to skip the level's opening scene
    WalkTo,    // the left stick toward the goal at kWalkStick until within kNear of it
    Revive,    // d-pad left pressed for one frame, then the pad at rest
};

// A pad driven by what player 1 is doing: it steers him toward the goal through the shown camera's heading (the stick
// moves the player relative to the camera, docs/research/characters.md#input) and presses what the test asks for.
class ReviveDriver final : public coney::InputSource {
  public:
    explicit ReviveDriver(std::function<const coney::human::Player*()> player) : m_player(std::move(player)) {}

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        ++m_frame;
        const coney::human::Player* player = m_player();
        switch (want) {
        case Want::Nothing:
            break;
        case Want::SkipScene: {
            constexpr std::uint64_t kTapEvery = 30;
            pad.buttons = m_frame % kTapEvery == 0 ? coney::pad::kCross : 0;
            break;
        }
        case Want::WalkTo:
            if (player != nullptr) {
                const coney::anim::Vec3 feet = player->human().position();
                const float dx = goal.x - feet.x;
                const float dy = goal.y - feet.y;
                if (std::hypot(dx, dy) > kNear) {
                    steer(pad, *player, dx, dy, kWalkStick);
                } else {
                    arrived = true;
                }
            }
            break;
        case Want::Revive:
            pad.buttons = coney::pad::kLeft;
            want = Want::Nothing;
            break;
        }
        return samples;
    }

    Want want = Want::Nothing;
    coney::anim::Vec3 goal{};
    bool arrived = false;

  private:
    // Sets the left stick to move toward (dx, dy) at `deflection`, turned by the shown camera's heading: up is the
    // camera's view along the ground, right its right.
    static void steer(coney::PadSample& pad, const coney::human::Player& player, float dx, float dy, float deflection) {
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
        pad.sticks[2] = raw(right * deflection);
        pad.sticks[3] = raw(-up * deflection);
    }

    // The raw byte for `value` in [-1, 1] as an input script writes it, past each axis's dead zone (core/pad.h).
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }

    std::function<const coney::human::Player*()> m_player;
    std::uint64_t m_frame = 0;
};

// How many of `lines` start with `prefix`.
std::size_t countOf(const std::vector<std::string>& lines, std::string_view prefix) {
    return static_cast<std::size_t>(
        std::ranges::count_if(lines, [prefix](const std::string& line) { return line.starts_with(prefix); }));
}

} // namespace

TEST_CASE("the disc's level80: player 1 with a flash revives a knocked-out Warrior with d-pad left",
          "[disc][story][revive]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }

    // Checkpoint 2 as --play-level starts it; the opening scene skipped until player 1 has the pad. (At checkpoint 1
    // the command lesson holds him still.)
    coney::test::DiscSession* session = nullptr;
    ReviveDriver driver([&session]() -> const coney::human::Player* {
        coney::platform::PlayLevelMode* play = session != nullptr ? session->session().play() : nullptr;
        return play != nullptr ? &play->player() : nullptr;
    });
    coney::test::DiscSession run(*wad, driver);
    session = &run;
    run.session().startAtLevel("level80", 2);
    REQUIRE(run.runUntilPlay());
    driver.want = Want::SkipScene;
    REQUIRE(run.runUntil(
        [&run] { return run.session().play() != nullptr && run.session().play()->player().padControlled(); }, kLimit));
    driver.want = Want::Nothing;
    run.run(30);

    // The nearest Warrior of player 1's gang, made revivable as a level script does (`HuSetRevivable(h, 1)`): Coney's
    // level80 crew is not flagged (open question on docs/research/combat.md#revive: what flags the crew in the
    // original). The test knocks him out by taking his health, which the brains see as a fight's knock-out. The brains
    // belong to the session's gameplay, which the test changes through them.
    const coney::ai::Brains* found = run.session().gameplay().brains();
    REQUIRE(found != nullptr);
    auto& brains = const_cast<coney::ai::Brains&>(*found);
    coney::ai::Brain* player = nullptr;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        if (brains.at(i).type() == coney::ai::BrainType::Player) {
            player = &brains.at(i);
        }
    }
    REQUIRE(player != nullptr);
    coney::ai::Brain* warrior = nullptr;
    int crew = 0;
    float nearest = 1e9F;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        coney::ai::Brain& brain = brains.at(i);
        if (brain.type() != coney::ai::BrainType::Warrior || brain.gang() != player->gang()) {
            continue;
        }
        ++crew;
        if (const float distance = brain.distanceTo(*player); distance < nearest) {
            nearest = distance;
            warrior = &brain;
        }
    }
    REQUIRE(warrior != nullptr);
    coney::script::ScriptSystem& scripts = run.session().flow().scripts();
    const std::array<coney::script::Value, 2> revivable{coney::script::Value(warrior->handle()),
                                                        coney::script::Value(1.0)};
    REQUIRE(scripts.call("HuSetRevivable", revivable));
    coney::human::Human& downed = warrior->human();
    REQUIRE(downed.hasFlag(coney::human::flag::kRevivable));
    downed.fighter().health().set(0);
    run.run(60);
    REQUIRE(warrior->knockedOut());

    // Walked beside him with no flash: no prompt (docs/research/hud.md#action-prompts, "No flash, no prompt").
    coney::Inventory& inventory = run.session().flow().state().player.inventory;
    inventory.set(0, coney::item::kRevive, 0);
    driver.goal = downed.position();
    driver.want = Want::WalkTo;
    const coney::anim::Vec3 startAt = run.session().play()->player().human().position();
    const bool walked = run.runUntil([&driver] { return driver.arrived; }, kLimit);
    const coney::anim::Vec3 endAt = run.session().play()->player().human().position();
    UNSCOPED_INFO("from " << startAt.x << "," << startAt.y << " to " << endAt.x << "," << endAt.y << " goal "
                          << driver.goal.x << "," << driver.goal.y << " pad "
                          << run.session().play()->player().padControlled());
    REQUIRE(walked);
    driver.want = Want::Nothing;
    run.run(30);
    const coney::hud::Hud& hud = run.session().flow().hud();
    const std::string reviveText(run.session().flow().context().strings->get(4));
    REQUIRE_FALSE(reviveText.empty());
    CHECK(hud.actionPrompt(0).empty());

    // With a flash: the revive prompt.
    inventory.set(0, coney::item::kRevive, 1);
    run.run(2);
    CHECK(hud.actionPrompt(0) == reviveText);

    // D-pad left: 351 on player 1, then at its end 353, the flash spent, the Warrior healed and up with 354.
    const coney::human::Human& reviver = run.session().play()->player().human();
    driver.want = Want::Revive;
    std::vector<std::uint32_t> reviverClips;
    bool sawRevived = false;
    const bool revived = run.runUntil(
        [&] {
            const std::uint32_t id = reviver.animator().animId();
            if (reviverClips.empty() || reviverClips.back() != id) {
                reviverClips.push_back(id);
            }
            sawRevived = sawRevived || downed.animator().animId() == coney::combat::kRevivedAnim;
            return countOf(run.log(), "revive: revived human") > 0;
        },
        kLimit);
    if (!revived) {
        for (const std::string& line : run.log()) {
            if (line.starts_with("revive")) {
                UNSCOPED_INFO(line);
            }
        }
    }
    REQUIRE(revived);
    run.run(2);
    sawRevived = sawRevived || downed.animator().animId() == coney::combat::kRevivedAnim;
    const bool begin = std::ranges::find(reviverClips, coney::combat::kReviveBeginAnim) != reviverClips.end();
    const bool end = std::ranges::find(reviverClips, coney::combat::kReviveEndAnim) != reviverClips.end();
    const int flashes = inventory.count(0, coney::item::kRevive);
    std::printf("  level80 revive: %d crew Warriors; 351 %d, 353 %d, 354 %d; flashes 1 -> %d; health %d of %d\n", crew,
                begin ? 1 : 0, end ? 1 : 0, sawRevived ? 1 : 0, flashes, downed.fighter().health().value(),
                downed.fighter().health().maximum());
    CHECK(begin);
    CHECK(end);
    CHECK(sawRevived);
    CHECK(flashes == 0);
    CHECK(downed.fighter().health().value() == downed.fighter().health().maximum());
    CHECK(countOf(run.log(), "revive: reviving human") == 1);
    CHECK(countOf(run.log(), "revive: cut short") == 0);

    // He is up: his brain back on, no prompt beside him.
    run.run(30);
    CHECK_FALSE(warrior->knockedOut());
    CHECK(hud.actionPrompt(0) != reviveText);
    CHECK(run.session().flow().scripts().errors() == 0);
}
