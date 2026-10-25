// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/player_combat.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "combat/combat_script.h"
#include "support/fixtures.h"

using namespace coney::combat;
using coney::human::Gait;

namespace {

// A range list giving the chain attacks the damage the research measured on a civilian (combat.md#damage-table) and
// every other id 10.
AnimRangeList researchedRanges() {
    const std::map<int, std::int16_t> damage{{11, 26}, {12, 17}, {13, 53}, {14, 44}, {15, 44}, {16, 36},
                                             {17, 61}, {19, 53}, {51, 57}, {53, 57}, {55, 57}, {147, 66}};
    coney::test::Bytes bytes;
    bytes.u32(722);
    for (int id = 0; id < 722; ++id) {
        const auto found = damage.find(id);
        const std::int16_t value = found != damage.end() ? found->second : std::int16_t{10};
        bytes.u16(0).u16(1000).u32(std::bit_cast<std::uint32_t>(1.0F)).u16(0);
        bytes.u16(static_cast<std::uint16_t>(value)).u16(10).u16(0);
    }
    auto list = AnimRangeList::parse(bytes.span());
    REQUIRE(list.has_value());
    return std::move(list).value_or(AnimRangeList{});
}

// One update's output, with the command that made it.
struct Frame {
    CommandId command = command::kNone;
    CombatOutput out;
};

// What an attack's clip would hold on the record's +0x08 as it plays, standing in for the animator the dispatcher runs
// beside in the game (docs/research/tasks.md#held-flags): the wind-up, the chain window from `open`, the end phase from
// `close`, the recovery from `recovery`, nothing from `end`, in updates from the clip's start; the moving attacks hold
// 0x1000000 to their end. The values are the phases measured at runtime (docs/research/combat/index.md#attacks; the
// bat's 34 and 36, which have no chain window, docs/research/combat-moves.md#bat-cross), S1's for the attacks not
// listed.
class ClipPhases {
  public:
    // An attack `animId` started this update.
    void start(int animId) {
        static constexpr std::array<Timing, 8> kTimings{{{anim_id::kAttackX1, 10, 20, 21, 30},
                                                         {34, 13, 13, 25, 37},
                                                         {36, 20, 20, 23, 35},
                                                         {anim_id::kAttackSSS3, 16, 16, 22, 26},
                                                         {anim_id::kAttackXS2, 22, 22, 26, 30},
                                                         {anim_id::kRunningAttackCharge, 27, 27, 27, 27},
                                                         {anim_id::kRunningAttackDive, 60, 60, 60, 60},
                                                         {anim_id::kAttackFromRun, 21, 21, 21, 21}}};
        const auto found = std::ranges::find(kTimings, animId, &Timing::id);
        m_timing = found != kTimings.end() ? *found : Timing{animId, 6, 15, 16, 20};
        m_moving = animId == anim_id::kRunningAttackCharge || animId == anim_id::kRunningAttackDive ||
                   animId == anim_id::kAttackFromRun;
        m_age = 0;
        m_playing = true;
    }
    // The bits held at the start of the next update, which ages the clip by one.
    std::uint32_t next() {
        if (!m_playing) {
            return 0;
        }
        ++m_age;
        if (m_age >= m_timing.end) {
            m_playing = false;
            return 0;
        }
        if (m_moving) {
            return kPhaseRunAttack;
        }
        if (m_age < m_timing.open) {
            return kPhaseWindUp;
        }
        if (m_age < m_timing.close) {
            return kPhaseChainWindow;
        }
        return m_age < m_timing.recovery ? kPhaseEnd : kPhaseRecovery;
    }

  private:
    struct Timing {
        int id, open, close, recovery, end;
    };
    Timing m_timing{};
    int m_age = 0;
    bool m_playing = false;
    bool m_moving = false;
};

// Whether `animId`, started by the dispatcher, is an attack whose clip holds the attack's phases.
bool isAttack(int animId) {
    return animId != anim_id::kNone && animId != anim_id::kGrabPlayerIntro && animId != anim_id::kTacklePlayerIntro &&
           animId != anim_id::kGrabLetGo && animId != anim_id::kRageStart && animId != anim_id::kGrabSpinToRear &&
           animId != anim_id::kGrabSpinToFront;
}

// Plays an input script through the street's command tables into a PlayerCombat; `context` fills in what the world
// would say on each frame (gait, target, reach). Both sticks are the pad's: the camera is behind the player.
class Runner {
  public:
    explicit Runner(const AnimRangeList* ranges) : m_combat(ranges, 0, 99) {}

