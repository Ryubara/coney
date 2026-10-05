// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "animation/anim_task.h"
#include "combat/anim_ids.h"
#include "combat/being_hit.h"
#include "combat/combat_tuning.h"
#include "combat/player_combat.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/human_animator.h"
#include "human/locomotion.h"
#include "human/locomotion_gate.h"
#include "human/target_human.h"
#include "human/victim.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

// When the player's moves take a press again and give the stick back, for every move, under button spam and with the
// stick held at partial deflections (docs/research/combat.md#input-return). The synthetic character plays each move's
// clip for as long as Rembrandt's clip on the disc lasts (S1 20 updates, X1 30, the grab miss 71 and 69, ...). Each
// test checks that the clip sequence plays fully under the spam, the update at which a press next starts a move, the
// update at which the stick moves the player again and the gait he resumes at, and that nothing of the move is left
// behind (gait, traversal, the fighter's state, the hold).

using coney::anim::Vec3;
using coney::human::Gait;
using coney::human::IncomingHit;
using coney::human::PairStage;
using coney::human::TargetState;
using coney::human::Traversal;
using coney::test::Fight;
using coney::test::FightCharacter;
namespace combat = coney::combat;
namespace id = combat::anim_id;

namespace {

// The synthetic clips have no range flag, so they play at 0.75: a clip of `updates` lasts updates / 40 seconds.
constexpr float kSecondsPerUpdate = 0.75F / 30.0F;

// The clips this file names.
constexpr int kIdle = 388;
constexpr int kWalkStart = 413;
constexpr int kWalk = 408;
constexpr int kRun = 410;
constexpr int kSprint = 411;
constexpr int kGrabMiss = 69;
constexpr int kGrabConnect = 72;
constexpr int kFrontHold = 82;
constexpr int kRearHold = 84;
constexpr int kTackleMiss = 2;
constexpr int kTackleHit = 5;
constexpr int kMounted = 210;
constexpr int kMugIntro = 338;
constexpr int kStunExit = 357;
constexpr int kRise = 199;
constexpr int kDuckCounter = 617;
constexpr auto kFightIdle = static_cast<int>(coney::human::kAnimFightIdle);
constexpr auto kNormalFromFight = static_cast<int>(coney::human::kAnimNormalFromFight);

// One clip of the timing character: anim `clip` lasting `updates` (Rembrandt's on the disc), carrying the body at
// `velocity` m/s by its root.
coney::test::LocomotionClip timed(std::uint32_t clip, float updates, float velocity = 0.0F) {
    return {.id = clip,
            .speed = 0.0F,
            .duration = updates * kSecondsPerUpdate,
            .rootVelocity = velocity,
            .rangeFlags = 0,
            .reach = 0.0F,
            .knockdown = false};
}

// A clip `clip` of `frames` frames at the disc's rate flags `rangeFlags`, with the phase events `markers` (frame,
// type): the chain attacks as Rembrandt's on the disc (flag 0x800, rate 0.8), so their phases fall where the clip's
// events put them.
coney::test::LocomotionClip evented(std::uint32_t clip, float frames, std::uint16_t rangeFlags,
                                    std::vector<std::array<std::uint16_t, 2>> markers) {
    return {.id = clip,
            .speed = 0.0F,
            .duration = frames / 30.0F,
            .rootVelocity = 0.0F,
            .rangeFlags = rangeFlags,
            .reach = 0.0F,
            .knockdown = false,
            .pairX = 0.0F,
            .markers = std::move(markers)};
}

// The phase events of Rembrandt's clips on the disc: the chain window (0x2c), the end (0x2d) and the recovery (0x48),
// at their frames (docs/research/tasks.md#held-flags).
constexpr std::uint16_t kWindow = 0x2c;
constexpr std::uint16_t kEnd = 0x2d;
constexpr std::uint16_t kRecovery = 0x48;
constexpr std::uint16_t kRate08 = 0x800;

// The fight's clips with the disc's lengths for the moves timed here, and the chain attacks' and the snaps' phase
// events. The charge carries the body at 7.45 m/s, as at runtime.
std::vector<coney::test::LocomotionClip> timingClips() {
    std::vector<coney::test::LocomotionClip> clips = coney::test::fightClips();
    const std::vector<coney::test::LocomotionClip> timedClips{
        timed(0, 26.67F, 7.45F),
        timed(1, 61.33F),
        timed(2, 58.67F),
        timed(4, 6.67F),
        timed(5, 45.33F),
        timed(6, 45.33F),
        timed(11, 30.0F),
        timed(12, 20.0F),
        timed(13, 30.0F),
        timed(14, 30.0F),
        timed(15, 20.0F),
        timed(16, 25.0F),
        timed(17, 33.75F),
        timed(19, 26.25F),
        timed(23, 24.0F),
        timed(24, 21.33F),
        timed(25, 16.0F),
        timed(27, 16.0F),
        timed(29, 16.0F),
        timed(51, 21.33F),
        timed(52, 21.33F),
        timed(53, 22.67F),
        timed(54, 22.67F),
        timed(55, 22.67F),
        timed(56, 22.67F),
        timed(57, 44.0F),
        timed(58, 44.0F),
        timed(69, 16.0F),
        timed(71, 5.33F),
        timed(72, 18.67F),
        timed(73, 18.67F),
        timed(78, 28.0F),
        timed(79, 28.0F),
        timed(80, 17.33F),
        timed(81, 17.33F),
        timed(94, 26.67F),
        timed(95, 26.67F),
        timed(147, 30.67F),
        timed(148, 30.67F),
        timed(199, 28.0F),
        timed(212, 25.33F),
        timed(338, 20.0F),
        timed(339, 20.0F),
        timed(357, 24.0F),
        timed(389, 25.33F),
        timed(617, 26.67F),
        timed(643, 64.0F),
        evented(11, 24.0F, kRate08, {{8, kWindow}, {16, kEnd}, {17, kRecovery}}),
        evented(12, 16.0F, kRate08, {{5, kWindow}, {12, kEnd}, {13, kRecovery}}),
        evented(13, 24.0F, kRate08, {{14, kEnd}, {15, kRecovery}}),
        evented(14, 24.0F, kRate08, {{18, kEnd}, {19, kRecovery}}),
        evented(15, 16.0F, kRate08, {{11, kEnd}, {12, kRecovery}}),
        evented(16, 20.0F, kRate08, {{5, kWindow}, {15, kEnd}, {16, kRecovery}}),
        evented(17, 27.0F, kRate08, {{19, kEnd}, {20, kRecovery}}),
        evented(19, 21.0F, kRate08, {{13, kEnd}, {14, kRecovery}}),
        evented(25, 12.0F, 0, {{6, kEnd}}),
        evented(27, 12.0F, 0, {{6, kEnd}}),
        evented(29, 12.0F, 0, {{6, kEnd}}),
        evented(57, 33.0F, 0, {{14, kWindow}, {25, kEnd}})};
    // Each replaces the fight's clip of its id, or joins them (an id may appear only once in a set).
    for (const coney::test::LocomotionClip& clip : timedClips) {
        const auto same = std::ranges::find(clips, clip.id, &coney::test::LocomotionClip::id);
        if (same != clips.end()) {
            *same = clip;
        } else {
            clips.push_back(clip);
        }
    }
    return clips;
}

// The timing character, built once.
const FightCharacter& timingCharacter() {
    static const FightCharacter character(timingClips());
    return character;
}

// The lines of the input script `script` ordered by their frame (a script's frames must not go down), the lines of
// one frame kept in their order.
std::string ordered(std::string_view script) {
    std::vector<std::pair<int, std::string>> lines;
    std::size_t begin = 0;
    while (begin < script.size()) {
        const std::size_t end = std::min(script.find('\n', begin), script.size());
        const std::string_view line = script.substr(begin, end - begin);
        int frame = 0;
        std::from_chars(line.data(), line.data() + line.size(), frame);
        if (!line.empty()) {
            lines.emplace_back(frame, std::string(line));
        }
        begin = end + 1;
    }
    std::ranges::stable_sort(lines, {}, &std::pair<int, std::string>::first);
    std::string out;
    for (const auto& [frame, line] : lines) {
        out += line + "\n";
    }
    return out;
}

// One update of the player as the tests read it.
struct Step {
    int clip = 0;
    bool started = false; // a clip began this update: a new id, or the one playing again from its start
    Vec3 position;
    float speed = 0.0F;
    Gait gait = Gait::Standing;
    bool holds = false; // the move holds the body (no stick movement) at the end of the update
};

// What a scripted run left, update by update.
class Timeline {
  public:
    // Runs `frames` updates of `script` on `fight`, calling `each` after every update with the next update's index
    // (what it does is acted on at that update).
    template <typename Each> static Timeline record(Fight& fight, std::string_view script, int frames, Each each) {
        Timeline line;
        int lastClip = -1;
        float lastTime = 0.0F;
        fight.run(ordered(script), static_cast<std::uint64_t>(frames), [&](std::uint64_t frame) {
            const coney::human::Human& human = fight.human();
            const coney::human::HumanAnimator& animator = human.animator();
            const coney::anim::AnimTask* top = animator.tasks().top();
            const float time = top != nullptr ? top->time() : 0.0F;
            Step step;
            step.clip = static_cast<int>(animator.animId());
            step.started = step.clip != lastClip || (animator.drivingClipPlaying() && time < lastTime);
            step.position = human.position();
            step.speed = human.speed();
            step.gait = human.gait();
            // Held by the move: its clip's bits or combat's states, not the walk start the stick then plays.
            coney::human::GateInput gate = human.gateInput();
            gate.flags &= ~coney::human::kFlagStartClip;
            step.holds = human.fighter().holdsMovement(animator) || coney::human::stickBusy(gate) ||
                         coney::human::stickVelocityGated(gate);
            line.m_steps.push_back(step);
            lastClip = step.clip;
            lastTime = time;
            each(static_cast<int>(frame) + 1);
        });
        return line;
    }
    // Runs `frames` updates of `script` on `fight`.
    static Timeline record(Fight& fight, std::string_view script, int frames) {
        return record(fight, script, frames, [](int) {});
    }

