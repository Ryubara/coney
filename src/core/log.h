// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "core/log_filter.h"
#include "core/log_record.h"
#include "core/log_ring.h"

/// Coney's run log: every line Coney writes about a run, stamped with the simulation step, the play time that step
/// stands for, its level and the checkpoint (`[step 120 | 00:04.0 | info cp2] combat: ...`), so a bug report or an
/// agent reading it sees what happened and when. The stamp counts steps, never real time, so a test-mode run writes
/// the same lines every time. The original has nothing like it: Coney's own debug tooling.
///
/// It is a process-wide tap, like coney::events (the `--event-log` CSV for differential playthroughs, a different
/// thing): a subsystem writes wherever it sees something happen; the platform sets the outputs (src/platform/
/// log_output.h: the console and the log file). Every line also goes into a ring of the last 30 s at every level,
/// which dump() writes into the log for a bug report, and an assertion failure or a crash writes before the
/// process ends.
///
/// Never write the game's own text or script source: names, ids, numbers and positions only (logs are attached to
/// public issues). How to read the log and add a line: docs/guides/run-log.md.
namespace coney::logging {

/// Where the lines that pass the filter go: the record and its formatted line (formatLine()).
using Sink = std::function<void(const Record& record, std::string_view line)>;
/// Where a dump's lines go (formatted, LocalOnly text withheld): the log file.
using DumpSink = std::function<void(std::string_view line)>;

/// Sends the lines that pass the filter to `sink`; an empty one sends them nowhere (they still go into the ring).
void setSink(Sink sink);
/// Sends dumps to `sink`; an empty one drops them.
void setDumpSink(DumpSink sink);
/// The filter lines must pass to reach the sink (Info and up by default).
void setFilter(const LogFilter& filter);
[[nodiscard]] LogFilter filter();

/// The step lines are stamped with from now on: the game mode stack sets it before each step it runs.
void setStep(std::uint64_t step);
[[nodiscard]] std::uint64_t step();
/// The checkpoint lines are stamped with from now on: the level's, as it starts or as its script sets one.
void setCheckpoint(int checkpoint);
/// Back to no checkpoint (`cp-`), as the front end runs.
void clearCheckpoint();
[[nodiscard]] int checkpoint();

/// Writes `text` (one or more lines; a line break ends each, and empty lines are skipped) as `level` lines of
/// `category`: into the ring, and to the sink when the filter lets them through.
void write(Category category, Level level, std::string_view text, Privacy privacy = Privacy::Public);
/// write() at each level.
inline void error(Category category, std::string_view text) { write(category, Level::Error, text); }
inline void warn(Category category, std::string_view text) { write(category, Level::Warn, text); }
inline void info(Category category, std::string_view text) { write(category, Level::Info, text); }
inline void detail(Category category, std::string_view text) { write(category, Level::Detail, text); }

/// Where an older log line belongs, read from its prefix (routeOf()).
struct Route {
    Category category = Category::Engine;
    Level level = Level::Info;
    Privacy privacy = Privacy::Public;
};
/// The category, level and privacy of a line that names its subsystem as a prefix (`pickup: ...`, `script error:
/// ...`): how the subsystems that log through a plain text callback are routed, each line keeping its text. A line
/// with no known prefix is an engine line at Info. The table: docs/guides/run-log.md#categories.
[[nodiscard]] Route routeOf(std::string_view line);
/// Writes each line of `text` as routeOf() places it: the text callbacks' way into the log.
void writeRouted(std::string_view text);

/// Writes the ring to the dump sink, framed by a first and a last line naming `reason` (`bug report`), and returns
/// how many records it held. The ring keeps them: a later dump writes them again.
std::size_t dump(std::string_view reason);
/// The ring, for tests and the debug menus.
[[nodiscard]] const LogRing& ring();

/// Puts the logger back as at start-up: no sinks, the default filter, step 0, no checkpoint, an empty ring. For
/// tests and for the platform's teardown.
void reset();

namespace detail {

/// The failure path's dump (CONEY_ASSERT, the platform's crash handler): dump(`reason`) the first time, nothing
/// after, so an abort after a failed assertion does not dump twice. Takes no lock it could wait on.
void dumpOnFailure(const char* reason) noexcept;

} // namespace detail

} // namespace coney::logging