    // Runs `frames` frames of `script`.
    std::vector<Frame> run(std::string_view script, std::uint64_t frames,
                           const std::function<void(std::uint64_t, CombatInput&)>& context = {}) {
        std::vector<Frame> out;
        for (const coney::test::PadFrame& pad : coney::test::playScript(script, frames)) {
            const std::uint64_t frame = out.size();
            CombatInput input;
            input.command = m_matcher.update(pad.buttons, m_tables, m_tuning.historyHoldSamples);
            input.buttons = pad.buttons;
            input.stick = Stick{pad.leftX, pad.leftY};
            input.padStick = input.stick;
            input.nowMs = frame * 1000 / 30;
            input.phase = m_clip.next();
            if (context) {
                context(frame, input);
            }
            out.push_back(Frame{input.command, m_combat.update(input, m_tuning)});
            if (isAttack(out.back().out.startAnim)) {
                m_clip.start(out.back().out.startAnim);
            }
        }
        return out;
    }

    PlayerCombat& combat() { return m_combat; }

  private:
    CombatTuning m_tuning;
    CommandTables m_tables = CommandTables::street();
    CommandMatcher m_matcher;
    PlayerCombat m_combat;
    ClipPhases m_clip;
};

// The frames on which a clip started, in order.
std::vector<std::pair<std::size_t, int>> starts(const std::vector<Frame>& frames) {
    std::vector<std::pair<std::size_t, int>> out;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].out.startAnim != anim_id::kNone) {
            out.emplace_back(i, frames[i].out.startAnim);
        }
    }
    return out;
}

// The frames on which a hit landed, with its damage.
std::vector<std::pair<std::size_t, int>> hits(const std::vector<Frame>& frames) {
    std::vector<std::pair<std::size_t, int>> out;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (frames[i].out.hitAnim != anim_id::kNone) {
            out.emplace_back(i, frames[i].out.hitDamage);
        }
    }
    return out;
}

using Starts = std::vector<std::pair<std::size_t, int>>;

} // namespace

TEST_CASE("a press every 6 updates plays S1, SS2 and SSS3, hitting 2, 4 and 7 updates after each start", "[combat]") {
    const AnimRangeList ranges = researchedRanges();
    Runner runner(&ranges);
    const auto frames = runner.run("10 tap square\n16 tap square\n22 tap square\n", 60);
    CHECK(starts(frames) == Starts{{10, anim_id::kAttackS1}, {16, anim_id::kAttackSS2}, {22, anim_id::kAttackSSS3}});
    CHECK(hits(frames) == Starts{{12, 17}, {20, 36}, {29, 53}});
    CHECK_FALSE(runner.combat().chain().active());
}

TEST_CASE("X1 starts on cross's release, hits at 8, and a square 5 updates later chains XS2 at its window",
          "[combat]") {
    const AnimRangeList ranges = researchedRanges();
    Runner runner(&ranges);
    const auto frames = runner.run("40 tap cross\n46 tap square\n", 80);
    // The square is buffered in X1's wind-up and plays when the window opens, 10 updates in.
    CHECK(starts(frames) == Starts{{41, anim_id::kAttackX1}, {51, anim_id::kAttackXS2}});
    CHECK(hits(frames) == Starts{{49, 26}, {60, 44}});
}

TEST_CASE("a square in recovery is dropped and one after the attack starts a new S1", "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("0 tap square\n19 tap square\n24 tap square\n", 40);
    CHECK(starts(frames) == Starts{{0, anim_id::kAttackS1}, {24, anim_id::kAttackS1}});
    CHECK(hits(frames) == Starts{{2, 0}, {26, 0}}); // no range list: no damage
}

