// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the clubhouse's arcade cabinet, the sixth entry of the hub's trophies
// (FBMission[6]), leads to Armies of the Night as the original offers it (docs/research/armies.md#stages,
// docs/research/story-progress.md#hub-doors): `level95` starts at the checkpoint the story gives once `level84` is
// complete (11), from a story save with every story mission complete and the five flashbacks complete (or one of them
// not), in the game session as `--play-level` starts it (support/disc_session.h). A driver walks player 1 to the
// cabinet's flag with the left stick at a partial deflection along the level's paths (support/stick_walker.h) and taps
// triangle at its prompt. With the five complete the press loads `level60` and plays it; with one missing the cabinet
// only refuses. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints
// counts only (LEGAL.md).

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/story_graph.h"
#include "fileio/wad.h"
#include "gamemodes/start_up_flow.h"
#include "hud/hud.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scripting/lua_value.h"
#include "scripting/message_handlers.h"
#include "scripting/script_system.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"
#include "support/live_pad.h"
#include "support/stick_walker.h"
#include "warriors/game_state.h"
#include "world_objects/flags.h"

namespace {

using coney::anim::Vec3;

// The hub's checkpoint after `level84`, the last story mission (runNextMission's row 84 → 95 (11),
// docs/research/story-progress.md#order).
constexpr int kHubAfterStory = 11;
// The flashbacks FBMission[6] requires, in its order (level95.lua's main chunk, story-progress.md#hub-doors).
constexpr std::array<int, 5> kFlashbacks{82, 92, 83, 20, 11};
// The cabinet's flag (FBMission[6].flag = fMenu[4], AddFlag "fMenu_4" in level95.lua) and `level60`'s record in
// the level table (docs/references/levels.md#level-24).
constexpr std::string_view kCabinetFlag = "fMenu_4";
constexpr std::size_t kLevel60Record = 24;
// How near the driver goes to the flag before it stops: inside the context action's 1.8 m.
constexpr float kPromptReach = 1.0F;
// Frames the walk may take (the hub's opening holds the player first), the prompt may take to show once he stands
// there, and the load may take after the press: story.LoadLevel comes 1,000 ms after it (ScheduleFuncArg1), then the
// loading screen and level60's intro scene.
constexpr int kWalkFrames = 3000;
constexpr int kPromptFrames = 30;
constexpr int kLoadFrames = 150;

// How many of `lines` from `from` on contain `text`.
std::size_t countFrom(const std::vector<std::string>& lines, std::size_t from, std::string_view text) {
    std::size_t count = 0;
    for (std::size_t i = from; i < lines.size(); ++i) {
        count += lines[i].find(text) != std::string::npos ? 1 : 0;
    }
    return count;
}

// level95 at the story's end with a pad the driver steers frame by frame, the scripts' calls traced. The story save is
// made as the level's Lua state is fresh: global.lua's main chunk fills the unlock records (CfgUnlockables), and
// UM_Unlock(level, 0, 0), as a mission's completion calls it, completes every story mission and `flashbacks`.
class Clubhouse {
  public:
    Clubhouse(const coney::io::Wad& wad, std::span<const int> flashbacks)
        : m_run(wad, m_pad), m_scripts(m_run.session().flow().scripts()),
          m_complete(m_run.session().flow().story().findLastMissionLevels()) {
        // findLastMission's list includes level64, Armies of the Night's last stage, which the story never plays.
        std::erase(m_complete, 64);
        m_complete.insert(m_complete.end(), flashbacks.begin(), flashbacks.end());
        m_run.session().startAtLevel("level95", kHubAfterStory, [this] {
            m_scripts.traceCalls([this](std::string_view line) { m_trace.emplace_back(line); });
            (void)m_scripts.runFile(coney::script::kGlobalScript);
            for (const int level : m_complete) {
                const std::array<coney::script::Value, 3> unlock{coney::script::Value(static_cast<double>(level)),
                                                                 coney::script::Value(0.0), coney::script::Value(0.0)};
                (void)m_scripts.call("UM_Unlock", unlock);
            }
        });
    }

    // Runs until the level plays; whether it does.
    bool start() { return m_run.runUntilPlay(); }
    coney::test::DiscSession& run() { return m_run; }
    coney::test::StickWalker& walker() { return m_walker; }
    coney::StartUpFlow& flow() { return m_run.session().flow(); }
    // The scripts' traced calls, and how many from `from` on contain `text`.
    const std::vector<std::string>& trace() const { return m_trace; }
    std::size_t traced(std::size_t from, std::string_view text) const { return countFrom(m_trace, from, text); }

    // The cabinet's flag: its handle and where it stands; nothing when the hub has no such flag.
    std::optional<std::pair<double, Vec3>> cabinet() {
        const std::optional<double> handle = flow().flags().findByName(kCabinetFlag);
        const coney::world_objects::WorldFlag* flag = handle ? flow().flags().find(*handle) : nullptr;
        if (flag == nullptr) {
            return std::nullopt;
        }
        return std::pair{*handle, Vec3{flag->position[0], flag->position[1], flag->position[2]}};
    }