    // The updates at which `clip` started.
    [[nodiscard]] std::vector<int> startsOf(int clip) const {
        std::vector<int> starts;
        for (int i = 0; i < size(); ++i) {
            if (at(i).started && at(i).clip == clip) {
                starts.push_back(i);
            }
        }
        return starts;
    }
    // The update at which `clip` started for the `n`th time (from 0), or -1 when it started fewer times (a clip that
    // never played must fail a check, not read past an empty list).
    [[nodiscard]] int nthStart(int clip, std::size_t n) const {
        const std::vector<int> starts = startsOf(clip);
        return n < starts.size() ? starts[n] : -1;
    }
    // The first update from `from` at which the clip is no longer `clip` (its end), or -1.
    [[nodiscard]] int endOf(int clip, int from) const {
        for (int i = from; i < size(); ++i) {
            if (at(i).clip != clip) {
                return i;
            }
        }
        return -1;
    }
    // The first update from `from` at which combat no longer holds the body, or -1.
    [[nodiscard]] int freedAt(int from) const {
        for (int i = from; i < size(); ++i) {
            if (!at(i).holds) {
                return i;
            }
        }
        return -1;
    }
    // The first update after `from` at which the player stands more than 1 cm from where he stood at `from`, or -1.
    [[nodiscard]] int movedAt(int from) const {
        const Vec3 there = at(from).position;
        for (int i = from + 1; i < size(); ++i) {
            if (std::hypot(at(i).position.x - there.x, at(i).position.y - there.y) > 0.01F) {
                return i;
            }
        }
        return -1;
    }
    // How far the player went between updates `from` and `to`, metres across the ground.
    [[nodiscard]] float travelled(int from, int to) const {
        return std::hypot(at(to).position.x - at(from).position.x, at(to).position.y - at(from).position.y);
    }
    // Whether `clip` played at any update.
    [[nodiscard]] bool played(int clip) const {
        return std::ranges::any_of(m_steps, [clip](const Step& step) { return step.clip == clip; });
    }
    [[nodiscard]] const Step& at(int update) const { return m_steps.at(static_cast<std::size_t>(update)); }
    [[nodiscard]] int size() const { return static_cast<int>(m_steps.size()); }

