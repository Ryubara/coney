// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc what `level99` checkpoint 3 shows and does from the hit that breaks the wooden
// fence to the cut to the flash dealer's camera, as the original does (docs/research/scripting.md#level99, item 44;
// docs/research/objects.md#barriers; issue #233). The level plays in the game session as `--play-level level99
// --checkpoint 3` plays it (support/disc_session.h), the lesson's scenes skipped with the pad, then
// `P3.WoodenFences` called as the box `vAlmostDone` calls it (a stand-in for the walk over the roofs, which other
// tests cover: the box needs the lesson's earlier steps to arm it) and its whole run played from there: Vermin's
// point, the cut to the fence, control given back, and the charge, which the pad drives like a gamepad (L2 held, the
// left stick toward the fence through the shown camera at 98 %, cross within 3 m of it). In the original (confirmed
// (runtime), five runs):
//
// - the fence is hidden at the hit, with its dust, and the three boards it throws are removed in the same update; its
//   body stays until its next 60-tick update, so it is removed 0-29 updates after the hit and `P3.FenceBroken` runs in
//   that update;
// - `P3.DealerPoizo` runs 14 updates later (`ScheduleFunc` 500 ms) and cuts to the dealer's camera at once, so the
//   hit-to-cut gap is 15-44 updates (0.5-1.5 s), never longer;
// - between the charge and the cut nothing fades and no screen effect is queued.
//
// Before the fix the boards stood in the gap until the fence's removal and the cut, which is what a player saw as the
// broken fence staying on screen for a second.
//
// It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only
// (LEGAL.md).

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

#include "animation/anim_math.h"
#include "core/input_script.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/wad.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "platform/scene_stage.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "warriors/created_humans.h"

namespace {

// The box `vAlmostDone`'s handle, as the lesson's handlers are called with it (docs/research/scripting.md#level99).
constexpr double kBoxHandle = 29.0;
// The two fences the player can charge, `dyn_door_fence02` and `03`: their centres' x at y -6.67 (item 43).
constexpr std::array<float, 2> kFenceX{23.6F, 27.98F};
constexpr float kFenceY = -6.67F;
// How far from a fence's centre its door counts as that fence's when asking whether it is drawn, m.
constexpr float kFenceReach = 2.0F;
// The charge: how near the fence the cross is pressed, m, and the stick's deflection toward it, a share of full: past
// the run's 0.95, short of full, as a thumb on a stick holds it.
constexpr float kPressWithin = 3.0F;
constexpr float kSprintStick = 0.98F;
// A walk to a spot: the stick's deflection (between the walk's dead zone and the run's 0.95), and how near counts as
// there, m.
constexpr float kWalkStick = 0.5F;
constexpr float kArrived = 0.4F;
// Where a player who walks up first stands before he sprints: 4.8 m short of the fence, as the original's slot 10
// stands him (docs/research/scripting.md#level99, item 44).
constexpr float kStandShort = 4.8F;
// How long such a player stands there before the sprint, frames: 2 s, long enough for the dealer to turn to him.
constexpr std::uint64_t kStandFrames = 60;
// The frames a walk up may take.
constexpr std::uint64_t kWalkLimit = 600;
// The frames the run may take to reach the lesson and from the charge to the dealer's cut.
constexpr std::uint64_t kLessonLimit = 3000;
constexpr std::uint64_t kCutLimit = 400;
// The letterbox opens over 1 s after `P3.BreakFence`, in frames.
constexpr std::uint64_t kLetterboxOut = 40;
// A camera jump of this size (m) in one update is a cut.
constexpr float kCutJump = 3.0F;
// The original's windows, updates: the fence's removal after the hit, `P3.DealerPoizo` after `P3.FenceBroken`, the
// cut after the hit (docs/research/scripting.md#level99, item 44), and how soon the charge's stop follows the hit.
constexpr std::uint64_t kRemovalMost = 29;
constexpr std::uint64_t kScheduleUpdates = 14;
constexpr std::uint64_t kCutLeast = 15;
constexpr std::uint64_t kCutMost = 44;
constexpr std::uint64_t kStopMost = 4;
// Where the boards' type names end: `dyn_wooddmg_a` and `_b`.
constexpr std::array<std::string_view, 2> kBoardTypes{"dyn_wooddmg_a", "dyn_wooddmg_b"};

// Player 1's pad: cross tapped to skip a scene while skipping; while charging L2 held and the left stick toward the
// fence at kSprintStick, and cross for two frames within kPressWithin of it.
class ChargeDriver final : public coney::InputSource {
  public:
    // `play` gives the play mode once the level plays.
    explicit ChargeDriver(std::function<coney::platform::PlayLevelMode*()> play) : m_play(std::move(play)) {}

