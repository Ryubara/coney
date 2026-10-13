// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the story's missions play through, as `coney --play-level NAME
// --checkpoint N` plays them (the game session, tests/support/disc_session.h), driven by an adaptive pad: a driver that
// reads where player 1 stands and what he is doing each frame and steers him with the analog sticks and buttons as a
// player would (docs/guides/testing.md#playthrough-tests). A run passes when the scripts reach the next checkpoint with
// no script error and no failed mission. They run only when the environment variable CONEY_DISC names the disc and skip
// otherwise; they print counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_math.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

using coney::human::Traversal;

// What a leg of a route asks of the driver.
enum class Move : std::uint8_t {
    Run,   // sprint (L2 held) at full deflection to the point; done within kArrived of it
    Climb, // sprint to within kClimbSlowWithin of the point beyond a face, then walk at kClimbStick, tapping triangle
           // near it; done once a climb has ended
    Jump, // sprint (L2 held) along the leg's line and tap triangle from the point's x on; done once the jump has landed
    Charge, // sprint (L2 held) along the leg's line and tap cross from the point's x on; done once the charge has ended
};

// One leg of a route: what to do and its point on the roofs (x, y; game axes, metres).
struct Leg {
    Move move = Move::Run;
    float x = 0.0F;
    float y = 0.0F;
};

// How close to a Run leg's point counts as there, m.
constexpr float kArrived = 1.0F;
// The stick's deflection on the way to a climb: below the jump's 0.95 (docs/research/characters.md#jump), so a tap
// that finds no climbable face never jumps, and above the climb's 0.12.
constexpr float kClimbStick = 0.9F;
// How far from a Climb leg's point triangle starts being tapped, m: the point lies 1.5-2 m beyond the face, and a
// standing climb takes a face within about 1.2 m (docs/research/characters.md#climb).
constexpr float kClimbTapWithin = 4.0F;
// How far from a Climb leg's point the driver stops sprinting and goes on at kClimbStick, m: beyond the tap distance,
// so the walk has slowed him before triangle is tapped.
constexpr float kClimbSlowWithin = 6.0F;
// Frames between button taps while a climb, a jump or a charge is wanted.
constexpr int kTapEvery = 6;
// How far past a Jump or Charge leg's take-off x the driver aims along the leg's line, m.
constexpr float kJumpLead = 20.0F;
// The attack record's bit a run attack or a charge holds (`+0x08` & 0x1000000, docs/research/combat.md#run-attacks):
// with L2 held it is the charge, so a Charge leg's charge has ended once it clears.
constexpr std::uint32_t kChargeRecordBit = 0x1000000;

// A pad driven by what player 1 is doing: it follows `route` leg by leg, turning each wanted direction into the left
// stick by the shown camera's heading (the stick moves the player relative to the camera,
// docs/research/characters.md#input).
class RouteDriver final : public coney::InputSource {
  public:
    // `player` gives player 1 once the level plays (null before).
    RouteDriver(std::span<const Leg> route, std::function<const coney::human::Player*()> player)
        : m_route(route), m_player(std::move(player)) {}

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        const coney::human::Player* player = m_player();
        if (player == nullptr || done()) {
            return samples;
        }
        ++m_frame;
        const coney::human::Human& human = player->human();
        advance(human);
        if (done()) {
            return samples;
        }
        const Leg& leg = m_route[m_leg];
        const coney::anim::Vec3 feet = human.position();
        // Where to steer: the leg's point, or for a jump or a charge a point well past the take-off on the leg's line.
        const float aimX = leg.move == Move::Jump || leg.move == Move::Charge ? leg.x + kJumpLead : leg.x;
        const float dx = aimX - feet.x;
        const float dy = leg.y - feet.y;
        const float reach = std::hypot(leg.x - feet.x, leg.y - feet.y);
        // The chase is a race: the Hi-Hat pursuers run at up to 13 m/s and fight whoever they catch, and the crew
        // does not turn back to help. The dispatcher refusing a disabled command is confirmed (code)
        // (docs/research/ai-gangs.md#warrior-auto-commands); that the chapter's Setup turns the commands off, and the
        // pursuers' speed, are inferred (docs/research/scripting.md#level3, docs/research/ai-scripting.md#devil-run).
        // So he sprints wherever he is not about to climb; Snow is tireless in the chase, so the sprint never runs
        // out (docs/research/characters.md#sprint).
        const bool slowing = leg.move == Move::Climb && reach < kClimbSlowWithin;
        steer(pad, *player, dx, dy, slowing ? kClimbStick : 1.0F);