TEST_CASE("a snap: the full stick 90 degrees off the facing, then square, with a human there", "[combat]") {
    Runner runner(nullptr);
    const auto found = [](std::uint64_t, CombatInput& input) { input.snapTarget = true; };
    const auto frames = runner.run("70 stick left 100 0\n71 tap square\n72 stick left 0 0\n", 80, found);
    CHECK(starts(frames) == Starts{{71, anim_id::kSnapRight}});
    // With nobody found there (or only the current target) the same input is an S1.
    Runner alone(nullptr);
    const auto none = alone.run("70 stick left 100 0\n71 tap square\n72 stick left 0 0\n", 80);
    CHECK(starts(none) == Starts{{71, anim_id::kAttackS1}});
}

TEST_CASE("R1 held blocks and reads nothing else; cross under it still attacks, and the block cuts its end phase",
          "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("200 press r1\n205 tap cross\n210 stick left 80 0\n230 stick left 0 0\n"
                                   "230 release r1\n",
                                   235);
    for (std::size_t frame = 200; frame <= 206; ++frame) {
        CHECK(frames[frame].out.blocking);
    }
    // X1's wind-up and window refuse the block (Human_CanFight); its end phase, from update 20, lets it cut in.
    for (std::size_t frame = 207; frame < 226; ++frame) {
        CHECK_FALSE(frames[frame].out.blocking);
    }
    CHECK(frames[226].out.blocking);
    CHECK_FALSE(frames[231].out.blocking);
    // Cross pressed and released while blocking plays X1 on the release.
    CHECK(starts(frames) == Starts{{206, anim_id::kAttackX1}});
}

TEST_CASE("L1 + R1 with a full rage meter starts rage", "[combat]") {
    // Filled by gains, which hold the meter for 5 s.
    Runner runner(nullptr);
    const CombatTuning tuning;
    runner.combat().rage().add(25.0F, tuning, 0);
    runner.combat().rage().add(25.0F, tuning, 0);
    runner.combat().rage().add(5.0F, tuning, 0);
    REQUIRE(runner.combat().rage().full());
    const auto frames = runner.run("120 press l1 r1\n122 release l1 r1\n", 130);
    CHECK(frames[120].command == command::kL1R1);
    CHECK(frames[120].out.rageStarted);
    CHECK(frames[120].out.startAnim == anim_id::kRageStart);
    CHECK(runner.combat().rage().raging());

    // Short of full, nothing.
    Runner partial(nullptr);
    partial.combat().rage().set(70);
    const auto none = partial.run("10 press l1 r1\n", 12);
    CHECK_FALSE(none[10].out.rageStarted);
}

TEST_CASE("L2 held and cross pressed charges at a run, and does nothing at a walk", "[combat]") {
    const char* script = "250 stick left 0 100\n265 press l2\n275 tap cross\n300 release l2\n300 stick left 0 0\n";
    Runner running(nullptr);
    const auto charged = running.run(script, 305, [](std::uint64_t frame, CombatInput& input) {
        input.gait = frame >= 255 ? Gait::Run : Gait::Standing;
    });
    CHECK(starts(charged) == Starts{{275, anim_id::kRunningAttackCharge}});

    Runner walking(nullptr);
    const auto walked = walking.run("0 stick left 0 50\n2 press l2\n5 tap cross\n", 10,
                                    [](std::uint64_t, CombatInput& input) { input.gait = Gait::Walk; });
    // The combination does nothing at a walk; the cross release's 0x10 that follows starts the walk attack, as any
    // cross tap at a walk does.
    CHECK(walked[5].command == command::kL2Cross);
    CHECK(starts(walked) == Starts{{6, anim_id::kAttackFromWalk}});
}