    coney::PortSamples sample(std::uint64_t frame) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        if (m_skipping && frame % 20 == 0) {
            pad.buttons = coney::pad::kCross;
        }
        const coney::platform::PlayLevelMode* play = m_play();
        if (play == nullptr) {
            return samples;
        }
        const coney::anim::Vec3 feet = play->player().human().position();
        // The walk to a spot, the stick at kWalkStick, until he stands within kArrived of it.
        if (m_walking) {
            const float dx = m_walkTo.x - feet.x;
            const float dy = m_walkTo.y - feet.y;
            if (std::hypot(dx, dy) < kArrived) {
                m_walking = false;
            } else {
                steer(pad, play->player(), dx, dy, kWalkStick);
            }
            return samples;
        }
        if (!m_charging) {
            return samples;
        }
        if (m_pressed < 2) {
            steer(pad, play->player(), m_targetX - feet.x, kFenceY - feet.y, kSprintStick);
        }
        pad.buttons |= coney::pad::kL2;
        if (m_pressed < 2 && (m_pressed > 0 || std::abs(kFenceY - feet.y) < kPressWithin)) {
            pad.buttons |= coney::pad::kCross;
            ++m_pressed;
        }
        return samples;
    }

    // Taps cross to skip scenes, or stops.
    void skipScenes(bool on) { m_skipping = on; }
    // Charges the fence whose centre is at x `x`.
    void chargeAt(float x) {
        m_charging = true;
        m_targetX = x;
    }
    // Walks to (x, y) and stands there.
    void walkTo(float x, float y) {
        m_walking = true;
        m_walkTo = {x, y, 0.0F};
    }
    // Whether a walk is still under way.
    [[nodiscard]] bool walking() const { return m_walking; }

  private:
    // Sets the left stick toward (dx, dy) at `magnitude`, turned by the shown camera's heading: up is the camera's
    // view along the ground, right its right.
    static void steer(coney::PadSample& pad, const coney::human::Player& player, float dx, float dy, float magnitude) {
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
        pad.sticks[2] = raw(right * magnitude);
        pad.sticks[3] = raw(-up * magnitude);
    }

    // The raw byte for `value` in [-1, 1] as an input script writes it (pad::stickValue reads it back).
    static std::uint8_t raw(float value) {
        return coney::stickByteFromPercent(static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * 100.0F)));
    }

    std::function<coney::platform::PlayLevelMode*()> m_play;
    bool m_skipping = false;
    bool m_charging = false;
    float m_targetX = 0.0F;
    int m_pressed = 0;
    bool m_walking = false;
    coney::anim::Vec3 m_walkTo{};
};

// When, in frames of the run, the events of one charge happened (0: not yet), and what showed in between.
struct Timeline {
    std::uint64_t charge = 0;         // the pad started the charge
    std::uint64_t hit = 0;            // the fence took the breaking hit
    std::uint64_t stopped = 0;        // the player stood still at the fence after it
    std::uint64_t removed = 0;        // the fence removed
    std::uint64_t fenceBroken = 0;    // P3.FenceBroken ran
    std::uint64_t dealerPoizo = 0;    // P3.DealerPoizo ran
    std::uint64_t cut = 0;            // the camera jumped to the dealer's
    std::uint64_t dealerHit = 0;      // P3.DealerHit ran (the dealer attacked)
    std::size_t effects = 0;          // ScreenQueueEffect calls from the charge to the cut
    float fadeMost = 0.0F;            // the greatest fade level from the charge to the cut
    std::size_t fenceDrawnBefore = 0; // doors drawn at the fence the update before the hit
    std::size_t fenceDrawnAtHit = 0;  // ... in the update of the hit
    std::size_t boardsBefore = 0;     // boards standing the update before the hit
    std::size_t boardsAfterMost = 0;  // the most boards standing in any update from the hit's to the cut
};