        // Buttons: L2 held while he sprints and for a jump's or a charge's run-up; triangle tapped near a climb's face
        // or past a jump's take-off; cross tapped past a charge's (L2 held, then cross pressed: the charge,
        // combat.md#run-attacks).
        const bool tapFrame = m_frame % kTapEvery == 0;
        const bool onGround = human.traversal() == Traversal::None && !human.airborne();
        switch (leg.move) {
        case Move::Run:
            pad.buttons |= coney::pad::kL2;
            break;
        case Move::Climb:
            if (!slowing) {
                pad.buttons |= coney::pad::kL2;
            }
            if (tapFrame && onGround && reach < kClimbTapWithin) {
                pad.buttons |= coney::pad::kTriangle;
            }
            break;
        case Move::Jump:
            pad.buttons |= coney::pad::kL2;
            // The route runs east, so "past the take-off" is feet.x >= leg.x.
            if (onGround && feet.x >= leg.x && !m_sawMove) {
                pad.buttons |= coney::pad::kTriangle;
            }
            break;
        case Move::Charge:
            pad.buttons |= coney::pad::kL2;
            if (tapFrame && onGround && feet.x >= leg.x && !m_sawMove) {
                pad.buttons |= coney::pad::kCross;
            }
            break;
        }
        return samples;
    }

    // Whether every leg is done.
    [[nodiscard]] bool done() const { return m_leg >= m_route.size(); }
    // The legs done.
    [[nodiscard]] std::size_t legsDone() const { return m_leg; }
    // Climbs, jumps and charges made.
    [[nodiscard]] int climbs() const { return m_climbs; }
    [[nodiscard]] int jumps() const { return m_jumps; }
    [[nodiscard]] int charges() const { return m_charges; }

  private:
    // Moves on past the legs `human` has finished: a run once he is at its point, a climb or a jump once one began
    // and he is back on his feet, a charge once one began and ended.
    void advance(const coney::human::Human& human) {
        while (!done()) {
            const Leg& leg = m_route[m_leg];
            const Traversal traversal = human.traversal();
            bool finished = false;
            switch (leg.move) {
            case Move::Run:
                finished = std::hypot(leg.x - human.position().x, leg.y - human.position().y) < kArrived;
                break;
            case Move::Climb:
                m_sawMove = m_sawMove || traversal == Traversal::Climbing;
                finished = m_sawMove && traversal == Traversal::None;
                m_climbs += finished ? 1 : 0;
                break;
            case Move::Jump:
                m_sawMove = m_sawMove || traversal == Traversal::Jumping;
                finished = m_sawMove && (traversal == Traversal::None || traversal == Traversal::RunStop);
                m_jumps += finished ? 1 : 0;
                break;
            case Move::Charge: {
                const bool charging = (human.gateInput().flags & kChargeRecordBit) != 0;
                m_sawMove = m_sawMove || charging;
                finished = m_sawMove && !charging;
                m_charges += finished ? 1 : 0;
                break;
            }
            }
            if (!finished) {
                return;
            }
            ++m_leg;
            m_sawMove = false;
        }
    }

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

    // The raw byte for `value` in [-1, 1] as an input script writes it: each axis has its own dead zone round the
    // centre (core/pad.h), so the value is laid out past it, as the pad reads it back (pad::stickValue).
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }

    std::span<const Leg> m_route;
    std::function<const coney::human::Player*()> m_player;
    std::size_t m_leg = 0;
    bool m_sawMove = false;
    int m_climbs = 0;
    int m_jumps = 0;
    int m_charges = 0;
    std::uint64_t m_frame = 0;
};

