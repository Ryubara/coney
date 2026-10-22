// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coney::perf {

/// Coney's own profiling: named zones timed with a real clock, counters sampled once a frame, and a recorder that
/// keeps each frame's values for a benchmark report (docs/guides/performance.md). Debug tooling, not a mechanic: the
/// game never reads anything here, so it cannot change what a step does, and test mode stays lockstep.
///
/// Core reads no clock (docs/guides/conventions.md#update-and-render): the platform installs one with setClock()
/// (src/platform/perf_clock.cpp), and tests install a made-up one. With no clock, or while measuring is off, a zone
/// costs one branch. Zones and the recorder belong to the main thread; a counter's sampler may read values other
/// threads write (the allocation counter does, through atomics).

/// A clock: nanoseconds since some fixed moment.
using Clock = std::uint64_t (*)();

/// Installs the clock zones read (nullptr: none, zones then record nothing).
void setClock(Clock clock);

/// Whether zones measure. Off by default; `--bench` and the debug menus' Performance page turn it on.
[[nodiscard]] bool enabled();
/// Turns measuring on or off. Zones open when it changes record nothing.
void setEnabled(bool on);

/// The clock's time now, or 0 with no clock.
[[nodiscard]] std::uint64_t now();

/// A zone's name, registered once per call site; two sites with one name share one entry, so their times add up.
class ZoneSite {
  public:
    /// Registers `name` (a string with static storage, as the CONEY_PERF_ZONE macro passes).
    explicit ZoneSite(std::string_view name);
    /// The zone's index in the table.
    [[nodiscard]] std::size_t index() const { return m_index; }

  private:
    std::size_t m_index;
};

/// Adds `nanoseconds` to zone `index` in the frame being measured.
void addZoneTime(std::size_t index, std::uint64_t nanoseconds);

/// Times its own lifetime into a zone, when measuring is on as it starts.
class ScopedZone {
  public:
    /// Starts timing into `site`.
    explicit ScopedZone(const ZoneSite& site) : m_index(site.index()), m_on(enabled()), m_start(m_on ? now() : 0) {}
    /// Adds the time since the start.
    ~ScopedZone() {
        if (m_on) {
            addZoneTime(m_index, now() - m_start);
        }
    }
    ScopedZone(const ScopedZone&) = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;
    ScopedZone(ScopedZone&&) = delete;
    ScopedZone& operator=(ScopedZone&&) = delete;

  private:
    std::size_t m_index;
    bool m_on;
    std::uint64_t m_start;
};

/// The names of every zone registered so far, by index.
[[nodiscard]] std::vector<std::string> zoneNames();

/// Registers a counter: `sample` returns a running total (draw calls, bytes allocated), and each frame records how
/// much it grew. Registering a name again replaces its sampler. Returns the counter's index.
std::size_t addCounter(std::string_view name, std::function<std::uint64_t()> sample);
/// Removes every counter (a renderer that stops takes its own away).
void clearCounters();
/// The names of the counters, by index.
[[nodiscard]] std::vector<std::string> counterNames();

/// One frame's values: each zone's time in nanoseconds, each counter's growth, and the frame's own length (from the
/// end of the frame before; 0 for the first).
struct FrameRecord {
    std::uint64_t frameNanoseconds = 0;
    std::vector<std::uint64_t> zones;    ///< By zone index.
    std::vector<std::uint64_t> counters; ///< By counter index.
};

/// Ends the frame being measured: what its zones and counters gathered becomes its record (kept while recording, and
/// in the recent window the Performance page shows), and the next frame starts at zero. Called once a frame by the
/// main loop. Does nothing while measuring is off.
void endFrame();

/// Starts keeping every frame's record from the next endFrame() on, forgetting earlier ones; `reserve` frames are
/// made room for up front, so recording allocates nothing while it measures.
void startRecording(std::size_t reserve);
/// Stops keeping records.
void stopRecording();
/// The frames kept since startRecording(), copied out (for the summary and tests, after a run).
[[nodiscard]] std::vector<FrameRecord> recorded();

/// The mean of each zone and counter over the last frames (at most kRecentFrames), for the debug menus.
inline constexpr std::size_t kRecentFrames = 60;
/// A recent mean: the frame's length, then each zone and counter by index.
struct RecentMeans {
    double frameMilliseconds = 0.0;
    std::vector<double> zoneMilliseconds;
    std::vector<double> counters;
    std::size_t frames = 0; ///< How many frames the means cover.
};
/// The recent means.
[[nodiscard]] RecentMeans recentMeans();

/// The spread of one value over the recorded frames.
struct Spread {
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};
/// The spread of `values` (nearest-rank percentiles); zeros when there are none.
[[nodiscard]] Spread spreadOf(std::vector<double> values);

/// A benchmark's summary of the recorded frames: the frame time's spread, each zone's spread in milliseconds and
/// each counter's spread per frame, by name, in registration order.
struct Summary {
    std::size_t frames = 0;
    Spread frameMilliseconds;
    std::vector<std::pair<std::string, Spread>> zoneMilliseconds;
    std::vector<std::pair<std::string, Spread>> counters;
};
/// Summarises recorded() (the first record, which has no frame length, counts for its zones but not the frame time).
[[nodiscard]] Summary summarise();

/// The summary as a JSON object, with `meta` (what was measured and how: the build, the level, the fighters) as its
/// first member's string values.
[[nodiscard]] std::string toJson(const Summary& summary, const std::vector<std::pair<std::string, std::string>>& meta);

/// Forgets every zone time, counter, record and the enabled state; the names stay registered (call sites keep their
/// indices). For tests.
void resetForTests();

} // namespace coney::perf

#define CONEY_PERF_CONCAT_INNER(a, b) a##b
#define CONEY_PERF_CONCAT(a, b) CONEY_PERF_CONCAT_INNER(a, b)

/// Times the rest of the enclosing scope as the zone `name` (a string literal).
#define CONEY_PERF_ZONE(name)                                                                                          \
    static const ::coney::perf::ZoneSite CONEY_PERF_CONCAT(coneyPerfSite, __LINE__){name};                             \
    const ::coney::perf::ScopedZone CONEY_PERF_CONCAT(coneyPerfZone, __LINE__) {                                       \
        CONEY_PERF_CONCAT(coneyPerfSite, __LINE__)                                                                     \
    }
