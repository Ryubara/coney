// SPDX-License-Identifier: GPL-3.0-or-later
// The engine pages of the debug menus: Time, Tunables, Display and Input. Coney's own tools (no @orig).
#include "debug/debug_pages.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <utility>

#include "core/pad.h"
#include "debug/debug_session.h"

namespace coney::debug {

namespace {

// The names of the buttons held in `buttons`, in the pad word's bit order; `-` for none.
std::string buttonNames(std::uint16_t buttons) {
    static constexpr std::array<std::string_view, 16> kNames{"L2",    "R2",     "L1",     "R1",  "triangle", "circle",
                                                             "cross", "square", "select", "L3",  "R3",       "start",
                                                             "up",    "right",  "down",   "left"};
    std::string text;
    for (std::size_t bit = 0; bit < kNames.size(); ++bit) {
        if ((buttons & (1U << bit)) != 0) {
            text += (text.empty() ? "" : " ") + std::string(kNames.at(bit));
        }
    }
    return text.empty() ? "-" : text;
}

// The menu callbacks below copy strings and call the registry; all they can throw is a failed allocation, which
// ends the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

// The Tunables page of one category: each tunable as a toggle or a number with its default, then a reset.
void fillTunableCategory(MenuPage& page, TunableRegistry& registry, const std::string& category) {
    for (Tunable* tunable : registry.inCategory(category)) {
        const std::string path = tunable->path();
        // The items read the queued value, so a change shows at once though it lands between steps.
        const auto get = [&registry, path] { return registry.value(path).value_or(0.0); };
        const auto set = [&registry, path](double value) { registry.set(path, value); };
        if (tunable->type() == TunableType::Bool) {
            page.add(toggleItem(
                         tunable->name(), [get] { return get() != 0.0; }, [set](bool on) { set(on ? 1.0 : 0.0); }))
                .withHelp(tunable->description());
            continue;
        }
        MenuItem item = numberItem(tunable->name(), get, set, tunable->min(), tunable->max(), tunable->step(),
                                   tunable->type() == TunableType::Int);
        item.units = tunable->unitText();
        item.defaultValue = tunable->defaultValue();
        item.help = tunable->description().empty() ? std::format("Default {}", tunable->format(tunable->defaultValue()))
                                                   : std::format("{} (default {})", tunable->description(),
                                                                 tunable->format(tunable->defaultValue()));
        page.add(std::move(item));
    }
    page.add(actionItem("Reset this page", [&registry, category] {
        for (Tunable* tunable : registry.inCategory(category)) {
            registry.set(tunable->path(), tunable->defaultValue());
        }
    }));
}

} // namespace

void addTimePage(DebugSession& session) {
    TimeControl& time = session.time();
    // Steps run since the last sample (one per due step): 1 at full speed, 0 paused, a pulse in slow motion.
    auto lastSteps = std::make_shared<std::uint64_t>(0);
    session.model().addChannel("Time/Steps run", [&time, lastSteps] {
        const std::uint64_t steps = time.steps();
        const auto delta = static_cast<float>(steps - *lastSteps);
        *lastSteps = steps;
        return delta;
    });
    // The real frame time, when the platform measures it.
    const std::function<double()> frameMilliseconds = session.services().frameMilliseconds;
    if (frameMilliseconds) {
        session.model().addChannel("Time/Frame ms",
                                   [frameMilliseconds] { return static_cast<float>(frameMilliseconds()); });
    }
    session.model().addPage(
        "Time",
        [&time, frameMilliseconds](MenuPage& page) {
            page.add(toggleItem(
                         "Paused", [&time] { return time.paused(); }, [&time](bool paused) { time.setPaused(paused); }))
                .withHelp("Stops the game's steps; the menus keep running.");
            page.add(actionItem("Step one",
                                [&time] {
                                    time.setPaused(true);
                                    time.stepOnce();
                                }))
                .withHelp("Runs exactly one fixed 1/30 s step, then stays paused.");
            MenuItem slow = numberItem(
                "Slow motion", [&time] { return time.slowMotion(); },
                [&time](double divisor) { time.setSlowMotion(static_cast<int>(divisor)); }, 1, TimeControl::kMaxDivisor,
                1, true);
            slow.units = "x slower";
            slow.defaultValue = 1;
            slow.help = "Runs one fixed step out of every N that are due: 1/N speed; the steps never change length.";
            page.add(std::move(slow));
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
        "Pause, single step and slow motion; every step stays 1/30 s.");
}

void addTunablesPage(DebugSession& session) {
    TunableRegistry& registry = session.tunables();
    session.model().addPage(
        "Tunables",
        [&session, &registry](MenuPage& page) {
            for (const std::string& category : registry.categories()) {
                MenuItem item = submenuItem(category, [&registry, category] {
                    auto sub = std::make_shared<MenuPage>(category);
                    sub->setRebuild([&registry, category](MenuPage& p) { fillTunableCategory(p, registry, category); });
                    return sub;
                });
                item.detail = std::format("{}", registry.inCategory(category).size());
                page.add(std::move(item));
            }
            if (registry.size() == 0) {
                page.add(watchItem("No tunables", [] { return std::string("no subsystem registered one yet"); }));
            }
            page.add(actionItem("Reset all", [&registry] { registry.resetAll(); }))
                .withHelp("Every tunable back to its default.");
            const std::string file = session.services().tunablesFile;
            page.add(actionItem("Save overrides",
                                [&session, &registry, file] {
                                    if (file.empty()) {
                                        session.print("tunables: no file to save to");
                                        return;
                                    }
                                    auto saved = registry.save(file);
                                    session.print(saved ? std::format("tunables: saved {}", file)
                                                        : saved.error().message);
                                }))
                .withHelp("Writes the values that differ from their defaults to the overrides file.");
            page.add(actionItem("Load overrides",
                                [&session, &registry, file] {
                                    auto loaded = file.empty() ? std::expected<std::size_t, Error>(fail(
                                                                     ErrorCode::NotFound, "tunables: no file to load"))
                                                               : registry.load(file);
                                    session.print(loaded ? std::format("tunables: {} overrides from {}", *loaded, file)
                                                         : loaded.error().message);
                                }))
                .withHelp("Reads the overrides file again; values change between two steps.");
            page.add(watchItem("File", [file] { return file.empty() ? std::string("(none)") : file; }));
        },
        "Live-editable values of the subsystems, by category.");
}

// NOLINTEND(bugprone-exception-escape)

void addDisplayPage(DebugSession& session) {
    DisplayOptions& display = session.display();
    session.model().addPage(
        "Display",
        [&display](MenuPage& page) {
            page.add(toggleItem(
                         "Frame stats", [&display] { return display.frameStats; },
                         [&display](bool on) { display.frameStats = on; }))
                .withHelp("Frames and steps run, and the time controls, in the top-right corner.");
            page.add(toggleItem(
                         "Safe area", [&display] { return display.safeArea; },
                         [&display](bool on) { display.safeArea = on; }))
                .withHelp("The GUI square: where the menus and the HUD place things (docs/research/graphics.md).");
            page.add(toggleItem(
                         "Logical screen", [&display] { return display.logicalBounds; },
                         [&display](bool on) { display.logicalBounds = on; }))
                .withHelp("The edges of the original's 640 x 448 screen.");
        },
        "Overlays over the game's screen.");
}

void addInputPage(DebugSession& session) {
    const InputGate& gate = session.gate();
    MenuModel& model = session.model();
    model.addChannel("Input/Left stick x", [&gate] { return gate.rawPad().leftX(); });
    model.addChannel("Input/Left stick y", [&gate] { return gate.rawPad().leftY(); });
    model.addChannel("Input/Right stick x", [&gate] { return gate.rawPad().rightX(); });
    model.addChannel("Input/Right stick y", [&gate] { return gate.rawPad().rightY(); });
    model.addPage(
        "Input",
        [&gate](MenuPage& page) {
            const Pad& pad = gate.rawPad();
            page.add(watchItem("Connected", [&pad] { return std::string(pad.connected() ? "yes" : "no"); }));
            page.add(watchItem("Held", [&pad] { return buttonNames(pad.buttons()); }));
            page.add(watchItem(
                "Left stick x", [&pad] { return std::format("{:+.2f}", pad.leftX()); }, "Input/Left stick x"));
            page.add(watchItem(
                "Left stick y", [&pad] { return std::format("{:+.2f}", pad.leftY()); }, "Input/Left stick y"));
            page.add(watchItem(
                "Right stick x", [&pad] { return std::format("{:+.2f}", pad.rightX()); }, "Input/Right stick x"));
            page.add(watchItem(
                "Right stick y", [&pad] { return std::format("{:+.2f}", pad.rightY()); }, "Input/Right stick y"));
            page.add(watchItem("Sticks raw",
                               [&pad] {
                                   const auto& raw = pad.rawSticks();
                                   return std::format("L {} {}  R {} {}", raw[2], raw[3], raw[0], raw[1]);
                               }))
                .withHelp("The PS2 bytes: 0 left or up, 255 right or down; 95 to 160 reads as centred.");
            page.add(watchItem("L2 / R2 pressure", [&pad] {
                const auto& pressure = pad.pressure();
                return std::format("{} / {}", pressure.at(static_cast<std::size_t>(pad::Pressure::L2)),
                                   pressure.at(static_cast<std::size_t>(pad::Pressure::R2)));
            }));
            page.add(watchItem("Hidden from game", [&gate] { return buttonNames(gate.hiddenButtons()); }))
                .withHelp("Buttons the menu keeps from gameplay until they are let go.");
        },
        "Port 1 live: buttons, sticks and pressures.");
}

} // namespace coney::debug