TEST_CASE("a grab, a strike and a forward throw, with the power meter paying for both", "[combat]") {
    const AnimRangeList ranges = researchedRanges();
    Runner runner(&ranges);
    const auto frames =
        runner.run("90 tap circle\n140 tap square\n180 stick left 0 100\n181 tap circle\n182 stick left 0 0\n", 200,
                   [](std::uint64_t, CombatInput& input) { input.grabTargetInReach = true; });
    // The tap's release (0xd) grabs.
    CHECK(frames[91].command == command::kCircleTapped);
    CHECK(frames[91].out.grabStarted);
    CHECK(frames[91].out.startAnim == anim_id::kGrabPlayerIntro);
    // Square strikes (51 or 53) and hits for 57; then circle with the stick ahead throws (147).
    CHECK(frames[140].out.grabAction == GrabAction::Strike);
    CHECK(frames[141].out.hitDamage == 57); // a grab strike hits 1 update after its start
    CHECK(frames[181].out.grabAction == GrabAction::Throw);
    CHECK(frames[181].out.startAnim == anim_id::kThrow1Front);
    CHECK(frames[183].out.hitDamage == 66);
    CHECK(runner.combat().mode() == CombatMode::Free);
    // 400, less 40 and 100, less the drain while holding (15 a second over 90 updates), plus the refill since.
    const int drained = 400 - 40 - 100 - 45;
    CHECK(runner.combat().power().value() >= drained - 2);
    CHECK(runner.combat().power().value() <= drained + (2 * 19) + 2);
}

TEST_CASE("a rear hold's throw lands its hit the spin's updates later than a front hold's", "[combat]") {
    // From a rear hold the spin to the front (80) plays before the throw's clip
    // (docs/research/combat-throws.md#throw-direction, item 4), so the hit comes 2 updates after the throw's own start,
    // here 17 updates after the press.
    const AnimRangeList ranges = researchedRanges();
    Runner runner(&ranges);
    const auto frames = runner.run("90 tap circle\n180 stick left 0 100\n181 tap circle\n182 stick left 0 0\n", 215,
                                   [](std::uint64_t frame, CombatInput& input) {
                                       input.grabTargetInReach = true;
                                       input.fromRear = frame > 120;
                                       input.rearThrowSpinUpdates = frame > 120 ? 17 : 0;
                                       // The spin and the throw hold the attack's bits until both are over.
                                       if (frame > 181 && frame < 210) {
                                           input.phase = kPhaseWindUp;
                                       }
                                   });
    CHECK(frames[181].out.grabAction == GrabAction::Throw);
    CHECK(frames[181].out.startAnim == anim_id::kThrow1Front);
    CHECK(hits(frames) == Starts{{200, 66}});
}

TEST_CASE("circle held 7 samples tackles; mounted, square strikes", "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("185 press circle\n195 release circle\n200 tap square\n", 210,
                                   [](std::uint64_t, CombatInput& input) { input.grabTargetInReach = true; });
    CHECK(frames[191].command == command::kCircleHeld);
    CHECK(frames[191].out.tackleStarted);
    CHECK(frames[191].out.startAnim == anim_id::kTacklePlayerIntro);
    CHECK_FALSE(frames[195].out.grabStarted); // the long press's release is no tap
    CHECK((frames[200].out.startAnim == anim_id::kMountStrike1 || frames[200].out.startAnim == anim_id::kMountStrike2));
    CHECK(runner.combat().mode() == CombatMode::Tackling);
    runner.combat().release();
    CHECK(runner.combat().mode() == CombatMode::Free);
}

TEST_CASE("circle in a front grab mounts; mounted, square, cross, circle back to the hold and L2 off", "[combat]") {
    Runner runner(nullptr);
    // Grab, mount, square, cross; circle back to the hold; mount again and get off with L2.
    const auto frames = runner.run("1 tap circle\n40 tap circle\n80 tap square\n120 tap cross\n160 tap circle\n"
                                   "200 tap circle\n240 press l2\n242 release l2\n",
                                   260, [](std::uint64_t, CombatInput& input) { input.grabTargetInReach = true; });
    REQUIRE(frames[2].out.grabStarted);
    CHECK(frames[40].out.grabAction == GrabAction::Mount);
    CHECK(frames[40].out.startAnim == anim_id::kGrabMount);
    CHECK((frames[80].out.startAnim == anim_id::kMountStrike1 || frames[80].out.startAnim == anim_id::kMountStrike2));
    CHECK(frames[80].out.mountAction == MountAction::Strike);
    // Cross strikes on its release (0x10).
    bool crossStrike = false;
    for (std::size_t f = 120; f < 125; ++f) {
        crossStrike = crossStrike || frames[f].out.startAnim == anim_id::kMountStrike3;
    }
    CHECK(crossStrike);
    CHECK(frames[160].out.mountAction == MountAction::ToHold);
    CHECK(frames[160].out.startAnim == anim_id::kMountPickup);
    CHECK(frames[200].out.grabAction == GrabAction::Mount);
    bool off = false;
    for (std::size_t f = 240; f < 245; ++f) {
        off = off || frames[f].out.mountAction == MountAction::GetOff;
    }
    CHECK(off);
    CHECK(runner.combat().mode() == CombatMode::Free);
}

