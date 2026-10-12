// SPDX-License-Identifier: GPL-3.0-or-later
// The Time page of the debug menus: pause, single step and the time scale. Coney's own tool (no @orig): the original
// has no debug menu (docs/research/debug.md#not-present). Behaviour: docs/guides/debug-menu.md#pages.
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/time_control.h"

namespace coney::debug {

namespace {

/// The scales the page offers one press away, in hundredths; 1x is the reset.
constexpr std::array<int, 5> kPresetPercents{10, 25, 50, 200, 400};

} // namespace

// The menu callbacks below format strings; all they can throw is a failed allocation, which ends the program either
// way.
// NOLINTBEGIN(bugprone-exception-escape)

void addTimePage(DebugSession& session) {
    TimeControl& time = session.time();
    // Steps run since the last sample (one per due step): 1 at full speed, 0 paused, a pulse when slowed.
    auto lastSteps = std::make_shared<std::uint64_t>(0);
    session.model().addChannel("Time/Steps run", [&time, lastSteps] {
        const std::uint64_t steps = time.steps();
        const auto delta = static_cast<float>(steps - *lastSteps);
        *lastSteps = steps;
        return delta;
    });
    // The real frame time, when the platform measures it. Its presence also says a real clock paces the run, which a
    // faster scale needs: a test-mode run is lockstep, one step a frame.
    const std::function<double()> frameMilliseconds = session.services().frameMilliseconds;
    if (frameMilliseconds) {
        session.model().addChannel("Time/Frame ms",
                                   [frameMilliseconds] { return static_cast<float>(frameMilliseconds()); });
    }
    // Sets the scale and says so, as a notice under the corner lines (and in the log).
    const auto setScale = [&session, &time](int percent) {
        time.setScalePercent(percent);
        session.notice(std::format("Time scale {}", formatTimeScale(time.scalePercent())));
    };
    session.model().addPage(
        "Time",
        [&time, frameMilliseconds, setScale](MenuPage& page) {
            page.add(toggleItem(
                         "Paused", [&time] { return time.paused(); }, [&time](bool paused) { time.setPaused(paused); }))
                .withHelp("Stops the game's steps; the menus keep running.");
            page.add(actionItem("Step one",
                                [&time] {
                                    time.setPaused(true);
                                    time.stepOnce();
                                }))
                .withHelp("Runs exactly one fixed 1/30 s step, then stays paused.");
            // The scale as a number; the item's own step is a twentieth, its fine step (L2) reaches the hundredths.
            MenuItem scale = numberItem(
                "Time scale", [&time] { return time.scale(); }, [&time](double value) { time.setScale(value); },
                TimeControl::kMinScalePercent / 100.0, TimeControl::kMaxScalePercent / 100.0, 0.05, false);
            scale.units = "x";
            scale.defaultValue = 1.0;
            scale.help = frameMilliseconds
                             ? "Game speed: more or fewer whole 1/30 s steps a second, never a shorter or longer step."
                             : "Game speed in whole 1/30 s steps; this run is lockstep, so above 1x it stays 1x.";
            page.add(std::move(scale));
            page.add(actionItem("Reset to 1x", [setScale] { setScale(100); }))
                .withHelp("The game's own speed: 30 steps a second.");
            for (const int percent : kPresetPercents) {
                page.add(actionItem(std::format("Preset {}", formatTimeScale(percent)),
                                    [setScale, percent] { setScale(percent); }))
                    .withHelp(percent < 100 ? "Slower: a share of the due steps runs; the menus stay at full speed."
                                            : "Faster: more steps a frame, up to 4 per 1x of scale.");
            }
            page.add(watchItem(
                "Due / run steps", [&time] { return std::format("{} / {}", time.frames(), time.steps()); },
                "Time/Steps run"));
            if (frameMilliseconds) {
                page
                    .add(watchItem(
                        "Frame time", [frameMilliseconds] { return std::format("{:.2f} ms", frameMilliseconds()); },
                        "Time/Frame ms"))
                    .withHelp("Real time per frame, measured by the platform; the steps stay 1/30 s.");
            }
        },
        "Pause, single step and the time scale; every step stays 1/30 s.");
}

// NOLINTEND(bugprone-exception-escape)

} // namespace coney::debug
