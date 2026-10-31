// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/jitter_buffer.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>

#include "net/snapshot.h"

namespace coney::net {

namespace {

// The distance from `a` to `b` in time units, signed: positive when `b` is later.
std::int64_t signedDistance(std::uint64_t a, std::uint64_t b) {
    return b >= a ? static_cast<std::int64_t>(b - a) : -static_cast<std::int64_t>(a - b);
}

// The magnitude of a signed distance.
std::uint64_t magnitude(std::int64_t distance) {
    return distance >= 0 ? static_cast<std::uint64_t>(distance) : static_cast<std::uint64_t>(-distance);
}

} // namespace

JitterBuffer::JitterBuffer(JitterConfig config) : m_config(config) {}

bool JitterBuffer::push(std::uint32_t step, const SnapshotHumans& humans) {
    if (!m_frames.empty() && step <= m_frames.rbegin()->first) {
        return false;
    }
    if (!m_frames.empty()) {
        m_intervalSteps = std::max<std::uint32_t>(1, step - m_frames.rbegin()->first);
    }
    m_frames.emplace(step, humans);
    return true;
}

void JitterBuffer::advance() {
    if (m_frames.empty()) {
        return;
    }
    // The first advance with a snapshot held starts the clock on its target.
    if (!m_started) {
        // Never before the oldest snapshot held, so the first samples have something to blend from.
        m_started = true;
        m_time = std::max(target(), std::uint64_t{m_frames.begin()->first} * kTimeUnitsPerStep);
        m_speedPercent = 100;
        prune();
        return;
    }

    // How far the target would be from where a plain step leaves the clock: positive when the clock is behind it.
    const std::uint64_t plain = m_time + kTimeUnitsPerStep;
    const std::int64_t error = signedDistance(plain, target());
    const std::uint64_t nudge = kTimeUnitsPerStep * m_config.nudgePercent / 100;
    const std::uint64_t tolerance = std::uint64_t{m_intervalSteps + 1} * kTimeUnitsPerStep;
    std::uint64_t next = plain;
    if (magnitude(error) > std::uint64_t{m_config.snapSteps} * kTimeUnitsPerStep) {
        // Far off (a long stall, a server that skipped ahead): jump rather than ease for seconds.
        next = target();
        m_correcting = false;
        ++m_snaps;
    } else {
        // Off by more than the snapshots' interval and a step, ease toward the target until within a nudge of it.
        if (!m_correcting && magnitude(error) > tolerance) {
            m_correcting = true;
        } else if (m_correcting && magnitude(error) < nudge) {
            m_correcting = false;
        }
        if (m_correcting) {
            next = error > 0 ? plain + nudge : plain - nudge;
        } else if (const std::uint64_t over = next % kTimeUnitsPerStep; over != 0) {
            // On target but between whole steps after easing: back onto the steps by at most a nudge, so the
            // snapshots' own steps are drawn exactly again.
            next = over < kTimeUnitsPerStep / 2 ? next - std::min(over, nudge)
                                                : next + std::min(kTimeUnitsPerStep - over, nudge);
        }
    }
    m_speedPercent = static_cast<std::uint32_t>((next - m_time) * 100 / kTimeUnitsPerStep);

    // Never past the newest snapshot: with nothing newer the clock holds there.
    const std::uint64_t newest = std::uint64_t{newestStep()} * kTimeUnitsPerStep;
    if (next > newest) {
        next = newest;
        ++m_starvedSteps;
    }
    m_time = std::max(next, m_time);
    prune();
}

std::optional<JitterSample> JitterBuffer::sample() const {
    if (!m_started || m_frames.empty()) {
        return std::nullopt;
    }
    // The newer end: the first snapshot at or after the time (the clock never passes the newest); the older, the one
    // before it, or the newer itself when there is none.
    const auto to = m_frames.lower_bound(static_cast<std::uint32_t>(m_time / kTimeUnitsPerStep));
    const auto newer = to->first * kTimeUnitsPerStep >= m_time ? to : std::next(to);
    const auto older = newer == m_frames.begin() ? newer : std::prev(newer);
    JitterSample sample{
        .fromStep = older->first, .toStep = newer->first, .from = &older->second, .to = &newer->second, .alpha = 1.0F};
    if (newer != older && newer->first * kTimeUnitsPerStep != m_time) {
        const std::uint64_t span = std::uint64_t{newer->first - older->first} * kTimeUnitsPerStep;
        const std::uint64_t into = m_time - (std::uint64_t{older->first} * kTimeUnitsPerStep);
        sample.alpha = static_cast<float>(static_cast<double>(into) / static_cast<double>(span));
    }
    return sample;
}

std::uint32_t JitterBuffer::newestStep() const { return m_frames.empty() ? 0 : m_frames.rbegin()->first; }

std::uint64_t JitterBuffer::delay() const {
    if (!m_started) {
        return 0;
    }
    const std::uint64_t newest = std::uint64_t{newestStep()} * kTimeUnitsPerStep;
    return newest >= m_time ? newest - m_time : 0;
}

bool JitterBuffer::settled() const {
    if (!m_started || m_correcting) {
        return false;
    }
    const std::uint64_t tolerance = std::uint64_t{m_intervalSteps + 1} * kTimeUnitsPerStep;
    return magnitude(signedDistance(m_time, target())) <= tolerance;
}

std::uint64_t JitterBuffer::target() const {
    const std::uint32_t newest = newestStep();
    return newest >= m_config.delaySteps ? std::uint64_t{newest - m_config.delaySteps} * kTimeUnitsPerStep : 0;
}

void JitterBuffer::prune() {
    // Keep the snapshot the time blends from: the last one before the first at or after the time.
    const auto newer =
        m_frames.lower_bound(static_cast<std::uint32_t>((m_time + kTimeUnitsPerStep - 1) / kTimeUnitsPerStep));
    if (newer == m_frames.begin() || newer == m_frames.end()) {
        if (newer == m_frames.end() && m_frames.size() > 1) {
            m_frames.erase(m_frames.begin(), std::prev(m_frames.end()));
        }
        return;
    }
    m_frames.erase(m_frames.begin(), std::prev(newer));
}

} // namespace coney::net