TEST_CASE("circle with nobody in reach plays the grab and misses; during an attack it is refused", "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("5 tap circle\n30 tap square\n32 tap circle\n", 40);
    CHECK(frames[6].out.grabMissed);
    CHECK(frames[6].out.startAnim == anim_id::kGrabPlayerIntro);
    CHECK(runner.combat().mode() == CombatMode::Free);
    CHECK(frames[33].command == command::kCircleTapped);
    CHECK(frames[33].out.startAnim == anim_id::kNone);
}

TEST_CASE("triangle in a grab mugs; the stick held on the target finishes it and the hold returns", "[combat]") {
    Runner runner(nullptr);
    std::uint64_t succeeded = 0;
    // Grab at 1, triangle at 10; then steer the stick (0.8, a partial deflection) to the mugging's target each update.
    auto context = [&runner](std::uint64_t, CombatInput& input) {
        input.grabTargetInReach = true;
        input.victimMuggable = true;
        if (const auto& mugging = runner.combat().mugging()) {
            const float radians = mugging->targetDegrees() * std::numbers::pi_v<float> / 180.0F;
            input.padStick = Stick{0.8F * std::sin(radians), 0.8F * std::cos(radians)};
        }
    };
    const auto frames = runner.run("0 tap circle\n10 tap triangle\n", 200, context);
    CHECK(frames[10].out.grabAction == GrabAction::Mug);
    for (std::size_t frame = 11; frame < frames.size(); ++frame) {
        if (frames[frame].out.game == GameResult::Succeeded) {
            succeeded = frame;
            break;
        }
    }
    CHECK(succeeded == 160); // 5000 ms after the mugging began
    CHECK(runner.combat().mode() == CombatMode::Grabbing);
}

TEST_CASE("an R1 press fails the stereo theft and plays its fail clip", "[combat]") {
    Runner runner(nullptr);
    runner.combat().startTheft(TheftKind::Rotate, 0, stereoStageTurns(2));
    const auto frames = runner.run("0 stick left 100 0\n1 stick left 87 50\n2 stick left 50 87\n3 stick left 0 100\n"
                                   "4 tap r1\n",
                                   6);
    CHECK(frames[3].out.game == GameResult::Running);
    CHECK(runner.combat().theft().has_value() == false);
    CHECK(frames[4].out.game == GameResult::Failed);
    CHECK(frames[4].out.startAnim == anim_id::kStereoStealFail);
    CHECK(runner.combat().mode() == CombatMode::Free);
}

TEST_CASE("cross held and square pressed outside a hold is the special 653, spending a quarter of the power",
          "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("10 press cross\n12 tap square\n14 release cross\n", 20);
    CHECK(frames[12].command == command::kCrossSquare);
    CHECK(frames[12].out.startAnim == anim_id::kSpecial);
    // 100 of 400 spent (and refilling since).
    CHECK(runner.combat().power().value() <= 320);
    CHECK(runner.combat().power().value() >= 300);

    // Short of a quarter of the meter, nothing plays.
    Runner weak(nullptr);
    weak.combat().power().set(40);
    const auto none = weak.run("10 press cross\n12 tap square\n14 release cross\n", 20);
    CHECK(none[12].out.startAnim == anim_id::kNone);
}