  private:
    std::vector<Step> m_steps;
};

// Script lines tapping `button` every `every` updates from `from` up to `to`.
std::string taps(std::string_view button, int from, int to, int every) {
    std::string lines;
    for (int frame = from; frame <= to; frame += every) {
        lines += std::format("{} tap {}\n", frame, button);
    }
    return lines;
}

// The first update at or after `from` at which a tap of the series `taps(button, first, last, every)` makes its
// command, `delay` updates after the tap: square and R1 on the press (0), cross and circle tapped on the release (1).
int nextCommand(int from, int first, int last, int every, int delay) {
    for (int frame = first; frame <= last; frame += every) {
        if (frame + delay >= from) {
            return frame + delay;
        }
    }
    return -1;
}

// Checks that `clip`, started at `start`, played its whole length (no press cut it short): it gives way on the update
// its time runs out, give or take the update the time carried over from the clip before it (a clip handed over to
// starts part-way into its first update).
void checkPlayedFully(const Timeline& line, int clip, int start, float updates) {
    const int end = line.endOf(clip, start + 1);
    INFO("clip " << clip << " from update " << start << " to " << end);
    REQUIRE(end > 0);
    CHECK(end - start >= static_cast<int>(std::floor(updates)));
    CHECK(end - start <= static_cast<int>(std::floor(updates)) + 1);
}

// Checks that nothing of a move is left once it is over: the player is free, not blocking, holding or held, has no
// attack playing and is not in a traversal or a sprint.
void checkNothingLeft(Fight& fight) {
    const coney::human::Fighter& fighter = fight.human().fighter();
    CHECK(fighter.combat().mode() == combat::CombatMode::Free);
    CHECK_FALSE(fighter.blocking());
    CHECK_FALSE(fighter.combat().chain().active());
    CHECK(fighter.held() == nullptr);
    CHECK(fighter.pairStage() == PairStage::None);
    CHECK_FALSE(fighter.grabbed());
    CHECK_FALSE(fighter.helpless(fight.human().animator()));
    CHECK(fight.human().traversal() == Traversal::None);
    CHECK_FALSE(fight.human().sprinting());
}

// Checks that from update `from` the stick, held at a walk, walks the player again: the walk start that update (as at
// runtime, where 413 replaces the move's end at once) and the walk's gait and clip after the start's 12 updates.
void checkWalksFrom(const Timeline& line, int from) {
    INFO("the stick back from update " << from);
    CHECK(line.at(from).clip == kWalkStart);
    CHECK(line.movedAt(from) > 0);
    CHECK(line.movedAt(from) <= from + 2);
    CHECK(line.at(from + 15).clip == kWalk);
    CHECK(line.at(from + 15).gait == Gait::Walk);
}

// A hit of attack `animId` with `damage`, `code` and `flags`, from 1 m in front of the player at (40, 40).
IncomingHit hitOf(int animId, int damage, int code, std::uint16_t flags = 0) {
    return IncomingHit{
        .damage = damage, .attackAnim = animId, .code = code, .flags = flags, .attacker = Vec3{40.0F, 41.0F, 0.0F}};
}

} // namespace

TEST_CASE("circle spammed with nobody in reach plays 71 and 69 once, in full; 389 takes the next circle",
          "[human][combat][timing]") {
    for (const int every : {2, 3, 4}) {
        Fight fight(timingCharacter(), 30.0F);
        // Taps from 10 (the grab on the release, 11) to 80, the stick at rest.
        const Timeline line = Timeline::record(fight, taps("circle", 10, 80, every), 120);
        INFO("a tap every " << every << " updates");
        // 71 (5 updates at runtime), then 69 (16), each once and in full: every press in them is dropped.
        REQUIRE_FALSE(line.startsOf(id::kGrabPlayerIntro).empty());
        CHECK(line.nthStart(id::kGrabPlayerIntro, 0) == 11);
        checkPlayedFully(line, id::kGrabPlayerIntro, 11, 5.33F);
        const int miss = line.endOf(id::kGrabPlayerIntro, 11);
        CHECK(line.at(miss).clip == kGrabMiss);
        checkPlayedFully(line, kGrabMiss, miss, 16.0F);
        // Then 389, which holds nothing: the next circle's release grabs again in it.
        const int over = line.endOf(kGrabMiss, miss);
        const std::vector<int> grabs = line.startsOf(id::kGrabPlayerIntro);
        REQUIRE(grabs.size() >= 2);
        CHECK(grabs[1] == nextCommand(over, 10, 80, every, 1));
        // (A circle released on the update 69 ends grabs at once, before 389 shows.)
        if (grabs[1] > over) {
            CHECK(line.at(over).clip == kNormalFromFight);
            CHECK(line.freedAt(11) == over);
        }
        CHECK(line.travelled(11, over - 1) < 0.01F);
    }
    // Left alone, 389 plays out (26 updates at runtime) into the idle.
    Fight fight(timingCharacter(), 30.0F);
    const Timeline line = Timeline::record(fight, "10 tap circle\n", 80);
    const int settle = line.nthStart(kNormalFromFight, 0);
    REQUIRE(settle >= 0);
    checkPlayedFully(line, kNormalFromFight, settle, 25.33F);
    CHECK(line.at(line.endOf(kNormalFromFight, settle)).clip == kIdle);
    checkNothingLeft(fight);
}

