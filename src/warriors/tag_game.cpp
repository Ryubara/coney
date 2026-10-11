// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/tag_game.h"

#include <algorithm>
#include <array>
#include <utility>

namespace coney {

namespace {

// The table at 0x00510918 (difficulties 1-3) and HuTagDifficulty's defaults.
constexpr std::array<TagTuning, 3> kTunings{TagTuning{.chargeMs = 13000, .pauseMs = 500, .cellsPerMs = 0.05F},
                                            TagTuning{.chargeMs = 11000, .pauseMs = 600, .cellsPerMs = 0.07F},
                                            TagTuning{.chargeMs = 9500, .pauseMs = 500, .cellsPerMs = 0.073F}};
// The grid's last cell.
constexpr float kGridMax = 255.0F;
// A sample joins the path when its squared distance from the last kept is more than this.
constexpr int kMinSpacingSquared = 6;

// One coordinate of the uniform Catmull-Rom segment from b to c (a before, d after) at t.
float catmullRom(float a, float b, float c, float d, float t) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return (a * ((-0.5F * t3) + t2 - (0.5F * t))) + (b * ((1.5F * t3) - (2.5F * t2) + 1.0F)) +
           (c * ((-1.5F * t3) + (2.0F * t2) + (0.5F * t))) + (d * ((0.5F * t3) - (0.5F * t2)));
}

// The squared distance between two cells.
int distanceSquared(TagCell a, TagCell b) {
    const int dx = a.x - b.x;
    const int dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

} // namespace

TagTuning tagTuning(int difficulty) {
    const int index = difficulty >= 1 && difficulty <= 3 ? difficulty - 1 : 0;
    return kTunings.at(static_cast<std::size_t>(index));
}

TagTuning defaultTagTuning() { return TagTuning{}; }

std::vector<TagCell> tagPath(std::span<const float> pattern, std::size_t count) {
    std::vector<TagCell> path;
    const std::size_t points = std::min(count, pattern.size() / 2);
    if (points == 0) {
        return path;
    }
    // The point at `i`, clamped to the first and last.
    const auto point = [&pattern, points](std::ptrdiff_t i) {
        const auto at =
            static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(i, 0, static_cast<std::ptrdiff_t>(points) - 1));
        return std::pair{pattern[2 * at], pattern[(2 * at) + 1]};
    };
    const std::size_t perSegment = TagGame::kMaxPoints / points;
    TagCell last{};
    for (std::size_t i = 0; i < points && path.size() < TagGame::kMaxPoints; ++i) {
        const auto s = static_cast<std::ptrdiff_t>(i);
        const auto [ax, ay] = point(s - 1);
        const auto [bx, by] = point(s);
        const auto [cx, cy] = point(s + 1);
        const auto [dx, dy] = point(s + 2);
        for (std::size_t k = 0; k < perSegment && path.size() < TagGame::kMaxPoints; ++k) {
            const float t = static_cast<float>(k * points) / static_cast<float>(TagGame::kMaxPoints);
            // Truncated, the low byte kept: a grid cell.
            const TagCell cell{static_cast<int>(static_cast<std::int32_t>(catmullRom(ax, bx, cx, dx, t)) & 0xff),
                               static_cast<int>(static_cast<std::int32_t>(catmullRom(ay, by, cy, dy, t)) & 0xff)};
            const int ex = cell.x - last.x;
            const int ey = cell.y - last.y;
            if ((ex * ex) + (ey * ey) > kMinSpacingSquared) {
                path.push_back(cell);
                last = cell;
            }
        }
    }
    return path;
}

TagGame::TagGame(std::vector<TagCell> path, float fraction, TagTuning tuning)
    : m_path(std::move(path)), m_tuning(tuning) {
    if (m_path.empty()) {
        m_result = Result::Unfinished;
        return;
    }
    // The tag's painted fraction sets where on the path the player starts, and the path's points before it show as
    // already painted.
    const auto start = static_cast<std::size_t>(std::clamp(fraction, 0.0F, 1.0F) * static_cast<float>(m_path.size()));
    m_progress = std::min(start, m_path.size() - 1);
    m_painted.assign(m_path.begin(), m_path.begin() + static_cast<std::ptrdiff_t>(m_progress));
    m_cursorX = static_cast<float>(m_path[m_progress].x);
    m_cursorY = static_cast<float>(m_path[m_progress].y);
}

float TagGame::fraction() const {
    if (m_result == Result::Finished) {
        return 1.0F;
    }
    return m_path.empty() ? 0.0F : static_cast<float>(m_progress) / static_cast<float>(m_path.size());
}

std::uint32_t TagGame::chargeLeftMs() const {
    const std::uint64_t used = m_started ? m_clockMs - m_chargeStartMs : 0;
    return used >= m_tuning.chargeMs ? 0 : static_cast<std::uint32_t>(m_tuning.chargeMs - used);
}

std::uint32_t TagGame::pauseLeftMs() const {
    return m_pauseUntilMs > m_clockMs ? static_cast<std::uint32_t>(m_pauseUntilMs - m_clockMs) : 0;
}

