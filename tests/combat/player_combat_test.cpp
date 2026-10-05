// SPDX-License-Identifier: GPL-3.0-or-later
#include "combat/player_combat.h"

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
            if (context) {
                context(frame, input);
            }
            out.push_back(Frame{input.command, m_combat.update(input, m_tuning)});
        }
        return out;
    }

    PlayerCombat& combat() { return m_combat; }

  private:
    CombatTuning m_tuning;
    CommandTables m_tables = CommandTables::street();
    CommandMatcher m_matcher;
    PlayerCombat m_combat;
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

TEST_CASE("a press every 6 updates plays S1, SS2 and SSS3, each hitting 2 updates later", "[combat]") {
    const AnimRangeList ranges = researchedRanges();
    Runner runner(&ranges);
    const auto frames = runner.run("10 tap square\n16 tap square\n22 tap square\n", 60);
    CHECK(starts(frames) == Starts{{10, anim_id::kAttackS1}, {16, anim_id::kAttackSS2}, {22, anim_id::kAttackSSS3}});
    CHECK(hits(frames) == Starts{{12, 17}, {18, 36}, {24, 53}});
    CHECK_FALSE(runner.combat().chain().active());
}

TEST_CASE("X1 starts on cross's release and a square 5 updates later chains XS2", "[combat]") {
    const AnimRangeList ranges = researchedRanges();
    Runner runner(&ranges);
    const auto frames = runner.run("40 tap cross\n46 tap square\n", 80);
    CHECK(starts(frames) == Starts{{41, anim_id::kAttackX1}, {47, anim_id::kAttackXS2}});
    CHECK(hits(frames) == Starts{{43, 26}, {49, 44}});
}

TEST_CASE("a square in recovery is dropped and one after the attack starts a new S1", "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("0 tap square\n19 tap square\n24 tap square\n", 40);
    CHECK(starts(frames) == Starts{{0, anim_id::kAttackS1}, {24, anim_id::kAttackS1}});
    CHECK(hits(frames) == Starts{{2, 0}, {26, 0}}); // no range list: no damage
}

TEST_CASE("a snap: the full stick 90 degrees off the facing, then square", "[combat]") {
    Runner runner(nullptr);
    const auto frames = runner.run("70 stick left 100 0\n71 tap square\n72 stick left 0 0\n", 80);
    CHECK(starts(frames) == Starts{{71, anim_id::kSnapRight}});
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
    Runner runner(nullptr);
    runner.combat().rage().set(78);
    const auto frames = runner.run("320 press l1 r1\n322 release l1 r1\n", 330);
    CHECK(frames[320].command == command::kL1R1);
    CHECK(frames[320].out.rageStarted);
    CHECK(frames[320].out.startAnim == anim_id::kRageStart);
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
    // The combination does nothing at a walk; the cross release's 0x10 that follows starts X1 as any cross tap does.
    CHECK(walked[5].command == command::kL2Cross);
    CHECK(starts(walked) == Starts{{6, anim_id::kAttackX1}});
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
    CHECK(frames[142].out.hitDamage == 57);
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
    CHECK(frames[200].out.startAnim == anim_id::kMountingStrike);
    CHECK(runner.combat().mode() == CombatMode::Tackling);
    runner.combat().release();
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