TEST_CASE("the stick at 60 % walks the player the update the grab miss's 69 ends; 389 never plays",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    // Circle every 3 updates from 10 to 31 (the last released inside 69), the stick at 60 % from 12 on.
    const Timeline line = Timeline::record(fight, taps("circle", 10, 31, 3) + "12 stick left 0 60\n", 90);
    CHECK(line.startsOf(id::kGrabPlayerIntro).size() == 1);
    const int miss = line.endOf(id::kGrabPlayerIntro, 11);
    const int over = line.endOf(kGrabMiss, miss);
    // Held still through 71 and 69, then the walk start at once and the walk.
    CHECK(line.travelled(11, over - 1) < 0.01F);
    CHECK(line.freedAt(11) == over);
    CHECK_FALSE(line.played(kNormalFromFight));
    checkWalksFrom(line, over);
    checkNothingLeft(fight);
}

TEST_CASE("circle held with nobody in reach plays the tackle's 4 and 2 in full, then 389", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    // The tackle comes on the 7th sample held (16); circle tapped every 4 updates after the release.
    const std::string script = "10 press circle\n24 release circle\n" + taps("circle", 30, 120, 4);
    const Timeline line = Timeline::record(fight, script, 140);
    CHECK(line.startsOf(id::kTacklePlayerIntro) == std::vector<int>{16});
    checkPlayedFully(line, id::kTacklePlayerIntro, 16, 6.67F);
    const int miss = line.endOf(id::kTacklePlayerIntro, 16);
    CHECK(line.at(miss).clip == kTackleMiss);
    checkPlayedFully(line, kTackleMiss, miss, 58.67F);
    const int over = line.endOf(kTackleMiss, miss);
    CHECK(line.at(over).clip == kNormalFromFight);
    CHECK(line.freedAt(16) == over);
    // The first circle after it grabs (and misses) again; none before.
    CHECK(line.nthStart(id::kGrabPlayerIntro, 0) == nextCommand(over, 30, 120, 4, 1));
}

TEST_CASE("square spammed plays S1, SS2 and SSS3 at their windows, each to its end, then S1 again",
          "[human][combat][timing]") {
    for (const int every : {2, 3, 4}) {
        Fight fight(timingCharacter(), 1.0F);
        const Timeline line = Timeline::record(fight, taps("square", 10, 100, every), 120);
        INFO("a press every " << every << " updates");
        // S1 at the first press; a press in its wind-up is buffered and plays SS2 when the window opens 6 updates in,
        // and SSS3 6 updates into SS2 (S1 6, SS2 6 at runtime).
        CHECK(line.nthStart(id::kAttackS1, 0) == 10);
        CHECK(line.nthStart(id::kAttackSS2, 0) == 16);
        CHECK(line.nthStart(id::kAttackSSS3, 0) == 22);
        // SSS3 ends the chain: presses in it are dropped and it plays to its end; the next press starts S1.
        checkPlayedFully(line, id::kAttackSSS3, 22, 26.25F);
        const int over = line.endOf(id::kAttackSSS3, 22);
        CHECK((line.at(over).clip == kFightIdle || line.at(over).clip == id::kAttackS1));
        const int second = line.nthStart(id::kAttackS1, 1);
        CHECK(second == nextCommand(over, 10, 100, every, 0));
        // The combo then repeats exactly.
        CHECK(line.nthStart(id::kAttackSS2, 1) == second + 6);
        CHECK(line.nthStart(id::kAttackSSS3, 1) == second + 12);
        CHECK(fight.human().fighter().hitsLanded() >= 6);
    }
}

TEST_CASE("S1: presses in its end phase and recovery are dropped; the next press and the stick come back as it ends",
          "[human][combat][timing]") {
    // S1 at 10 with nobody near; its length (20 updates at runtime) from a run with no other press.
    Fight alone(timingCharacter(), 30.0F);
    const Timeline single = Timeline::record(alone, "10 tap square\n", 60);
    const int over = single.endOf(id::kAttackS1, 10);
    CHECK(over - 10 >= 20);
    CHECK(over - 10 <= 21);
    // With nobody near it ends in 389, then the idle.
    CHECK(single.at(over).clip == kNormalFromFight);
    // A second press on each update from 15 (the end phase) on: dropped until S1 ends, a new S1 from then; one in
    // the window (up to 14) plays SS2.
    for (int delay = 12; delay <= 24; ++delay) {
        Fight probe(timingCharacter(), 30.0F);
        const Timeline line = Timeline::record(probe, std::format("10 tap square\n{} tap square\n", 10 + delay), 60);
        INFO("the second press " << delay << " updates after the first");
        CHECK(line.startsOf(id::kAttackSS2).empty() == (delay >= 15));
        if (delay >= 15) {
            CHECK((line.startsOf(id::kAttackS1).size() == 2) == (10 + delay >= over));
        }
    }
    // The stick held at 60 % from the press walks him on the update S1 ends.
    Fight walking(timingCharacter(), 30.0F);
    const Timeline walk = Timeline::record(walking, "10 tap square\n11 stick left 0 60\n", 60);
    CHECK(walk.freedAt(10) == over);
    CHECK(walk.travelled(10, over - 1) < 0.01F);
    checkWalksFrom(walk, over);
    checkNothingLeft(walking);
}