  private:
    coney::test::LivePad m_pad;
    coney::test::DiscSession m_run;
    coney::test::StickWalker m_walker{m_run, m_pad};
    coney::script::ScriptSystem& m_scripts;
    std::vector<int> m_complete;
    std::vector<std::string> m_trace;
};

// What the cabinet showed and did: whether its flag has a prompt, the HUD's prompt once the player stood at it, and
// the handler's calls after triangle.
struct CabinetPress {
    bool walked = false;
    std::string flagPrompt;
    std::string hudPrompt;
    std::size_t bonusMarkers = 0;
    std::size_t plays = 0;
    std::size_t refusals = 0;
};

// Walks to the cabinet, waits for its prompt, taps triangle and idles two frames.
CabinetPress pressCabinet(Clubhouse& hub) {
    CabinetPress press;
    const std::optional<std::pair<double, Vec3>> cabinet = hub.cabinet();
    REQUIRE(cabinet.has_value());
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const auto [handle, place] = *cabinet;
    const coney::script::MessageHandlers* messages = hub.flow().context().messages;
    REQUIRE(messages != nullptr);
    if (const auto found = messages->prompts().find(handle); found != messages->prompts().end()) {
        press.flagPrompt = found->second;
    }
    press.bonusMarkers = hub.traced(0, "ObjSpawn(\"dyn_w_bonus\"");
    press.walked = hub.walker().walkTo(place, kPromptReach, kWalkFrames);
    (void)hub.walker().idleUntil([&] { return !hub.flow().hud().actionPrompt(0).empty(); }, kPromptFrames);
    press.hudPrompt = hub.flow().hud().actionPrompt(0);
    const std::size_t from = hub.trace().size();
    hub.walker().frame(0, 0, "triangle");
    hub.walker().idle(2);
    press.plays = hub.traced(from, "> flashback.PlayMission(");
    press.refusals = hub.traced(from, "> flashback.NoAction(");
    return press;
}

} // namespace

TEST_CASE("the disc's level95: with the story and the five flashbacks complete, triangle at the arcade cabinet starts "
          "Armies of the Night",
          "[disc][story][hub][armies]") {
    // Expected: story-progress.md#hub-doors, confirmed (code) at level95.lua:f73 and f76: flashback.Setup gives
    // fMenu[4] the PLAY prompt and the handler flashback.PlayMission once level60's type-9 record (unlocked by
    // level84's completion, global.lua's Unlockables) is open and 82, 92, 83, 20 and 11 are complete; triangle runs
    // it, MA_LOADFBLEVEL schedules story.LoadLevel(60) 1,000 ms later, and level60 loads at checkpoint 1; the original
    // did so from the same save (confirmed (runtime), run r-20261122-level95-armies-trophy).
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    Clubhouse hub(*wad, kFlashbacks);
    REQUIRE(hub.start());
    const CabinetPress press = pressCabinet(hub);

    // The press loads level60, which plays its first frame.
    const std::size_t from = hub.trace().size();
    coney::test::DiscSession& run = hub.run();
    const bool loading = run.runUntil(
        [&] {
            const auto& last = run.session().lastLevelStart();
            return last.has_value() && last->first == "level60";
        },
        kLoadFrames);
    const bool playing = loading && run.runUntilPlay();
    const coney::GameState& state = hub.flow().state();
    std::printf("  level95 arcade cabinet: prompt on the flag %d, %zu bonus markers, walked %d, HUD prompt %d, "
                "PlayMission %zu, NoAction %zu, LoadLevel %zu, level60 loading %d, playing %d (record %zu, "
                "checkpoint %d)\n",
                press.flagPrompt.empty() ? 0 : 1, press.bonusMarkers, press.walked ? 1 : 0,
                press.hudPrompt.empty() ? 0 : 1, press.plays, press.refusals, hub.traced(from, "> story.LoadLevel("),
                loading ? 1 : 0, playing ? 1 : 0, static_cast<std::size_t>(state.currentLevel),
                static_cast<int>(state.checkPoint));
    CHECK_FALSE(press.flagPrompt.empty());
    CHECK(press.bonusMarkers == 1); // the cabinet's; the five flashbacks' trophies have none once complete
    CHECK(press.walked);
    CHECK(press.hudPrompt == press.flagPrompt);
    CHECK(press.plays == 1);
    CHECK(press.refusals == 0);
    CHECK(loading);
    CHECK(playing);
    CHECK(static_cast<std::size_t>(state.currentLevel) == kLevel60Record);
    CHECK(state.checkPoint == 1);
}

TEST_CASE("the disc's level95: with a flashback missing, the arcade cabinet refuses and the hub goes on",
          "[disc][story][hub][armies]") {
    // Expected: story-progress.md#hub-doors, confirmed (code) at level95.lua:f73: with FBMission[6]'s requirement
    // unmet, fMenu[4] gets a bonus marker and the handler flashback.NoAction, whose prompt names what is missing;
    // triangle takes it and nothing loads, as in the original (confirmed (runtime), run
    // r-20261122-level95-armies-trophy).
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // Every flashback but the last, level11.
    Clubhouse hub(*wad, std::span(kFlashbacks).first(4));
    REQUIRE(hub.start());
    const CabinetPress press = pressCabinet(hub);
    hub.walker().idle(kLoadFrames);
    const auto& last = hub.run().session().lastLevelStart();
    const bool stillHub = last.has_value() && last->first == "level95" && hub.run().session().inPlay();
    std::printf(
        "  level95 arcade cabinet, level11 incomplete: prompt on the flag %d, %zu bonus markers, walked %d, HUD "
        "prompt %d, PlayMission %zu, NoAction %zu, still in the hub %d\n",
        press.flagPrompt.empty() ? 0 : 1, press.bonusMarkers, press.walked ? 1 : 0, press.hudPrompt.empty() ? 0 : 1,
        press.plays, press.refusals, stillHub ? 1 : 0);
    CHECK_FALSE(press.flagPrompt.empty());
    CHECK(press.bonusMarkers == 2); // the cabinet's and level11's trophy's
    CHECK(press.walked);
    CHECK(press.hudPrompt == press.flagPrompt);
    CHECK(press.plays == 0);
    CHECK(press.refusals == 1);
    CHECK(stillHub);
}
