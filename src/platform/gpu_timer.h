// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace coney::platform {

/// The GPU's time for a frame, from OpenGL timer queries (docs/guides/performance.md#gpu-time): debug tooling for the
/// bench's `gpu.frame` measure. The game never reads it, and a machine whose driver has no timer queries simply has no
/// measure; nothing here can fail a run.

/// The calls a timer query takes, as a table so tests can stand in for a driver. `openGlTimerApi()` fills it with
/// OpenGL's; an empty table (a call missing) makes the timer unavailable.
struct GpuTimerApi {
    std::function<void(std::size_t count, std::uint32_t* ids)> generate;      ///< glGenQueries
    std::function<void(std::size_t count, const std::uint32_t* ids)> destroy; ///< glDeleteQueries
    std::function<void(std::uint32_t id)> begin;                              ///< glBeginQuery(GL_TIME_ELAPSED, id)
    std::function<void()> end;                                                ///< glEndQuery(GL_TIME_ELAPSED)
    std::function<bool(std::uint32_t id)> ready; ///< GL_QUERY_RESULT_AVAILABLE: the GPU has finished the query
    std::function<std::uint64_t(std::uint32_t id)> result; ///< GL_QUERY_RESULT: the time in nanoseconds

    /// Whether every call is present.
    [[nodiscard]] bool complete() const { return generate && destroy && begin && end && ready && result; }
};

/// OpenGL's timer-query calls from the loader librw ran; the table is incomplete (so the timer is unavailable) when the
/// context lacks them (older than OpenGL 3.3) or there is none.
[[nodiscard]] GpuTimerApi openGlTimerApi();

/// Times the GPU's work between begin() and end() with a small ring of queries. A query's result is ready some frames
/// after its end, and reading it early would stall the pipeline, so takeResult() only reads what is ready, oldest
/// first; a frame that finds every query still in flight is not measured (skipped() counts them) rather than waited
/// for. OpenGL allows one timer query open at a time, so begin() and end() do not nest.
class GpuTimer {
  public:
    /// How many frames can be in flight before one is dropped.
    static constexpr std::size_t kQueries = 4;

    /// Makes the queries through `api`; unavailable when the table is incomplete.
    explicit GpuTimer(GpuTimerApi api);
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;
    GpuTimer(GpuTimer&&) = delete;
    GpuTimer& operator=(GpuTimer&&) = delete;

    /// Whether the driver has timer queries.
    [[nodiscard]] bool available() const { return m_api.complete(); }

    /// Starts timing a frame, unless every query is in flight or one is already open (then the frame is not measured).
    void begin();
    /// Ends the frame begin() started; nothing when it did not start one.
    void end();
    /// The time of the oldest finished frame in nanoseconds, once the GPU is done with it; nullopt when it is not or
    /// there is none. Call it until it returns nullopt to read everything that is ready.
    [[nodiscard]] std::optional<std::uint64_t> takeResult();

    /// How many frames were not measured because the GPU was too far behind.
    [[nodiscard]] std::uint64_t skipped() const { return m_skipped; }

  private:
    GpuTimerApi m_api;
    std::array<std::uint32_t, kQueries> m_ids{};
    std::size_t m_head = 0;    // the slot the next begin() uses
    std::size_t m_pending = 0; // slots begun and ended, not yet read, from the one before m_head backwards
    bool m_open = false;       // a query is between begin() and end()
    std::uint64_t m_skipped = 0;
};

} // namespace coney::platform
