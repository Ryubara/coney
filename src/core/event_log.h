// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

/// A run's event log (`--event-log`): what a player would notice happening, in order, for a differential playthrough
/// against the original (docs/guides/research-workflow.md#differential-playthroughs). Each event is a kind, a name and
/// a detail, stamped with the step (the play update) it happened in:
///
/// | Kind | Name | Detail |
/// | --- | --- | --- |
/// | `call` | a script binding the scripts called | its arguments |
/// | `callback` | a script function the engine called by name | its arguments |
/// | `hint` | the text of a tutorial hint as it starts to show | |
/// | `sound` | a sound started, its name hash as `0x%08x` | |
/// | `human_in` / `human_out` / `human_gone` | a human that appeared, ran out of health, was deleted | |
///
/// The log is a process-wide tap, like a logger: the subsystems that see an event emit it wherever they are, and it
/// costs one test of a flag when no log is open. It never changes what the game does.
namespace coney::events {

/// Where events go: the step, kind, name and detail of each.
using Sink =
    std::function<void(std::uint64_t step, std::string_view kind, std::string_view name, std::string_view detail)>;

/// Starts sending events to `sink` (an empty one stops the log). Not thread-safe: call it before the game runs.
void setSink(Sink sink);
/// Whether a log is open, so an emitter can skip building an event's text.
[[nodiscard]] bool enabled();
/// The step events are stamped with from now on: the play update about to run (`--trace`'s numbering).
void setStep(std::uint64_t step);
/// The step events are stamped with.
[[nodiscard]] std::uint64_t step();
/// Sends one event to the log, if one is open.
void emit(std::string_view kind, std::string_view name, std::string_view detail = {});

/// One CSV line (with its newline) for an event: `step,kind,name,detail`, a field quoted when it holds a comma, a
/// quote or a line break (quotes doubled, line breaks kept inside the quotes).
[[nodiscard]] std::string csvLine(std::uint64_t step, std::string_view kind, std::string_view name,
                                  std::string_view detail);
/// The CSV file's header line.
inline constexpr std::string_view kCsvHeader = "step,kind,name,detail\n";

} // namespace coney::events
