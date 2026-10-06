// SPDX-License-Identifier: GPL-3.0-or-later
#include "warriors/tag_game.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
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

// The squared distance between a point and a cell.
float distanceSquared(float x, float y, TagCell cell) {
    const float dx = x - static_cast<float>(cell.x);
    const float dy = y - static_cast<float>(cell.y);
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
    : m_path(std::move(path)), m_tuning(tuning), m_chargeLeftMs(tuning.chargeMs) {
    if (m_path.empty()) {
        m_result = Result::Unfinished;
        return;
    }
    // The tag's painted fraction sets where on the path the player starts.
    const auto start = static_cast<std::size_t>(std::clamp(fraction, 0.0F, 1.0F) * static_cast<float>(m_path.size()));
    m_progress = std::min(start, m_path.size() - 1);
    m_cursorX = static_cast<float>(m_path[m_progress].x);
    m_cursorY = static_cast<float>(m_path[m_progress].y);
}

float TagGame::fraction() const {
    return m_path.empty()
               ? 0.0F
               : static_cast<float>(m_progress) / static_cast<float>(m_path.size() - 1 == 0 ? 1 : m_path.size() - 1);
}

bool TagGame::wastesCharge() const {
    return m_result == Result::Unfinished &&
           static_cast<float>(m_chargeLeftMs) < kWasteShare * static_cast<float>(m_tuning.chargeMs);
}

void TagGame::snapBack() {
    m_cursorX = static_cast<float>(m_path[m_progress].x);
    m_cursorY = static_cast<float>(m_path[m_progress].y);
    m_pauseLeftMs = m_tuning.pauseMs;
    m_offTrack = 0;
}

void TagGame::paint() {
    const TagCell cell{static_cast<int>(m_cursorX), static_cast<int>(m_cursorY)};
    const bool clear = std::ranges::all_of(m_painted, [cell](TagCell other) {
        const int dx = cell.x - other.x;
        const int dy = cell.y - other.y;
        return (dx * dx) + (dy * dy) >= kPaintSpacing * kPaintSpacing;
    });
    if (clear) {
        m_painted.push_back(cell);
    }
}

TagGame::Result TagGame::update(float stickX, float stickY, std::uint32_t elapsedMs, const SpendCharge& spend) {
    m_events = {};
    if (m_result != Result::Playing) {
        return m_result;
    }
    const std::uint32_t step = std::min(elapsedMs, kMaxStepMs);
    // A pause after a slip or a new charge holds everything.
    if (m_pauseLeftMs > 0) {
        m_pauseLeftMs = m_pauseLeftMs > step ? m_pauseLeftMs - step : 0;
        return m_result;
    }
    m_playedMs += step;
    // The charge in use runs down; when it is out the next is spent, or the game ends unfinished.
    if (step >= m_chargeLeftMs) {
        m_chargeLeftMs = 0;
        if (!spend || !spend()) {
            m_result = Result::Unfinished;
            return m_result;
        }
        m_events.chargeSpent = true;
        m_chargeLeftMs = m_tuning.chargeMs;
        snapBack();
        return m_result;
    }
    m_chargeLeftMs -= step;
    // The cursor follows the stick past the dead zone, its speed ramping up over the first 2 s; each cell far
    // enough from the painted ones is painted.
    const bool moving = std::hypot(stickX, stickY) > kDeadZone;
    if (moving) {
        const float ramp = std::min(1.0F, static_cast<float>(m_playedMs) / static_cast<float>(kRampMs));
        const float distance = static_cast<float>(step) * m_tuning.cellsPerMs * ramp;
        m_cursorX = std::clamp(m_cursorX + (stickX * distance), 0.0F, kGridMax);
        m_cursorY = std::clamp(m_cursorY + (stickY * distance), 0.0F, kGridMax);
        paint();
        if (m_painted.size() >= kMaxPoints) {
            m_result = Result::Unfinished;
            return m_result;
        }
    }
    // On track: the nearest path point within 6 cells and 4 points of the progress becomes the progress.
    const auto window = static_cast<std::ptrdiff_t>(kTrackWindow);
    const auto here = static_cast<std::ptrdiff_t>(m_progress);
    const std::ptrdiff_t first = std::max<std::ptrdiff_t>(0, here - window);
    const std::ptrdiff_t last = std::min<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(m_path.size()) - 1, here + window);
    std::optional<std::size_t> nearest;
    float best = static_cast<float>(kTrackCells * kTrackCells);
    for (std::ptrdiff_t i = first; i <= last; ++i) {
        const float d = distanceSquared(m_cursorX, m_cursorY, m_path[static_cast<std::size_t>(i)]);
        if (d <= best) {
            best = d;
            nearest = static_cast<std::size_t>(i);
        }
    }
    if (nearest) {
        m_progress = *nearest;
        m_offTrack = 0;
    } else if (moving && ++m_offTrack > kOffTrackUpdates) {
        m_events.slipped = true;
        snapBack();
    }
    if (m_progress + static_cast<std::size_t>(kFinishWindow) >= m_path.size() - 1) {
        m_result = Result::Finished;
    }
    return m_result;
}

} // namespace coney