TEST_CASE("cross spammed plays X1 on the release and XX2 at its window, each to its end", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 1.0F);
    const Timeline line = Timeline::record(fight, taps("cross", 10, 100, 3), 130);
    // X1 starts on the first release (11); the next press (13) is buffered and plays XX2 when the window opens 10
    // updates in; a third cross does nothing.
    CHECK(line.nthStart(id::kAttackX1, 0) == 11);
    CHECK(line.nthStart(id::kAttackXX2, 0) == 21);
    checkPlayedFully(line, id::kAttackXX2, 21, 30.0F);
    const int over = line.endOf(id::kAttackXX2, 21);
    // The next release after XX2's end starts X1 again.
    CHECK(line.nthStart(id::kAttackX1, 1) == nextCommand(over, 10, 100, 3, 1));
}

TEST_CASE("X1 alone lasts 30 updates; the stick at 35 % walks him as it ends", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    const Timeline line = Timeline::record(fight, "10 tap cross\n12 stick left 0 35\n", 80);
    CHECK(line.startsOf(id::kAttackX1) == std::vector<int>{11});
    checkPlayedFully(line, id::kAttackX1, 11, 30.0F);
    const int over = line.endOf(id::kAttackX1, 11);
    CHECK(line.freedAt(11) == over);
    checkWalksFrom(line, over);
    checkNothingLeft(fight);
}

TEST_CASE("square then cross spammed plays S1 and SX2, which plays to its end", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 1.0F);
    const Timeline line = Timeline::record(fight, "10 tap square\n" + taps("cross", 12, 60, 3), 80);
    CHECK(line.startsOf(id::kAttackS1) == std::vector<int>{10});
    CHECK(line.nthStart(id::kAttackSX2, 0) == 16);
    checkPlayedFully(line, id::kAttackSX2, 16, 20.0F);
    // A cross released after SX2 is over starts X1.
    const int over = line.endOf(id::kAttackSX2, 16);
    CHECK(line.nthStart(id::kAttackX1, 0) == nextCommand(over, 12, 60, 3, 1));
}

TEST_CASE("a snap plays to its end under square spam, then the stick at rest gives S1", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    // The stick flicked fully right for one update with square (combat.md#input-scripts), then square every 2.
    const std::string script = "10 stick left 100 0\n10 tap square\n11 stick left 0 0\n" + taps("square", 12, 60, 2);
    const Timeline line = Timeline::record(fight, script, 80);
    CHECK(line.nthStart(id::kSnapRight, 0) == 10);
    checkPlayedFully(line, id::kSnapRight, 10, 16.0F);
    const int over = line.endOf(id::kSnapRight, 10);
    CHECK(line.nthStart(id::kAttackS1, 0) == nextCommand(over, 12, 60, 2, 0));
}

TEST_CASE("the walk attack at 35 % plays to its end, then the stick walks him on", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    // Walking at 35 %, square every 3 updates from 30 to 45: 23 at the first, the rest dropped.
    const std::string script = "5 stick left 0 35\n" + taps("square", 30, 45, 3);
    const Timeline line = Timeline::record(fight, script, 90);
    CHECK(line.at(29).gait == Gait::Walk);
    CHECK(line.startsOf(id::kAttackFromWalk) == std::vector<int>{30});
    checkPlayedFully(line, id::kAttackFromWalk, 30, 24.0F);
    const int over = line.endOf(id::kAttackFromWalk, 30);
    CHECK(line.freedAt(30) == over);
    checkWalksFrom(line, over);
    checkNothingLeft(fight);
}

TEST_CASE("the run attack drops every press and the run resumes as it ends", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 60.0F);
    // A run (the stick full ahead), then square every 2 updates from 40 to 60, all inside the run attack.
    const std::string script = "0 stick left 0 100\n" + taps("square", 40, 60, 2);
    const Timeline line = Timeline::record(fight, script, 110);
    CHECK(line.at(39).gait == Gait::Run);
    REQUIRE_FALSE(line.startsOf(id::kAttackFromRun).empty());
    CHECK(line.nthStart(id::kAttackFromRun, 0) == 40);
    checkPlayedFully(line, id::kAttackFromRun, 40, 21.33F);
    const int over = line.endOf(id::kAttackFromRun, 40);
    // Its bit 0x1000000 drops every press (nothing is buffered); the run's blend takes over at its end.
    CHECK(fight.human().fighter().combat().chain().buffered() == combat::ChainButton::None);
    CHECK(line.freedAt(40) == over);
    // The run's blend (its run 410 or sprint 411 slot leading) takes over at once.
    CHECK((line.at(over).clip == kRun || line.at(over).clip == kSprint));
    CHECK(line.startsOf(id::kAttackFromRun).size() == 1);
}