// How many boards the level holds.
std::size_t boardsStanding(const coney::platform::PlayLevelMode& play) {
    std::size_t count = 0;
    for (const std::string_view type : kBoardTypes) {
        count += play.standingObjectsOf(type);
    }
    return count;
}

// How many of `trace`'s lines hold `text`.
std::size_t countLines(const std::vector<std::string>& trace, std::string_view text) {
    return static_cast<std::size_t>(
        std::ranges::count_if(trace, [text](const std::string& line) { return line.find(text) != std::string::npos; }));
}

// Runs checkpoint 3 of level99 to the charge at fence `fence` begun `delay` frames after control returns, and on to the
// dealer's cut; the timeline, with 0 for what never came. With `walkUp` the player first walks to kStandShort short of
// the fence and stands there kStandFrames before the charge.
Timeline runCharge(const coney::io::Wad& wad, std::size_t fence, std::uint64_t delay, bool walkUp = false) {
    coney::platform::PlayLevelMode* playPtr = nullptr;
    ChargeDriver driver([&playPtr] { return playPtr; });
    coney::test::DiscSession run(wad, driver);
    coney::script::ScriptSystem& scripts = run.session().flow().scripts();
    // Each line the scripts trace, with the frame it came in: the step running when the callback fires is the one
    // `run.frames()` then counts next, so the line is stamped one more, as the log's lines below are.
    std::vector<std::string> trace;
    std::vector<std::uint64_t> traceFrame;
    run.session().startAtLevel("level99", 3, [&scripts, &trace, &traceFrame, &run] {
        scripts.traceCalls([&trace, &traceFrame, &run](std::string_view line) {
            trace.emplace_back(line);
            traceFrame.push_back(run.frames() + 1);
        });
    });
    REQUIRE(run.runUntilPlay());
    playPtr = run.session().play();
    REQUIRE(playPtr != nullptr);

    // The frame a trace line holding `text` first appeared in, 0 if none has.
    const auto traceAt = [&](std::string_view text) -> std::uint64_t {
        for (std::size_t i = 0; i < trace.size(); ++i) {
            if (trace[i].find(text) != std::string::npos) {
                return traceFrame[i];
            }
        }
        return 0;
    };

    // The scenes skipped up to control (Vermin's fence demonstration starts as it ends), then the fence lesson:
    // `P3.WoodenFences` as the box calls it, and its whole run up to control given back.
    driver.skipScenes(true);
    REQUIRE(run.runUntil([&] { return traceAt("> P3.VerminFencePoizo") != 0; }, kLessonLimit));
    driver.skipScenes(false);
    run.run(30);
    const coney::HumanCreation* player = run.session().flow().humans().player(1);
    REQUIRE(player != nullptr);
    const std::array<coney::script::Value, 2> enter{coney::script::Value(kBoxHandle),
                                                    coney::script::Value(player->handle)};
    REQUIRE(scripts.call("P3.WoodenFences", enter));
    REQUIRE(run.runUntil([&] { return traceAt("> P3.BreakFence") != 0; }, kLessonLimit));
    run.run(kLetterboxOut + delay);
    if (walkUp) {
        driver.walkTo(kFenceX.at(fence), kFenceY + kStandShort);
        REQUIRE(run.runUntil([&driver] { return !driver.walking(); }, kWalkLimit));
        run.run(kStandFrames);
    }

    // The charge, watched update by update until the dealer's camera is on.
    Timeline timeline;
    timeline.charge = run.frames();
    driver.chargeAt(kFenceX.at(fence));
    const coney::anim::Vec3 fenceAt{kFenceX.at(fence), kFenceY, 0.0F};
    const std::size_t effectsBefore = countLines(trace, "ScreenQueueEffect(");
    std::size_t logSeen = run.log().size();
    coney::anim::Vec3 eye = playPtr->cameraEye();
    std::size_t fenceDrawn = playPtr->doorsDrawnNear(fenceAt, kFenceReach);
    std::size_t boards = boardsStanding(*playPtr);
    for (std::uint64_t i = 0; i < kCutLimit && timeline.cut == 0; ++i) {
        run.run(1);
        const std::uint64_t now = run.frames();
        for (; logSeen < run.log().size(); ++logSeen) {
            const std::string& line = run.log()[logSeen];
            if (timeline.hit == 0 && line.starts_with("objects: ") &&
                line.find("struck by clip") != std::string::npos) {
                timeline.hit = now;
                timeline.fenceDrawnBefore = fenceDrawn;
                timeline.boardsBefore = boards;
            }
            if (timeline.removed == 0 && line.starts_with("objects: ") && line.find(" removed") != std::string::npos) {
                timeline.removed = now;
            }
        }
        fenceDrawn = playPtr->doorsDrawnNear(fenceAt, kFenceReach);
        boards = boardsStanding(*playPtr);
        if (timeline.hit != 0) {
            if (timeline.hit == now) {
                timeline.fenceDrawnAtHit = fenceDrawn;
            }
            timeline.boardsAfterMost = std::max(timeline.boardsAfterMost, boards);
            if (timeline.stopped == 0 && playPtr->player().human().speed() < 0.01F) {
                timeline.stopped = now;
            }
        }
        timeline.fadeMost = std::max(timeline.fadeMost, playPtr->stage().fadeLevel());
        timeline.fenceBroken = traceAt("> P3.FenceBroken");
        timeline.dealerPoizo = traceAt("> P3.DealerPoizo");
        timeline.dealerHit = traceAt("> P3.DealerHit");
        const coney::anim::Vec3 nowEye = playPtr->cameraEye();
        if (timeline.dealerPoizo != 0 && coney::anim::distance(nowEye, eye) > kCutJump) {
            timeline.cut = now;
        }
        eye = nowEye;
    }
    timeline.effects = countLines(trace, "ScreenQueueEffect(") - effectsBefore;
    return timeline;
}

