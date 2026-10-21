// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/perf_bench.h"

#include <format>
#include <fstream>

#include "core/options.h"
#include "core/perf.h"
#include "debug/fighter_spawn.h"
#include "debug/play_controls.h"
#include "platform/perf_probes.h"

namespace coney::platform {

namespace {

// The distance ahead of the player the first row of fighters stands at, as the AI fighters page's default.
constexpr float kFighterDistance = 4.0F;
// The model choice: Random, so the fighters are drawn with many models and textures, as a crowd is.
constexpr std::size_t kRandomModels = 0;
// The fighters' model picks are seeded, so every run spawns the same crowd.
constexpr std::uint32_t kSpawnSeed = 0x5eed5eedU;

// A zone's or counter's mean from the summary, or 0 when it never ran.
double meanOf(const std::vector<std::pair<std::string, perf::Spread>>& values, std::string_view name) {
    for (const auto& [key, spread] : values) {
        if (key == name) {
            return spread.mean;
        }
    }
    return 0.0;
}

} // namespace

PerfBench::PerfBench(const Options& options)
    : m_file(options.benchFile.value_or(std::string{})), m_fighters(options.benchFighters.value_or(0)),
      m_warmup(static_cast<std::uint64_t>(options.benchWarmup.value_or(kDefaultBenchWarmup))),
      m_frames(static_cast<std::uint64_t>(options.frameLimit.value_or(0))) {
    installPerfClock();
    addAllocationCounters();
    perf::setEnabled(true);
}

void PerfBench::playFrame(std::uint64_t frame, debug::PlayControls* play) {
    if (frame == 0 && m_fighters > 0) {
        if (play == nullptr || !play->canSpawnFighter()) {
            m_spawnError = "no play mode to spawn fighters in";
        } else {
            debug::SpawnRandom random(kSpawnSeed);
            const debug::SpawnedFighters spawned =
                debug::spawnFighters(*play, kRandomModels, m_fighters, kFighterDistance, random);
            m_spawned = spawned.made;
            if (spawned.error) {
                m_spawnError = spawned.error->message;
            }
        }
    }
    // The frames from the end of the warm-up to the last are recorded (each record is made when its frame ends).
    if (frame == m_warmup) {
        perf::startRecording(static_cast<std::size_t>(m_frames - m_warmup + 1));
    }
}

std::expected<std::string, Error> PerfBench::finish(std::vector<std::pair<std::string, std::string>> meta) {
    perf::stopRecording();
    const perf::Summary summary = perf::summarise();
    meta.emplace_back("fighters_asked", std::to_string(m_fighters));
    meta.emplace_back("fighters_spawned", std::to_string(m_spawned));
    if (!m_spawnError.empty()) {
        meta.emplace_back("spawn_error", m_spawnError);
    }
    meta.emplace_back("warmup_frames", std::to_string(m_warmup));
    std::ofstream file(m_file, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected(Error{ErrorCode::Io, std::format("cannot write the bench report {}", m_file)});
    }
    file << perf::toJson(summary, meta);
    if (!file) {
        return std::unexpected(Error{ErrorCode::Io, std::format("cannot write the bench report {}", m_file)});
    }
    return std::format("bench: {} fighters, {} frames: frame {:.2f} ms mean, {:.2f} p95, {:.2f} p99; step {:.2f} ms, "
                       "render {:.2f} ms, present {:.2f} ms; {:.0f} draws, {:.0f} allocations a frame; report {}\n",
                       m_spawned, summary.frames, summary.frameMilliseconds.mean, summary.frameMilliseconds.p95,
                       summary.frameMilliseconds.p99, meanOf(summary.zoneMilliseconds, "step"),
                       meanOf(summary.zoneMilliseconds, "render"), meanOf(summary.zoneMilliseconds, "present"),
                       meanOf(summary.counters, "gl.draws"), meanOf(summary.counters, "alloc.count"), m_file);
}

} // namespace coney::platform