float TagGame::chargeLeft() const {
    return m_tuning.chargeMs == 0 ? 0.0F : static_cast<float>(chargeLeftMs()) / static_cast<float>(m_tuning.chargeMs);
}

bool TagGame::wastesCharge() const {
    return m_result == Result::Unfinished &&
           static_cast<float>(chargeLeftMs()) < kWasteShare * static_cast<float>(m_tuning.chargeMs);
}

void TagGame::snapBack() {
    m_cursorX = static_cast<float>(m_path[m_progress].x);
    m_cursorY = static_cast<float>(m_path[m_progress].y);
    m_offTrack = 0;
    m_pauseUntilMs = m_clockMs + m_tuning.pauseMs;
}

bool TagGame::paint() {
    const TagCell cell{static_cast<int>(m_cursorX), static_cast<int>(m_cursorY)};
    const bool clear = std::ranges::all_of(
        m_painted, [cell](TagCell other) { return distanceSquared(cell, other) >= kPaintSpacing * kPaintSpacing; });
    if (!clear) {
        return true;
    }
    if (m_painted.size() >= kMaxPoints) {
        return false;
    }
    m_painted.push_back(cell);
    return true;
}

void TagGame::spendPaint(const SpendCharge& spend) {
    // The first update stamps the charge's start and the ramp's; the first charge takes no can.
    if (!m_started) {
        m_started = true;
        m_chargeStartMs = m_clockMs;
        m_rampStartMs = m_clockMs;
        return;
    }
    if (m_clockMs - m_chargeStartMs < m_tuning.chargeMs) {
        return;
    }
    // A new charge: back on the progress, the count cleared, a pause, the ramp again from the pause's end; then a can
    // is taken, and with none the game is over.
    m_chargeStartMs = m_clockMs;
    snapBack();
    m_rampStartMs = m_pauseUntilMs;
    m_events.chargeSpent = true;
    if (!spend || !spend()) {
        m_result = Result::Unfinished;
    }
}

void TagGame::track(bool moving) {
    const TagCell cell{static_cast<int>(m_cursorX), static_cast<int>(m_cursorY)};
    const auto near = [this, cell](std::size_t i) { return distanceSquared(cell, m_path[i]) < kTrackDistanceSquared; };
    const auto window = static_cast<std::size_t>(kTrackWindow);
    // Near a point behind: on track, and the progress never goes back.
    for (std::size_t i = m_progress > window ? m_progress - window : 0; i < m_progress; ++i) {
        if (near(i)) {
            m_onTrack = true;
            return;
        }
    }
    // The first near point from the progress on (not the nearest): the progress moves there.
    const std::size_t last = std::min(m_progress + window, m_path.size());
    for (std::size_t i = m_progress; i < last; ++i) {
        if (near(i)) {
            m_onTrack = true;
            m_progress = i;
            return;
        }
    }
    m_onTrack = false;
    if (moving) {
        ++m_offTrack;
    }
}

TagGame::Result TagGame::update(float stickX, float stickY, std::uint32_t elapsedMs, const SpendCharge& spend) {
    m_events = {};
    if (m_result != Result::Playing) {
        return m_result;
    }
    m_clockMs += elapsedMs;
    // 1. The paint, in every update: the charge drains through pauses too.
    spendPaint(spend);
    if (m_result != Result::Playing || m_clockMs < m_pauseUntilMs) {
        return m_result;
    }
    // 2. The cursor follows the stick past the dead zone, by the time since the last unpaused update (at most 30 ms)
    // and the speed ramping up over 2 s; each cell far enough from the painted ones is painted.
    const auto step = static_cast<std::uint32_t>(std::min<std::uint64_t>(m_clockMs - m_lastPlayedMs, kMaxStepMs));
    m_lastPlayedMs = m_clockMs;
    const bool moving = (stickX * stickX) + (stickY * stickY) >= kDeadZoneSquared;
    if (moving) {
        const float sinceRamp = static_cast<float>(m_clockMs - std::min(m_clockMs, m_rampStartMs));
        const float ramp = std::min(1.0F, sinceRamp / static_cast<float>(kRampMs));
        const float distance = static_cast<float>(step) * m_tuning.cellsPerMs * ramp;
        m_cursorX = std::clamp(m_cursorX + (stickX * distance), 0.0F, kGridMax);
        m_cursorY = std::clamp(m_cursorY + (stickY * distance), 0.0F, kGridMax);
        if (!paint()) {
            m_result = Result::Unfinished;
            return m_result;
        }
        // 3. Five off-track moving updates since the last slip or charge: this one slips, before tracking, so the
        // on-track flag keeps its last value (off track) through the pause.
        if (m_offTrack > kOffTrackUpdates) {
            m_events.slipped = true;
            snapBack();
            return m_result;
        }
    }
    // 4. Tracking, and the finish at the third point from the end.
    track(moving);
    if (m_path.size() < m_progress + static_cast<std::size_t>(kFinishWindow)) {
        m_result = Result::Finished;
    }
    return m_result;
}

} // namespace coney