TEST_CASE("circle + cross outside a hold is the strong grapple: a grab connecting with 657, at no power cost",
          "[combat]") {
    Runner runner(nullptr);
    const auto inReach = [](std::uint64_t /*frame*/, CombatInput& input) { input.grabTargetInReach = true; };
    const auto frames = runner.run("10 press circle cross\n14 release circle cross\n", 20, inReach);
    CHECK(frames[10].command == command::kCircleCross);
    CHECK(frames[10].out.grabStarted);
    CHECK(frames[10].out.startAnim == anim_id::kGrabPlayerIntro);
    CHECK(frames[10].out.grappleAnim == anim_id::kStrongGrapple);
    CHECK(runner.combat().mode() == CombatMode::Grabbing);
    // No quarter of the meter spent: the hold's drain only.
    CHECK(runner.combat().power().value() > 300);

    // With nobody the search may grab, nothing plays.
    Runner alone(nullptr);
    const auto none = alone.run("10 press circle cross\n14 release circle cross\n", 20);
    CHECK(none[10].command == command::kCircleCross);
    CHECK_FALSE(none[10].out.grabStarted);
    CHECK(none[10].out.startAnim == anim_id::kNone);
    CHECK(alone.combat().mode() == CombatMode::Free);
}

TEST_CASE("a bat's anim set plays 34 for square and 36 for cross, with no chain after them", "[combat]") {
    Runner runner(nullptr);
    const auto set = [](std::uint64_t, CombatInput& input) { input.animSet = 3; };
    const auto frames = runner.run("10 tap square\n16 tap square\n50 tap cross\n", 90, set);
    // The second square, in 34's window, finds no chain attack after it, and the cross waits for 34 to end.
    CHECK(starts(frames) == Starts{{10, 34}, {51, 36}});
    // At a grounded target: 37.
    Runner grounded(nullptr);
    const auto down = grounded.run("10 tap square\n", 20, [](std::uint64_t, CombatInput& input) {
        input.animSet = 3;
        input.target = TargetKind::Grounded;
    });
    CHECK(starts(down) == Starts{{10, 37}});
}

TEST_CASE("square at a run plays 24 only with the record's +0x08 clear", "[combat]") {
    // Square tests the run with +0x08 clear (0x00286cc8): a bit square does not refuse on, such as a start clip's
    // 0x10000000, sends it on to S1 (combat.md#attacks).
    const char* script = "0 stick left 0 100\n10 tap square\n";
    for (const auto& [phase, expected] :
         {std::pair{0U, anim_id::kAttackFromRun}, std::pair{0x10000000U, anim_id::kAttackS1}}) {
        INFO("+0x08 " << phase);
        Runner runner(nullptr);
        const auto frames = runner.run(script, 12, [phase](std::uint64_t frame, CombatInput& input) {
            input.gait = Gait::Run;
            if (frame == 10) {
                input.phase = phase;
            }
        });
        CHECK(starts(frames) == Starts{{10, expected}});
    }
}

TEST_CASE("with a brick in hand square, cross and the charge throw it; mid-throw a press is refused", "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("10 tap square\n", 20, [](std::uint64_t, CombatInput& input) { input.animSet = 5; });
    CHECK(starts(frames) == Starts{{10, anim_id::kOneHandedThrow}});
    // Cross (on its release) at a walk throws from the walk.
    Runner walking(nullptr);
    const auto walked = walking.run("0 stick left 0 50\n5 tap cross\n", 12, [](std::uint64_t, CombatInput& input) {
        input.animSet = 5;
        input.gait = Gait::Walk;
    });
    CHECK(starts(walked) == Starts{{6, anim_id::kOneHandedThrowFromWalk}});
    // L2 + cross at a run: the throw from a run, not the charge.
    Runner running(nullptr);
    const auto ran =
        running.run("0 stick left 0 100\n2 press l2\n5 tap cross\n", 8, [](std::uint64_t, CombatInput& input) {
            input.animSet = 4;
            input.gait = Gait::Run;
        });
    CHECK(!starts(ran).empty());
    CHECK(starts(ran).front() == std::pair<std::size_t, int>{5, anim_id::kBarrelThrowFromRun});
}

