// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 revives a knocked-out Warrior with a flash, as the original does
// (docs/research/combat-rage.md#revive, docs/research/hud.md#action-prompts): `level80` checkpoint 2 played in the game
// session as `coney --play-level level80 --checkpoint 2` plays it (support/disc_session.h), one of player 1's Warriors
// knocked out by a strike that takes all his health (no script flags him: every Warrior is made revivable), and a
// driver that walks player 1 to him with the left stick at 60 % (turned by the camera, as a player steers), then taps
// d-pad left. Beside him with no flash no prompt shows; with a flash the revive prompt, `GSTRING.HUD` 4, shows; d-pad
// left turns and slides player 1 to 351's reach from the Warrior's head, plays 351 (0.70 s), 353 and 389 on him, and
// at 351's end spends the flash and heals the Warrior, who gets up with 354. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts and measures only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "combat/anim_ranges.h"
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
#include "human/locomotion.h"
#include "human/player.h"
#include "human/victim.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
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

    // The nearest Warrior of player 1's gang: revivable from the moment he was made (Human_Init gives every Warrior
    // flag 0x4, docs/research/combat.md#human-flags), with no script call. The test knocks him out in a fight: a strike
    // (S1, hit code 0x0a) from player 1's side that takes all his health. The brains belong to the session's gameplay,
    // which the test reaches through them.
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
    coney::human::Human& downed = warrior->human();
    REQUIRE(downed.hasFlag(coney::human::flag::kRevivable));
    downed.setFlag(coney::human::flag::kGod | coney::human::flag::kDemiGod, false);
    coney::human::IncomingHit strike;
    strike.damage = downed.fighter().health().value();
    strike.attackAnim = 12;
    strike.code = 0x0a;
    strike.attacker = player->human().position();
    downed.takeHit(strike);
    run.run(60);
    REQUIRE(downed.fighter().health().depleted());
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

    // D-pad left: the steer and 351 on player 1, then at its end 353, the flash spent, the Warrior healed and up with
    // 354. Each clip's updates and the updates his heading turns are counted; the head is where the steer aims.
    const coney::human::Human& reviver = run.session().play()->player().human();
    const coney::anim::Vec3 head = downed.ledPoint();
    driver.want = Want::Revive;
    std::vector<std::pair<std::uint32_t, int>> reviverClips;
    std::vector<std::pair<std::uint32_t, int>> revivedClips;
    int turning = 0;
    int sincePress = 0;
    float lastHeading = reviver.heading();
    // The line to the head as he stands at the press (the runtime's reference), and where the steer left him at its
    // 25th update: his distance from the head in plan, and how far his facing is to the left of that line (degrees;
    // headings grow to the left). 351's root motion carries him on along his facing, off the line, so the line from
    // where he ends turns as he nears the head; the press's line does not.
    const float line = coney::human::headingOf(coney::anim::subtract(head, reviver.position()));
    constexpr int kSteerRead = 25;
    float standOff = 0.0F;
    float offLine = 0.0F;
    // Counts `id` into `clips`: one more update of the clip playing, or a new clip.
    const auto count = [](std::vector<std::pair<std::uint32_t, int>>& clips, std::uint32_t id) {
        if (clips.empty() || clips.back().first != id) {
            clips.emplace_back(id, 0);
        }
        ++clips.back().second;
    };
    // One update's readings of both humans.
    const auto watch = [&] {
        count(reviverClips, reviver.animator().animId());
        if (!revivedClips.empty() || downed.animator().animId() == coney::combat::kRevivedAnim) {
            count(revivedClips, downed.animator().animId());
        }
        if (std::fabs(coney::human::wrapAngle(reviver.heading() - lastHeading)) > 1e-4F) {
            ++turning;
        }
        lastHeading = reviver.heading();
        if (++sincePress == kSteerRead) {
            standOff = std::hypot(head.x - reviver.position().x, head.y - reviver.position().y);
            offLine = coney::human::wrapAngle(reviver.heading() - line) * 180.0F / std::numbers::pi_v<float>;
        }
    };
    const bool revived = run.runUntil(
        [&] {
            watch();
            return countOf(run.log(), "revive: revived human") > 0;
        },
        kLimit);
    if (!revived) {
        for (const std::string& entry : run.log()) {
            if (entry.starts_with("revive")) {
                UNSCOPED_INFO(entry);
            }
        }
    }
    REQUIRE(revived);
    const int flashesAtHeal = inventory.count(0, coney::item::kRevive);
    const int healthAtHeal = downed.fighter().health().value();
    for (int update = 0; update < 150; ++update) {
        run.run(1);
        watch();
    }
    // Each clip's updates in order, the first (the update of the press reads it before 351) left out.
    const auto updatesOf = [](const std::vector<std::pair<std::uint32_t, int>>& clips, std::uint32_t id) {
        const auto found = std::ranges::find_if(clips, [id](const auto& clip) { return clip.first == id; });
        return found != clips.end() ? found->second : 0;
    };
    const int begin = updatesOf(reviverClips, coney::combat::kReviveBeginAnim);
    const int end = updatesOf(reviverClips, coney::combat::kReviveEndAnim);
    const int settle = updatesOf(reviverClips, coney::human::kAnimNormalFromFight);
    const int up = updatesOf(revivedClips, coney::combat::kRevivedAnim);
    const coney::combat::AnimRange* range =
        reviver.ranges() != nullptr ? reviver.ranges()->find(coney::combat::kReviveBeginAnim) : nullptr;
    const float direction = range != nullptr
                                ? std::atan2(-range->directionX, range->directionY) * 180.0F / std::numbers::pi_v<float>
                                : 0.0F;
    std::printf("  level80 revive: %d crew Warriors; updates 351 %d, 353 %d, 389 %d, 354 %d; steer %d updates, "
                "%.2f m from the head (reach %.2f), %.1f deg left of the line (351's direction %.1f); flashes 1 -> %d; "
                "health %d of %d\n",
                crew, begin, end, settle, up, turning, standOff, range != nullptr ? range->reach : 0.0F, offLine,
                direction, flashesAtHeal, healthAtHeal, downed.fighter().health().maximum());
    // The runtime's figures (docs/research/combat-rage.md#revive-runtime): 351 21 updates, 353 33, 389 25, 354 72; the
    // steer 25 updates, ending about 0.70 m from him and 11-13 degrees to the left of the line.
    CHECK(begin >= 20);
    CHECK(begin <= 22);
    CHECK(end >= 32);
    CHECK(end <= 34);
    CHECK(settle >= 24);
    CHECK(settle <= 26);
    CHECK(up >= 71);
    CHECK(up <= 73);
    CHECK(turning >= 23);
    CHECK(turning <= 26);
    REQUIRE(range != nullptr);
    // 351's reach on the disc is 1.04 m; its root motion brings him nearer: the runtime ended 0.70-0.71 m away.
    CHECK(standOff >= 0.6F);
    CHECK(standOff <= 0.9F);
    CHECK(offLine >= 8.0F);
    CHECK(offLine <= 14.0F);
    CHECK(flashesAtHeal == 0);
    CHECK(healthAtHeal == downed.fighter().health().maximum());
    CHECK(downed.fighter().health().value() == downed.fighter().health().maximum());
    CHECK(countOf(run.log(), "revive: reviving human") == 1);
    CHECK(countOf(run.log(), "revive: cut short") == 0);

    // He is up: his brain back on, no prompt beside him.
    CHECK_FALSE(warrior->knockedOut());
    CHECK(hud.actionPrompt(0) != reviveText);
    CHECK(run.session().flow().scripts().errors() == 0);
}