TEST_CASE("the charge and the dive play to their ends under cross or square spam", "[human][combat][timing]") {
    struct Case {
        std::string_view button;
        int clip;
        float updates;
    };
    for (const Case& c :
         {Case{"cross", id::kRunningAttackCharge, 26.67F}, Case{"square", id::kRunningAttackDive, 61.33F}}) {
        Fight fight(timingCharacter(), 60.0F);
        // A run, L2 held from 30, then the button every 3 updates from 40 to 64 (inside the charge too); L2 let go
        // at 110.
        const std::string script = "0 stick left 0 100\n30 press l2\n110 release l2\n" + taps(c.button, 40, 64, 3);
        const Timeline line = Timeline::record(fight, script, 160);
        INFO("L2 + " << c.button);
        const std::vector<int> starts = line.startsOf(c.clip);
        REQUIRE_FALSE(starts.empty());
        CHECK(starts.front() == 40);
        checkPlayedFully(line, c.clip, 40, c.updates);
        // Every press in it is dropped; the run resumes at its end, the stick still full.
        CHECK(starts.size() == 1);
        CHECK(line.freedAt(40) == line.endOf(c.clip, 40));
    }
}

TEST_CASE("R1 held blocks; let go, a press is taken at once but the stick moves him 5 updates later",
          "[human][combat][timing]") {
    SECTION("the stick") {
        Fight fight(timingCharacter(), 30.0F);
        const std::string script = "10 press r1\n20 stick left 60 0\n30 tap cross\n90 release r1\n";
        const Timeline line = Timeline::record(fight, script, 130);
        // The block holds the body: the stick at 60 % turns him with the shuffle and moves him nowhere.
        CHECK(line.travelled(11, 89) < 0.01F);
        CHECK(line.at(25).clip == 607);
        // Cross under the block plays X1 on the release, to its end, then the block's clip again (the closing 389
        // gives way to it).
        CHECK(line.startsOf(id::kAttackX1) == std::vector<int>{31});
        checkPlayedFully(line, id::kAttackX1, 31, 30.0F);
        CHECK(line.at(line.endOf(id::kAttackX1, 31)).clip == 607);
        // Let go at 90: the idle for 5 updates with the body held, then the walk start (at runtime 388 for 5, then
        // 413).
        CHECK(line.at(90).clip == kIdle);
        CHECK(line.freedAt(90) == 95);
        CHECK(line.travelled(89, 95) < 0.01F);
        CHECK(line.at(95).clip == kWalkStart);
        CHECK(line.movedAt(95) <= 97);
        checkNothingLeft(fight);
    }
    SECTION("a press") {
        Fight fight(timingCharacter(), 30.0F);
        const Timeline line = Timeline::record(fight, "10 press r1\n40 release r1\n40 tap square\n", 80);
        // Square on the update R1 is let go starts S1 at once.
        CHECK(line.startsOf(id::kAttackS1) == std::vector<int>{40});
        checkNothingLeft(fight);
    }
}

TEST_CASE("a grab reads nothing until the hold stands; strikes then play one at a time, each to its end",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 1.5F);
    // Circle at 10 (the grab on 11), then square every 2 updates from 12: the intro and the connect play untouched.
    const Timeline line = Timeline::record(fight, "10 tap circle\n" + taps("square", 12, 120, 2), 140);
    CHECK(line.startsOf(id::kGrabPlayerIntro) == std::vector<int>{11});
    checkPlayedFully(line, id::kGrabPlayerIntro, 11, 5.33F);
    const int connect = line.endOf(id::kGrabPlayerIntro, 11);
    CHECK(line.at(connect).clip == kGrabConnect);
    checkPlayedFully(line, kGrabConnect, connect, 18.67F);
    const int hold = line.endOf(kGrabConnect, connect);
    CHECK(line.at(hold).clip == kFrontHold);
    // The first square once the hold stands strikes (51 or 53); each strike plays to its end before the next.
    std::vector<int> strikes = line.startsOf(id::kGrabComboStrike1);
    const std::vector<int> others = line.startsOf(id::kGrabComboStrike2);
    strikes.insert(strikes.end(), others.begin(), others.end());
    std::ranges::sort(strikes);
    REQUIRE(strikes.size() >= 3);
    CHECK(strikes.front() == nextCommand(hold, 12, 120, 2, 0));
    for (std::size_t i = 1; i < strikes.size(); ++i) {
        INFO("strike " << i);
        CHECK(strikes[i] - strikes[i - 1] >= 22);
        CHECK(strikes[i] - strikes[i - 1] <= 25);
    }
    CHECK(fight.human().fighter().combat().mode() == combat::CombatMode::Grabbing);
    CHECK(fight.target().attached());
}

TEST_CASE("R1 spammed in a hold spins to the rear and back, each spin to its end", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 1.5F);
    const Timeline line = Timeline::record(fight, "10 tap circle\n" + taps("r1", 12, 110, 3), 130);
    REQUIRE(line.nthStart(kGrabConnect, 0) >= 0);
    const int hold = line.endOf(kGrabConnect, line.nthStart(kGrabConnect, 0));
    // The first R1 once the hold stands spins to the rear (78), in full; the next after it spins back (80).
    const std::vector<int> toRear = line.startsOf(id::kGrabSpinToRear);
    REQUIRE_FALSE(toRear.empty());
    CHECK(toRear.front() == nextCommand(hold, 12, 110, 3, 0));
    checkPlayedFully(line, id::kGrabSpinToRear, toRear.front(), 28.0F);
    const int rear = line.endOf(id::kGrabSpinToRear, toRear.front());
    CHECK(line.at(rear).clip == kRearHold);
    const std::vector<int> toFront = line.startsOf(id::kGrabSpinToFront);
    REQUIRE_FALSE(toFront.empty());
    CHECK(toFront.front() == nextCommand(rear, 12, 110, 3, 0));
    checkPlayedFully(line, id::kGrabSpinToFront, toFront.front(), 17.33F);
}