TEST_CASE("with a bat, a cross's 0x10 starts 36 only once 34 or 36 has ended: k37 after 34, k35 after 36", "[combat]") {
    // The move's whole length is lost, not only its end phase and recovery: the bat's 34 and 36 have no chain window
    // that keeps a press (docs/research/combat-moves.md#bat-cross). k counts updates from the move's first, and a tap
    // at update f has its 0x10 on f + 1.
    struct Case {
        const char* first;
        int firstClip;
        int free;
    };
    const auto set = [](std::uint64_t, CombatInput& input) { input.animSet = 3; };
    // Both moves start on update 10: square's press, cross's release.
    for (const Case& c : {Case{"10 tap square\n", 34, 37}, Case{"9 tap cross\n", 36, 35}}) {
        for (const int k : {3, 12, 24, c.free - 1, c.free, c.free + 1, c.free + 8}) {
            INFO("after " << c.firstClip << ", cross's 0x10 on k" << k);
            Runner runner(nullptr);
            const std::string script = std::string(c.first) + std::to_string(10 + k - 1) + " tap cross\n";
            const auto frames = runner.run(script, 120, set);
            const Starts expected = k >= c.free ? Starts{{10, c.firstClip}, {static_cast<std::size_t>(10 + k), 36}}
                                                : Starts{{10, c.firstClip}};
            CHECK(starts(frames) == expected);
        }
    }
}

TEST_CASE("with a bat, a second square in the swing is not kept: 34 chains into nothing, and cross waits for k37",
          "[combat]") {
    const auto set = [](std::uint64_t, CombatInput& input) { input.animSet = 3; };
    for (const int cross : {45, 46, 47}) {
        INFO("cross's 0x10 on update " << cross);
        Runner runner(nullptr);
        const auto frames =
            runner.run("10 tap square\n15 tap square\n" + std::to_string(cross - 1) + " tap cross\n", 100, set);
        // The 0x10 on update 46 is k36 (dropped); 47 is k37 (plays).
        CHECK(starts(frames) == (cross < 47 ? Starts{{10, 34}} : Starts{{10, 34}, {47, 36}}));
    }
}

TEST_CASE("with a bat, cross + square is no special: no 653, no power spent, and the cross's release swings 36",
          "[combat]") {
    // The original's Player_Special refuses a pad player holding an object (it throws a melee weapon first when the
    // throw is allowed, which Coney does not build); cross's own 0x10 then plays 36 on the release
    // (docs/research/combat-moves.md#bat-cross).
    for (const int set : {1, 2, 3, 4, 5, 6}) {
        INFO("anim set " << set);
        Runner runner(nullptr);
        const auto frames = runner.run("10 press cross\n11 tap square\n13 release cross\n", 30,
                                       [set](std::uint64_t, CombatInput& input) { input.animSet = set; });
        CHECK(frames[11].command == command::kCrossSquare);
        for (const Frame& frame : frames) {
            CHECK(frame.out.startAnim != anim_id::kSpecial);
            CHECK(frame.out.startAnim != anim_id::kSpecialRage);
        }
        CHECK(runner.combat().power().value() >= 390);
        // The melee sets swing on the release.
        if (set <= 3) {
            CHECK(starts(frames) == Starts{{13, set == 1 ? 47 : set == 2 ? 41 : 36}});
        }
    }
}

TEST_CASE("with a bat, circle + cross is no strong grapple, even with a target in reach", "[combat]") {
    Runner runner(nullptr);
    const auto frames =
        runner.run("10 press circle cross\n14 release circle cross\n", 20, [](std::uint64_t, CombatInput& input) {
            input.animSet = 3;
            input.grabTargetInReach = true;
        });
    CHECK(frames[10].command == command::kCircleCross);
    for (const Frame& frame : frames) {
        CHECK_FALSE(frame.out.grabStarted);
        CHECK(frame.out.grappleAnim == anim_id::kNone);
    }
    CHECK(runner.combat().mode() == CombatMode::Free);
}

TEST_CASE("square, cross, circle tapped and circle held each start the tandem at a target that allows one",
          "[combat][tandem]") {
    // The fighter found a victim held from the rear in front of the player (Tandem_CanStart true): every one of the
    // four commands starts the tandem, the set one of the three empty-handed ones, and no clip of the dispatcher's own.
    const auto tandem = [](std::uint64_t, CombatInput& input) { input.tandemTarget = true; };
    for (const std::string_view press :
         {"10 tap square\n", "10 tap cross\n", "10 tap circle\n", "10 press circle\n18 release circle\n"}) {
        Runner runner(nullptr);
        const auto frames = runner.run(press, 25, tandem);
        const auto started =
            std::ranges::find_if(frames, [](const Frame& f) { return f.out.tandemBase != anim_id::kNone; });
        REQUIRE(started != frames.end());
        CHECK((started->out.tandemBase == 163 || started->out.tandemBase == 169 || started->out.tandemBase == 175));
        CHECK(started->out.startAnim == anim_id::kNone);
        CHECK_FALSE(started->out.grabStarted);
        CHECK_FALSE(started->out.tackleStarted);
        CHECK_FALSE(started->out.grabMissed);
        CHECK(runner.combat().mode() == CombatMode::Free);
    }
}