// The rooftop chase's route from vbStartRail to vbEnterGallery (docs/missions/level3.md, checkpoint 4), east along the
// roofs at y 361-369: the climbs (short walls, walls and two fences) and the sprint jumps over the gaps, each
// `vbJumpWarn` box's. Past the last-but-one jump the roof at z 10.3 is closed by a 3.6 m wall that nothing climbs
// (x 167.4, y 361.5-366.7); the way on is the steel stairs beside it (y 367-369.5), up to the platform at z 16.5 and
// the jump to the gallery's roof. At x 85.2 a row of three `dyn_door_fence` barriers (numbers 4, 6 and 10, 10
// hitpoints) closes the roof from post to post: two-sided, they stop him from either side until one breaks, and a
// charge (16) breaks one in a hit (docs/research/objects.md#barriers), so he charges through the middle of number 10
// (y 363.7-366.2; number 6 is beside it at y 366.2-368.6). The run leg before the charge lines him up on y 365, because
// the charge breaks only the barrier it meets: coming in at y 366.4 it broke number 6, and he then pushed against
// number 10 while the pursuers caught up.
constexpr std::array kChaseRoute{
    Leg{Move::Run, -212.0F, 363.7F},  Leg{Move::Climb, -205.5F, 363.5F}, Leg{Move::Climb, -200.5F, 363.3F},
    Leg{Move::Run, -163.0F, 363.4F},  Leg{Move::Climb, -157.5F, 362.8F}, Leg{Move::Run, -136.0F, 361.8F},
    Leg{Move::Jump, -131.0F, 361.7F}, Leg{Move::Run, -112.0F, 363.8F},   Leg{Move::Climb, -106.5F, 364.1F},
    Leg{Move::Run, -100.0F, 364.5F},  Leg{Move::Climb, -94.5F, 364.7F},  Leg{Move::Run, -88.5F, 364.8F},
    Leg{Move::Climb, -83.0F, 364.9F}, Leg{Move::Run, -79.5F, 364.9F},    Leg{Move::Climb, -74.5F, 364.9F},
    Leg{Move::Jump, -64.5F, 364.9F},  Leg{Move::Run, 15.0F, 364.8F},     Leg{Move::Jump, 18.5F, 364.8F},
    Leg{Move::Run, 50.0F, 365.1F},    Leg{Move::Run, 66.0F, 367.4F},     Leg{Move::Climb, 74.5F, 367.5F},
    Leg{Move::Run, 79.0F, 365.0F},    Leg{Move::Charge, 81.5F, 365.0F},  Leg{Move::Run, 92.0F, 365.0F},
    Leg{Move::Climb, 96.0F, 365.0F},  Leg{Move::Jump, 100.5F, 365.0F},   Leg{Move::Run, 134.0F, 364.7F},
    Leg{Move::Climb, 139.5F, 364.8F}, Leg{Move::Jump, 149.5F, 364.8F},   Leg{Move::Run, 164.5F, 368.2F},
    Leg{Move::Run, 178.0F, 368.2F},   Leg{Move::Jump, 191.5F, 368.2F},   Leg{Move::Run, 206.0F, 368.2F},
};

// How long the chase may take before it counts as stuck: 3 minutes of frames.
constexpr std::uint64_t kChaseLimit = 5400;

} // namespace

TEST_CASE("the disc's level3 rooftop chase, driven by the pad, reaches the gallery", "[disc][story][playthrough]") {
    // docs/research/scripting.md#level3: the AI walks Snow to vbStartRail, the stick then runs him east along the
    // roofs, and vbEnterGallery hands over to the gallery chapter (SetCheckPoint(5)). A fall fails the mission.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession* session = nullptr;
    RouteDriver driver(kChaseRoute, [&session]() -> const coney::human::Player* {
        coney::platform::PlayLevelMode* play = session != nullptr ? session->session().play() : nullptr;
        return play != nullptr ? &play->player() : nullptr;
    });
    coney::test::DiscSession run(*wad, driver);
    session = &run;
    run.session().startAtLevel("level3", 4);
    REQUIRE(run.runUntilPlay());

    const auto failed = [&run] {
        return std::ranges::any_of(run.log(),
                                   [](const std::string& line) { return line.starts_with("mission failed"); });
    };
    const auto& state = run.session().flow().state();
    // The chapter's Setup turns player 1's Warrior commands 0-5 off (docs/research/scripting.md#level3), so through
    // the chase the chief's automatic attack is refused and the crew keeps running ahead
    // (docs/research/ai-gangs.md#warrior-auto-commands): the race the driver has to win.
    bool commandsOn = false;
    run.runUntil(
        [&] {
            const auto& commands = state.characters.warriorCommands.at(0);
            // Until the gallery's chapter (checkpoint 5), which sets its own.
            commandsOn = commandsOn || (state.checkPoint < 5 &&
                                        std::ranges::any_of(commands.begin(), commands.begin() + 6, std::identity{}));
            return state.checkPoint >= 5 || failed();
        },
        kChaseLimit);

    const coney::platform::PlayLevelMode* play = run.session().play();
    const std::uint64_t errors = run.session().flow().scripts().errors();
    std::printf("  level3 checkpoint 4 chase: checkpoint %.0f after %llu frames of play, legs %zu of %zu, climbs %d, "
                "jumps %d, charges %d, %llu script errors, %s\n",
                state.checkPoint, static_cast<unsigned long long>(run.playFrames()), driver.legsDone(),
                kChaseRoute.size(), driver.climbs(), driver.jumps(), driver.charges(),
                static_cast<unsigned long long>(errors), failed() ? "failed" : "not failed");
    if (play != nullptr) {
        const coney::anim::Vec3 feet = play->player().human().position();
        std::printf("  player 1 at (%.1f, %.1f, %.1f), health %d\n", static_cast<double>(feet.x),
                    static_cast<double>(feet.y), static_cast<double>(feet.z), play->player().human().health().value());
    }
    CHECK(errors == 0);
    CHECK_FALSE(commandsOn);
    CHECK_FALSE(failed());
    CHECK(state.checkPoint == 5);
    REQUIRE(play != nullptr);
    CHECK(play->player().human().health().value() > 0);
}