TEST_CASE("circle spammed in a hold with the stick at 60 % throws once; the stick walks him when the throw ends",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 1.5F);
    // The grab, then the stick 60 % ahead from 36 and circle every 3 updates from 40 to 66: one throw (147).
    const std::string script = "10 tap circle\n36 stick left 0 60\n" + taps("circle", 40, 66, 3);
    const Timeline line = Timeline::record(fight, script, 90);
    REQUIRE(line.nthStart(kGrabConnect, 0) >= 0);
    REQUIRE(line.endOf(kGrabConnect, line.nthStart(kGrabConnect, 0)) <= 40);
    CHECK(line.startsOf(id::kThrow1Front) == std::vector<int>{40});
    checkPlayedFully(line, id::kThrow1Front, 40, 30.67F);
    const int over = line.endOf(id::kThrow1Front, 40);
    // No grab started in the throw (the circles in it are dropped), and the stick walks him at its end.
    CHECK(line.startsOf(id::kGrabPlayerIntro) == std::vector<int>{11});
    CHECK(line.freedAt(40) == over);
    checkWalksFrom(line, over);
    checkNothingLeft(fight);
    CHECK(fight.target().state() == TargetState::Grounded);
    CHECK_FALSE(fight.target().attached());
}

TEST_CASE("L2 in a hold lets go: 95 plays to its end under circle spam, then the stick walks him round the target",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 1.5F);
    const std::string script =
        "10 tap circle\n40 press l2\n42 release l2\n44 stick left 0 60\n" + taps("circle", 44, 66, 3);
    const Timeline line = Timeline::record(fight, script, 120);
    CHECK(line.startsOf(id::kGrabLetGo) == std::vector<int>{40});
    checkPlayedFully(line, id::kGrabLetGo, 40, 26.67F);
    const int over = line.endOf(id::kGrabLetGo, 40);
    CHECK(line.startsOf(id::kGrabPlayerIntro) == std::vector<int>{11});
    CHECK(line.freedAt(40) == over);
    // The target stands 1.5 m away, so the stick walks him round it in the combat walk (380-387) at once.
    CHECK(line.at(over).clip >= 380);
    CHECK(line.at(over).clip <= 387);
    CHECK(line.movedAt(over) <= over + 1);
    checkNothingLeft(fight);
    CHECK(fight.target().state() == TargetState::Standing);
    CHECK_FALSE(fight.target().attached());
}

TEST_CASE("a tackle plays its intro and hit in full under square spam; mounted, each strike plays to its end",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 2.0F);
    const std::string script = "10 press circle\n24 release circle\n" + taps("square", 21, 140, 3);
    const Timeline line = Timeline::record(fight, script, 160);
    CHECK(line.startsOf(id::kTacklePlayerIntro) == std::vector<int>{16});
    checkPlayedFully(line, id::kTacklePlayerIntro, 16, 6.67F);
    const int hit = line.endOf(id::kTacklePlayerIntro, 16);
    CHECK(line.at(hit).clip == kTackleHit);
    checkPlayedFully(line, kTackleHit, hit, 45.33F);
    const int mounted = line.endOf(kTackleHit, hit);
    CHECK(line.at(mounted).clip == kMounted);
    // The first square once mounted strikes (212), then each after the last has ended.
    const std::vector<int> strikes = line.startsOf(id::kMountingStrike);
    REQUIRE(strikes.size() >= 2);
    CHECK(strikes.front() == nextCommand(mounted, 21, 140, 3, 0));
    checkPlayedFully(line, id::kMountingStrike, strikes[0], 25.33F);
    CHECK(strikes[1] == nextCommand(line.endOf(id::kMountingStrike, strikes[0]), 21, 140, 3, 0));
}

TEST_CASE("triangle spammed in a hold mugs once: the spin and the intro play to their ends",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 1.5F);
    const Timeline line = Timeline::record(fight, "10 tap circle\n" + taps("triangle", 40, 100, 3), 120);
    CHECK(line.startsOf(id::kGrabSpinToRear) == std::vector<int>{40});
    checkPlayedFully(line, id::kGrabSpinToRear, 40, 28.0F);
    const int intro = line.endOf(id::kGrabSpinToRear, 40);
    CHECK(line.at(intro).clip == kMugIntro);
    CHECK(line.startsOf(kMugIntro).size() == 1);
    checkPlayedFully(line, kMugIntro, intro, 20.0F);
    CHECK(fight.human().fighter().combat().mode() == combat::CombatMode::Mugging);
}

TEST_CASE("rage's start plays to its end under square spam, the body held", "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    // Filled by gains, which hold the meter 5 s (a set meter decays at once).
    combat::RageMeter& rage = fight.human().fighter().combat().rage();
    for (const float gain : {25.0F, 25.0F, 5.0F}) {
        rage.add(gain, combat::combatTuning(), 0);
    }
    REQUIRE(rage.full());
    const std::string script = "10 press l1 r1\n12 release l1 r1\n14 stick left 0 60\n" + taps("square", 15, 90, 3);
    const Timeline line = Timeline::record(fight, script, 120);
    CHECK(line.startsOf(id::kRageStart) == std::vector<int>{10});
    checkPlayedFully(line, id::kRageStart, 10, 64.0F);
    const int over = line.endOf(id::kRageStart, 10);
    CHECK(line.freedAt(10) == over);
    CHECK(line.travelled(10, over - 1) < 0.01F);
    // The first square after it attacks: the stick at 60 % walks him at once, so square gives the walk attack (or S1 if
    // he is not yet at a walk).
    const int attack = nextCommand(over, 15, 90, 3, 0);
    CHECK((line.nthStart(id::kAttackFromWalk, 0) == attack || line.nthStart(id::kAttackS1, 0) == attack));
    CHECK(fight.human().fighter().combat().rage().raging());
}