TEST_CASE("circle held over several steps starts the tandem with command 0xe; the square + cross combination does not",
          "[combat][tandem]") {
    // The page's four triggers are square, cross (its 0x10), circle tapped (0xd) and circle held (0xe,
    // docs/research/combat-moves.md#tandem); the combination 0x22 (cross held, square pressed) is not one of them.
    const auto tandem = [](std::uint64_t, CombatInput& input) { input.tandemTarget = true; };
    {
        Runner runner(nullptr);
        // Circle held from step 10 to step 18: the history hold makes it 0xe, which starts the tandem.
        const auto frames = runner.run("10 press circle\n18 release circle\n", 25, tandem);
        const auto started =
            std::ranges::find_if(frames, [](const Frame& f) { return f.out.tandemBase != anim_id::kNone; });
        REQUIRE(started != frames.end());
        CHECK(started->command == command::kCircleHeld);
        CHECK(std::ranges::none_of(frames, [](const Frame& f) { return f.command == command::kCircleTapped; }));
    }
    {
        Runner runner(nullptr);
        const auto frames = runner.run("10 press cross\n12 tap square\n14 release cross\n", 20, tandem);
        const auto combo =
            std::ranges::find_if(frames, [](const Frame& f) { return f.command == command::kCrossSquare; });
        REQUIRE(combo != frames.end());
        CHECK(combo->out.tandemBase == anim_id::kNone);
    }
}

TEST_CASE("a tandem's set follows the weapon in hand: a bat 181, a baton 181, a knife 187", "[combat][tandem]") {
    for (const auto& [set, base] : std::array{std::pair{3, 181}, std::pair{2, 181}, std::pair{1, 187}}) {
        Runner runner(nullptr);
        const auto frames = runner.run("10 tap square\n", 20, [set = set](std::uint64_t, CombatInput& input) {
            input.tandemTarget = true;
            input.animSet = set;
        });
        CHECK(frames[10].out.tandemBase == base);
    }
}

TEST_CASE("the tandem comes after the moving attacks and is refused mid-attack", "[combat][tandem]") {
    // Walking at the victim with the stick at 60 %, square is the walk attack 23, as in the original's order.
    Runner walking(nullptr);
    const auto walk = walking.run("0 stick left 0 60\n10 tap square\n", 20, [](std::uint64_t, CombatInput& input) {
        input.tandemTarget = true;
        input.gait = Gait::Walk;
    });
    CHECK(walk[10].out.tandemBase == anim_id::kNone);
    CHECK(walk[10].out.startAnim == anim_id::kAttackFromWalk);
    // Without a victim to share, square is S1; in S1's wind-up a second square (now with one) starts nothing.
    Runner runner(nullptr);
    const auto frames = runner.run("10 tap square\n12 tap square\n", 20,
                                   [](std::uint64_t frame, CombatInput& input) { input.tandemTarget = frame >= 11; });
    CHECK(frames[10].out.startAnim == anim_id::kAttackS1);
    CHECK(frames[12].out.tandemBase == anim_id::kNone);
}

TEST_CASE("circle at a rear-held victim starts the tandem even with both grab blocks on", "[combat][tandem]") {
    // The tandem is tested before the grab in Player_GrabOrTackle's step 3; the grab-block flags refuse only the grab.
    Runner runner(nullptr);
    const auto frames = runner.run("10 tap circle\n", 20, [](std::uint64_t, CombatInput& input) {
        input.tandemTarget = true;
        input.grabBlocked = true;
    });
    const auto started =
        std::ranges::find_if(frames, [](const Frame& f) { return f.out.tandemBase != anim_id::kNone; });
    CHECK(started != frames.end());
}