// A run's numbers on one line, counts only.
void print(std::size_t fence, std::uint64_t delay, const Timeline& t) {
    std::printf(
        "  fence %zu, charge delayed %llu updates, then the updates from its start: hit +%llu, stopped +%llu, removed "
        "+%llu, "
        "FenceBroken +%llu, DealerPoizo +%llu, cut +%llu (hit to cut %llu); screen effects %zu, fade %.2f; "
        "fence drawn %zu -> %zu, boards %zu -> %zu\n",
        fence, static_cast<unsigned long long>(delay), static_cast<unsigned long long>(t.hit - t.charge),
        static_cast<unsigned long long>(t.stopped - t.charge), static_cast<unsigned long long>(t.removed - t.charge),
        static_cast<unsigned long long>(t.fenceBroken - t.charge),
        static_cast<unsigned long long>(t.dealerPoizo - t.charge), static_cast<unsigned long long>(t.cut - t.charge),
        static_cast<unsigned long long>(t.cut - t.hit), t.effects, static_cast<double>(t.fadeMost), t.fenceDrawnBefore,
        t.fenceDrawnAtHit, t.boardsBefore, t.boardsAfterMost);
}

} // namespace

TEST_CASE("the disc's level99: the wooden fence's break to the dealer's cut is the original's, and the fence goes at "
          "the hit",
          "[disc][story][fence]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The charge is delayed 0, 18 and 21 updates to meet the fence's 60-tick phase at a middling wait (the removal 20
    // updates after the hit), a short one (2) and the longest (29); the second fence is charged once too: the
    // original's window, 15-44 updates, from near both ends.
    const std::vector<std::pair<std::size_t, std::uint64_t>> charges{{0, 0}, {0, 18}, {0, 21}, {1, 0}};
    std::uint64_t gapLeast = kCutMost;
    std::uint64_t gapMost = 0;
    for (const auto& [fence, delay] : charges) {
        const Timeline t = runCharge(*wad, fence, delay);
        print(fence, delay, t);
        INFO("fence " << fence << ", delay " << delay);
        REQUIRE(t.hit != 0);
        REQUIRE(t.cut != 0);
        // The hit, the charge's stop, then the removal and P3.FenceBroken in one update, within the fence's phase.
        CHECK(t.stopped >= t.hit);
        CHECK(t.stopped <= t.hit + kStopMost);
        REQUIRE(t.removed != 0);
        CHECK(t.removed >= t.hit);
        CHECK(t.removed <= t.hit + kRemovalMost);
        CHECK(t.fenceBroken == t.removed);
        // P3.DealerPoizo 0.5 s (14 updates) on, and the cut in the update it runs in.
        CHECK(t.dealerPoizo == t.fenceBroken + kScheduleUpdates);
        CHECK(t.cut >= t.dealerPoizo);
        CHECK(t.cut <= t.dealerPoizo + 1);
        // The whole wait, the original's 15-44 updates.
        const std::uint64_t gap = t.cut - t.hit;
        CHECK(gap >= kCutLeast);
        CHECK(gap <= kCutMost);
        gapLeast = std::min(gapLeast, gap);
        gapMost = std::max(gapMost, gap);
        // Nothing fades and no screen effect is queued from the charge to the cut.
        CHECK(t.effects == 0);
        CHECK(t.fadeMost == 0.0F);
        // The fence was drawn until the hit and is gone in the hit's own update, and so are the boards it threw: the
        // gap holds no broken fence.
        CHECK(t.fenceDrawnBefore == 1);
        CHECK(t.fenceDrawnAtHit == 0);
        CHECK(t.boardsAfterMost <= t.boardsBefore);
    }
    std::printf("  hit to cut: %llu to %llu updates over %zu charges (the original's window 15 to 44)\n",
                static_cast<unsigned long long>(gapLeast), static_cast<unsigned long long>(gapMost), charges.size());
}

