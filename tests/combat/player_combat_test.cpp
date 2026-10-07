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
// 0x1000000 to their end. The values are the phases measured at runtime (docs/research/combat.md#attacks), S1's for
// the attacks not listed.
class ClipPhases {
  public:
    // An attack `animId` started this update.
    void start(int animId) {
        static constexpr std::array<Timing, 6> kTimings{{{anim_id::kAttackX1, 10, 20, 21, 30},
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

TEST_CASE("R1 held blocks and reads nothing else; cross under it still attacks", "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("200 press r1\n205 tap cross\n210 stick left 80 0\n230 stick left 0 0\n"
                                   "230 release r1\n",
                                   235);
    for (std::size_t frame = 200; frame < 230; ++frame) {
        CHECK(frames[frame].out.blocking);
    }
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
    const auto frames = runner.run("10 tap square\n16 tap square\n40 tap cross\n", 80, set);
    // The second square, in 34's window, finds no chain attack after it.
    CHECK(starts(frames) == Starts{{10, 34}, {41, 36}});
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
