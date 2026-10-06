// SPDX-License-Identifier: GPL-3.0-or-later
#include "movies/captions.h"

#include <algorithm>
#include <format>
#include <utility>

#include "scenes/scene_record.h"

namespace coney::movies {

namespace {

// The scene event type that drives the captions (docs/research/scenes.md#events).
constexpr std::uint16_t kCaptionEvent = 41;
// The highest stored kind read as itself; above it a record reads as an ordinary caption.
constexpr std::uint32_t kHighestKind = 6;

} // namespace

CaptionKind captionKind(std::uint32_t stored) {
    return stored > kHighestKind ? CaptionKind::Ordinary : static_cast<CaptionKind>(stored);
}

std::expected<std::vector<CaptionRecord>, Error> parseSubtitles(std::span<const std::byte> chunk) {
    if (chunk.size() < 2) {
        return fail(ErrorCode::Truncated, "the Subtitles chunk has no length word");
    }
    const std::size_t length = static_cast<std::size_t>(chunk[0]) | (static_cast<std::size_t>(chunk[1]) << 8);
    if (length > chunk.size()) {
        return fail(ErrorCode::Truncated,
                    std::format("the Subtitles chunk says {} bytes but holds {}", length, chunk.size()));
    }
    std::vector<CaptionRecord> records;
    std::size_t pos = 2;
    // Records are read while one starts before length - 1.
    while (length >= 1 && pos < length - 1) {
        if (pos + 4 > chunk.size()) {
            return fail(ErrorCode::Truncated, std::format("a Subtitles record at byte {} has no kind", pos));
        }
        CaptionRecord record;
        for (std::size_t i = 0; i < 4; ++i) {
            record.kind |= static_cast<std::uint32_t>(chunk[pos + i]) << (8 * i);
        }
        pos += 4;
        // The string runs to its NUL, which must be inside the chunk.
        const auto text = chunk.subspan(pos);
        const auto end = std::ranges::find(text, std::byte{0});
        if (end == text.end()) {
            return fail(ErrorCode::Truncated, std::format("a Subtitles string at byte {} has no end", pos));
        }
        for (auto it = text.begin(); it != end; ++it) {
            record.text.push_back(static_cast<char>(*it));
        }
        pos += record.text.size() + 1;
        records.push_back(std::move(record));
    }
    return records;
}

Captions::Captions(std::vector<CaptionRecord> records, std::string_view language) : m_records(std::move(records)) {
    // Select the language's section: searches start after its marker.
    for (std::size_t i = 0; i < m_records.size(); ++i) {
        if (captionKind(m_records[i].kind) == CaptionKind::Language && m_records[i].text == language) {
            m_languageStart = i + 1;
            m_hasLanguage = true;
            break;
        }
    }
}

bool Captions::selectScene(std::string_view name) {
    if (!m_hasLanguage) {
        return false;
    }
    for (std::size_t i = m_languageStart; i < m_records.size(); ++i) {
        if (captionKind(m_records[i].kind) == CaptionKind::Scene && m_records[i].text == name) {
            m_cursor = i + 1;
            m_current.reset();
            m_kind = CaptionKind::Hidden;
            m_active = true;
            return true;
        }
    }
    return false;
}

void Captions::command(int command) {
    switch (command) {
    case 0:
        next();
        break;
    case 4:
        m_kind = CaptionKind::Hidden;
        break;
    case 5:
        m_kind = CaptionKind::Hidden5;
        break;
    case 6:
        m_flag = true;
        next();
        break;
    default:
        break;
    }
}

void Captions::next() {
    if (!m_active || m_cursor >= m_records.size()) {
        return;
    }
    m_current = m_cursor;
    m_kind = captionKind(m_records[m_cursor].kind);
    ++m_cursor;
}

const CaptionRecord* Captions::visible(bool subtitlesOn) const {
    if (!m_active || !m_current || m_kind == CaptionKind::Hidden || m_kind == CaptionKind::Hidden5) {
        return nullptr;
    }
    if (m_kind != CaptionKind::Emphasised && !subtitlesOn) {
        return nullptr;
    }
    return &m_records[*m_current];
}

CaptionStyle captionStyle(CaptionKind kind) {
    if (kind == CaptionKind::Emphasised) {
        return CaptionStyle{0.5F, 0.5F, 1.2F, graphics::Rgba{134, 26, 26, 255}, 0.9F};
    }
    return CaptionStyle{};
}

CaptionTimeline::CaptionTimeline(std::vector<Event> events) : m_events(std::move(events)) {
    std::ranges::stable_sort(m_events, {}, &Event::frame);
}

CaptionTimeline CaptionTimeline::fromScene(const scenes::SceneHeader& scene) {
    std::vector<Event> events;
    if (scene.tracks.camera) {
        for (const scenes::SceneEvent& event : scene.tracks.camera->events) {
            if (event.type == kCaptionEvent) {
                events.push_back(Event{event.frame, static_cast<int>(event.u32At(4))});
            }
        }
    }
    return CaptionTimeline(std::move(events));
}

std::vector<int> CaptionTimeline::advance(double seconds) {
    std::vector<int> fired;
    if (seconds - m_sceneTime < kAdvanceStep) {
        return fired;
    }
    m_sceneTime = seconds;
    const double frame = seconds * static_cast<double>(scenes::kSceneFrameRate);
    while (m_next < m_events.size() && static_cast<double>(m_events[m_next].frame) <= frame) {
        fired.push_back(m_events[m_next].command);
        ++m_next;
    }
    return fired;
}

} // namespace coney::movies
