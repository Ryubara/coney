// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/perf_bench.h"

#include <format>
#include <fstream>
#include <vector>

#include "core/options.h"
#include "core/perf.h"
#include "debug/entity_edit.h"
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

void PerfBench::makePlayerInvincible(debug::PlayControls* play) {
    // Player 1 is the inspector's first entity; the edit is applied at once, as the frame hook runs between steps.
    const debug::EntityInspect* inspect = play != nullptr ? play->inspect() : nullptr;
    debug::EntityEditTarget* edits = play != nullptr ? play->entityEdits() : nullptr;
    const std::vector<debug::EntityRef> entities =
        inspect != nullptr ? inspect->entities() : std::vector<debug::EntityRef>{};
    if (edits == nullptr || entities.empty()) {
        m_godError = "no player to make invincible";
        return;
    }
    const debug::EntityCommand command{
        .entity = entities.front().id, .op = debug::EditOp::Invincible, .values = {1.0, 0.0, 0.0}};
    if (auto applied = edits->applyEdit(command); !applied) {
        m_godError = applied.error().message;
    }
}

PerfBench::PerfBench(const Options& options)
    : m_file(options.benchFile.value_or(std::string{})), m_framesFile(options.benchFramesFile.value_or(std::string{})),
      m_fighters(options.benchFighters.value_or(0)), m_allies(options.benchAllies.value_or(0)), m_god(options.benchGod),
      m_spawnStep(static_cast<std::uint64_t>(options.benchSpawnStep.value_or(0))),
      m_measure(options.benchMeasure ? std::optional<std::uint64_t>(static_cast<std::uint64_t>(*options.benchMeasure))
                                     : std::nullopt),
      m_warmup(static_cast<std::uint64_t>(options.benchWarmup.value_or(kDefaultBenchWarmup))),
      m_frames(static_cast<std::uint64_t>(options.frameLimit.value_or(0))) {
    installPerfClock();
    addAllocationCounters();
    perf::setEnabled(true);
}

void PerfBench::playFrame(std::uint64_t frame, debug::PlayControls* play) {
    // The spawn on the first frame at which the steps of play asked for have run (frame 0 without a spawn step).
    if (!m_spawnFrame && m_steps >= m_spawnStep) {
        m_spawnFrame = frame;
        spawn(play);
    }
    // The frames from the end of the warm-up to the last are recorded (each record is made when its frame ends).
    if (m_spawnFrame && frame == *m_spawnFrame + m_warmup) {
        perf::startRecording(static_cast<std::size_t>(m_measure.value_or(m_frames) + 1));
    }
    // This frame is the last to measure: the run ends after it.
    if (m_measure && m_spawnFrame && frame + 1 >= *m_spawnFrame + m_warmup + *m_measure) {
        m_measured = true;
    }
}

void PerfBench::spawn(debug::PlayControls* play) {
    if (m_fighters > 0 || m_allies > 0) {
        if (play == nullptr || !play->canSpawnFighter()) {
            m_spawnError = "no play mode to spawn fighters in";
        } else {
            // One generator for both sides, enemies first, so a run without allies spawns the crowd it always did.
            debug::SpawnRandom random(kSpawnSeed);
            const debug::SpawnedFighters spawned =
                debug::spawnFighters(*play, kRandomModels, m_fighters, kFighterDistance, random);
            m_spawned = spawned.made;
            if (spawned.error) {
                m_spawnError = spawned.error->message;
            }
            if (m_allies > 0 && !spawned.error) {
                const debug::SpawnedFighters allies = debug::spawnFighters(
                    *play, kRandomModels, m_allies, kFighterDistance, random, ai::FighterSide::Ally);
                m_alliesSpawned = allies.made;
                if (allies.error) {
                    m_spawnError = allies.error->message;
                }
            }
        }
    }
    if (m_god) {
        makePlayerInvincible(play);
    }
}

std::expected<std::string, Error> PerfBench::finish(std::vector<std::pair<std::string, std::string>> meta) {
    perf::stopRecording();
    const perf::Summary summary = perf::summarise();
    meta.emplace_back("fighters_asked", std::to_string(m_fighters));
    meta.emplace_back("fighters_spawned", std::to_string(m_spawned));
    meta.emplace_back("allies_asked", std::to_string(m_allies));
    meta.emplace_back("allies_spawned", std::to_string(m_alliesSpawned));
    meta.emplace_back("player_god", m_god ? (m_godError.empty() ? "yes" : m_godError) : "no");
    if (!m_spawnError.empty()) {
        meta.emplace_back("spawn_error", m_spawnError);
    }
    meta.emplace_back("warmup_frames", std::to_string(m_warmup));
    meta.emplace_back("spawn_step", std::to_string(m_spawnStep));
    meta.emplace_back("spawn_frame", m_spawnFrame ? std::to_string(*m_spawnFrame) : std::string("none"));
    std::ofstream file(m_file, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected(Error{ErrorCode::Io, std::format("cannot write the bench report {}", m_file)});
    }
    file << perf::toJson(summary, meta);
    if (!file) {
        return std::unexpected(Error{ErrorCode::Io, std::format("cannot write the bench report {}", m_file)});
    }
    if (!m_framesFile.empty()) {
        std::ofstream frames(m_framesFile, std::ios::binary | std::ios::trunc);
        frames << perf::framesCsv(perf::recorded());
        if (!frames) {
            return std::unexpected(Error{ErrorCode::Io, std::format("cannot write the bench frames {}", m_framesFile)});
        }
    }
    return std::format("bench: {} fighters, {} frames: frame {:.2f} ms mean, {:.2f} p95, {:.2f} p99, {:.2f} max; "
                       "step {:.2f} ms, "
                       "render {:.2f} ms, present {:.2f} ms; {:.0f} draws, {:.0f} allocations a frame; report {}\n",
                       m_spawned + m_alliesSpawned, summary.frames, summary.frameMilliseconds.mean,
                       summary.frameMilliseconds.p95, summary.frameMilliseconds.p99, summary.frameMilliseconds.max,
                       meanOf(summary.zoneMilliseconds, "step"), meanOf(summary.zoneMilliseconds, "render"),
                       meanOf(summary.zoneMilliseconds, "present"), meanOf(summary.counters, "gl.draws"),
                       meanOf(summary.counters, "alloc.count"), m_file);
}

} // namespace coney::platform