TEST_CASE("a duck's counter is asked once under square spam and plays to its end; R1 let go keeps the duck",
          "[human][combat][timing]") {
    for (const bool release : {false, true}) {
        Fight fight(timingCharacter(), 30.0F);
        // Blocking from 0 (let go at 13 in the second run); an attacker 1 m ahead warns at 10 (the duck on 11);
        // square every 2 updates from 12 to 40.
        const std::string script =
            (release ? "0 press r1\n13 release r1\n" : "0 press r1\n") + taps("square", 12, 40, 2);
        const Timeline line = Timeline::record(fight, script, 90, [&](int next) {
            if (next == 11) {
                fight.human().warn(coney::human::AttackNotice{.warning = combat::AttackWarning::Duck,
                                                              .attackAnim = id::kAttackSSX3,
                                                              .code = 0x25,
                                                              .attacker = Vec3{40.0F, 41.0F, 0.0F}});
            }
        });
        INFO((release ? "R1 let go in the duck" : "R1 held"));
        CHECK(line.startsOf(combat::kBlockDodge) == std::vector<int>{11});
        // The counter comes in the duck's window (k = 7-17 at runtime), once, and plays its 27 updates.
        const std::vector<int> counters = line.startsOf(kDuckCounter);
        REQUIRE(counters.size() == 1);
        CHECK(counters.front() - 11 >= 7);
        CHECK(counters.front() - 11 <= 18);
        checkPlayedFully(line, kDuckCounter, counters.front(), 26.67F);
        CHECK(fight.human().fighter().duckCounters() == 1);
    }
}

TEST_CASE("hit, the player reacts in full under square spam and the stick walks him when the reaction ends",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    // An S1 from in front at 10 (272, 16 updates); square every 3 updates up to 23 and the stick at 60 % from 11.
    const Timeline line =
        Timeline::record(fight, "11 stick left 0 60\n" + taps("square", 11, 23, 3), 70, [&](int next) {
            if (next == 10) {
                fight.human().takeHit(hitOf(id::kAttackS1, 14, 0x0a, 0x800));
            }
        });
    CHECK(line.startsOf(272) == std::vector<int>{10});
    checkPlayedFully(line, 272, 10, 16.0F);
    const int over = line.endOf(272, 10);
    CHECK(line.startsOf(id::kAttackS1).empty());
    CHECK(line.freedAt(10) == over);
    checkWalksFrom(line, over);
    checkNothingLeft(fight);
}

TEST_CASE("stunned, the player acts again after 357; knocked down, mashing gets him up sooner and he acts after 199",
          "[human][combat][timing]") {
    SECTION("stun") {
        Fight fight(timingCharacter(), 30.0F);
        // SX2 from in front (280, a stun): the reaction, then 357; square every 3 updates.
        const Timeline line = Timeline::record(fight, taps("square", 11, 80, 3), 100, [&](int next) {
            if (next == 10) {
                fight.human().takeHit(hitOf(id::kAttackSX2, 37, 0x1a, 0xc00));
            }
        });
        const int exit = line.nthStart(kStunExit, 0);
        REQUIRE(exit >= 0);
        CHECK(exit == line.endOf(280, 10));
        checkPlayedFully(line, kStunExit, exit, 24.0F);
        const int over = line.endOf(kStunExit, exit);
        CHECK(line.nthStart(id::kAttackS1, 0) == nextCommand(over, 11, 80, 3, 0));
    }
    SECTION("knockdown") {
        Fight still(timingCharacter(), 30.0F);
        Fight mashed(timingCharacter(), 30.0F);
        // A heavy mid hit from in front (288 knocks down); left alone he rises after 2750 ms, mashing sooner.
        const auto knock = [](Fight& fight) {
            return [&fight](int next) {
                if (next == 10) {
                    fight.human().takeHit(hitOf(653, 50, 0x26));
                }
            };
        };
        const Timeline alone = Timeline::record(still, "", 160, knock(still));
        const Timeline mash = Timeline::record(mashed, taps("square", 11, 150, 3), 200, knock(mashed));
        const int risesAlone = alone.nthStart(kRise, 0);
        const int risesMashed = mash.nthStart(kRise, 0);
        REQUIRE(risesAlone >= 0);
        REQUIRE(risesMashed >= 0);
        CHECK(risesAlone - 10 >= 82);
        CHECK(risesAlone - 10 <= 84);
        CHECK(risesMashed < risesAlone);
        // The rise plays to its end; the first square after it attacks.
        checkPlayedFully(mash, kRise, risesMashed, 28.0F);
        const int up = mash.endOf(kRise, risesMashed);
        CHECK(mash.nthStart(id::kAttackS1, 0) == nextCommand(up, 11, 150, 3, 0));
    }
}

TEST_CASE("a clip that never started has no start: the timeline's lookups fail a check, never read an empty list",
          "[human][combat][timing]") {
    Fight fight(timingCharacter(), 30.0F);
    // Nothing pressed: the player only idles.
    const Timeline line = Timeline::record(fight, "", 10);
    CHECK(line.startsOf(id::kRageStart).empty());
    CHECK(line.nthStart(id::kRageStart, 0) == -1);
    CHECK(line.nthStart(kIdle, 0) == 0);
    CHECK(line.nthStart(kIdle, 1) == -1);
    CHECK(line.endOf(kIdle, 0) == -1);
    CHECK(line.freedAt(0) == 0);
}