TEST_CASE("the disc's level99: a player who walks up to the wooden fence and then charges it sees no fade before it "
          "breaks",
          "[disc][story][fence]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Issue #106: the player walks up to 4.8 m short of each fence, stands 2 s (within the dealer's 17 m, so he turns
    // to face him) and only then sprints and charges. In the original the charge warns no one: its search finds nobody
    // between 3.29 and 4.11 m ahead, so the dealer, about 13 m off behind the fence, never hears of it and P3.DealerHit
    // never runs (docs/research/scripting.md#level99, item 44). So nothing fades and no screen effect is queued until
    // the fence breaks and P3.DealerPoizo cuts to the dealer.
    for (const std::size_t fence : {std::size_t{0}, std::size_t{1}}) {
        const Timeline t = runCharge(*wad, fence, 0, true);
        print(fence, 0, t);
        std::printf("  walked up first: P3.DealerHit %s\n", t.dealerHit != 0 ? "ran" : "never ran");
        INFO("fence " << fence << ", walked up first");
        REQUIRE(t.hit != 0);
        CHECK(t.dealerHit == 0);
        CHECK(t.effects == 0);
        CHECK(t.fadeMost == 0.0F);
        // The dealer's set-up waits for the break: the removal, P3.FenceBroken, then P3.DealerPoizo 14 updates on.
        REQUIRE(t.fenceBroken != 0);
        CHECK(t.fenceBroken >= t.hit);
        CHECK(t.dealerPoizo == t.fenceBroken + kScheduleUpdates);
        REQUIRE(t.cut != 0);
        CHECK(t.cut - t.hit >= kCutLeast);
        CHECK(t.cut - t.hit <= kCutMost);
    }
}
