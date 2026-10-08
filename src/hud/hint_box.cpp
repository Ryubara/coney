// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/hint_box.h"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>
#include <vector>

#include "core/event_log.h"
#include "gui/markup.h"
#include "hud/hud_layout.h"

namespace coney::hud {

namespace {

// `text` split at spaces that are not inside a tag.
std::vector<std::string> wordsOf(std::string_view text) {
    std::vector<std::string> words;
    std::string word;
    bool inTag = false;
    for (const char c : text) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        }
        if (c == ' ' && !inTag) {
            if (!word.empty()) {
                words.push_back(std::move(word));
                word.clear();
            }
            continue;
        }
        word.push_back(c);
    }
    if (!word.empty()) {
        words.push_back(std::move(word));
    }
    return words;
}

// A tag's argument as whole milliseconds, or none.
std::optional<std::uint32_t> millisecondsOf(std::string_view argument) {
    // A copy, so that the parse is given a bounded, owned buffer.
    const std::string digits(argument);
    std::uint32_t ms = 0;
    const auto result = std::from_chars(digits.c_str(), digits.c_str() + digits.size(), ms);
    if (digits.empty() || result.ec != std::errc{}) {
        return std::nullopt;
    }
    return ms;
}

} // namespace

MarkupTimes markupTimesOf(std::string_view text) {
    MarkupTimes times;
    for (const gui::MarkupToken& token : gui::parseMarkup(text)) {
        if (token.kind != gui::MarkupToken::Kind::Tag) {
            continue;
        }
        if (token.tag == gui::MarkupTag::DisplayTime && !times.displayMs) {
            times.displayMs = millisecondsOf(token.argument);
        } else if (token.tag == gui::MarkupTag::Freeze && !times.freezeMs) {
            times.freezeMs = millisecondsOf(token.argument);
        }
    }
    return times;
}

// The width of the last line of marked-up `line` as the original's word wrap measures it: the text after the last
// line break, with every tag stripped (a button icon counts for nothing).
float strippedWidth(std::string_view line, const gui::TextStyle& style, const gui::FontLookup& fonts) {
    std::string plain;
    for (const gui::MarkupToken& token : gui::parseMarkup(line)) {
        if (token.kind == gui::MarkupToken::Kind::Text) {
            plain += token.text;
        } else if (token.kind == gui::MarkupToken::Kind::Tag &&
                   (token.tag == gui::MarkupTag::Cr || token.tag == gui::MarkupTag::Cr2 ||
                    token.tag == gui::MarkupTag::Cr3 || token.tag == gui::MarkupTag::Crm)) {
            plain.clear();
        }
    }
    return gui::layoutText(plain, style, fonts).width;
}

std::string wrapText(std::string_view text, const gui::TextStyle& style, const gui::FontLookup& fonts, float width) {
    // A leading <AUTOINDENT f> gives the wrap width instead.
    if (const std::vector<gui::MarkupToken> tokens = gui::parseMarkup(text);
        !tokens.empty() && tokens.front().kind == gui::MarkupToken::Kind::Tag &&
        tokens.front().tag == gui::MarkupTag::AutoIndent) {
        float autoWidth = 0.0F;
        const std::string_view argument = tokens.front().argument;
        if (const auto [end, error] = std::from_chars(argument.data(), argument.data() + argument.size(), autoWidth);
            error == std::errc{} && autoWidth > 0.0F) {
            width = autoWidth;
        }
    }
    std::string done;
    std::string line;
    for (const std::string& word : wordsOf(text)) {
        std::string candidate = line;
        if (!candidate.empty()) {
            candidate += ' ';
        }
        candidate += word;
        if (!line.empty() && strippedWidth(candidate, style, fonts) > width) {
            done += line + "<CR>";
            line = word;
        } else {
            line = std::move(candidate);
        }
    }
    return done + line;
}

void HintBox::insert(Hint hint) {
    const auto later = std::ranges::find_if(m_queue, [&hint](const Hint& h) { return h.priority > hint.priority; });
    m_queue.insert(later, std::move(hint));
}

void HintBox::queue(std::string text, int priority) {
    if (m_queue.size() >= kHintQueueSlots) {
        return;
    }
    insert(Hint{std::move(text), priority});
    // A more urgent hint interrupts: the showing one goes back with its own priority and will start again.
    if (m_showing && priority < m_showing->priority) {
        Hint interrupted = std::move(*m_showing);
        m_showing.reset();
        if (m_queue.size() < kHintQueueSlots) {
            insert(std::move(interrupted));
        }
    }
}

void HintBox::flush(int priority) {
    const auto matches = [priority](const Hint& hint) {
        return priority >= kHintFlushAll || hint.priority == priority;
    };
    std::erase_if(m_queue, matches);
    if (m_showing && matches(*m_showing)) {
        m_showing.reset();
    }
}

bool HintBox::contains(std::string_view text) const {
    if (m_showing && m_showing->text == text) {
        return true;
    }
    return std::ranges::any_of(m_queue, [text](const Hint& hint) { return hint.text == text; });
}

void HintBox::withdraw(std::string_view text) {
    if (m_showing && m_showing->text == text) {
        m_showing.reset();
        return;
    }
    const auto found = std::ranges::find_if(m_queue, [text](const Hint& hint) { return hint.text == text; });
    if (found != m_queue.end()) {
        m_queue.erase(found);
    }
}

void HintBox::update(std::uint64_t nowMs, const HudSound& audio) {
    m_nowMs = nowMs;
    // 1. The showing hint ends only when its markup's time is over.
    if (m_showing) {
        std::optional<std::uint32_t> endMs = m_times.displayMs;
        if (m_times.freezeMs) {
            endMs = std::max(endMs.value_or(0), std::max(*m_times.freezeMs, kHintFreezeMinMs));
        }
        if (endMs && nowMs - m_shownAtMs >= *endMs) {
            m_showing.reset();
        }
    }
    // 2. When free, the front of the queue shows, with its cue.
    if (!m_showing && !m_queue.empty()) {
        m_showing = std::move(m_queue.front());
        m_queue.pop_front();
        m_times = markupTimesOf(m_showing->text);
        m_shownAtMs = nowMs;
        audio.playCue(kCueHint);
        // The event log (`--event-log`): a hint as the player starts to see it.
        events::emit("hint", m_showing->text);
    }
}

float HintBox::fade() const {
    if (!m_showing) {
        return 0.0F;
    }
    if (!m_times.displayMs) {
        return 1.0F;
    }
    const std::uint64_t age = m_nowMs - m_shownAtMs;
    if (age >= *m_times.displayMs) {
        return 0.0F;
    }
    const std::uint64_t remaining = *m_times.displayMs - age;
    return remaining >= kHintDisplayFadeMs ? 1.0F
                                           : static_cast<float>(remaining) / static_cast<float>(kHintDisplayFadeMs);
}

gui::TextStyle HintBox::textStyle() {
    gui::TextStyle style;
    style.x = kHintTextX;
    style.scale = metricsOfHeight(kMessageTextHeight).width * 30.0F;
    style.colour = kHintTextColour;
    style.shadowAlpha = 128;
    return style;
}

const std::string& HintBox::wrapped(const gui::TextStyle& style, const gui::FontLookup& fonts) const {
    if (m_showing && m_wrappedFrom != m_showing->text) {
        m_wrappedFrom = m_showing->text;
        m_wrapped = wrapText(m_showing->text, style, fonts, kHintWrapWidth);
    }
    return m_wrapped;
}

float HintBox::boxHeight(const gui::FontLookup& fonts) const {
    if (!m_showing || !fonts) {
        return 0.0F;
    }
    const gui::TextStyle style = textStyle();
    return gui::layoutText(wrapped(style, fonts), style, fonts).height + kHintBoxExtra.height;
}

void HintBox::render(const HudCanvas& canvas) const {
    if (!m_showing || !canvas.text.fonts) {
        return;
    }
    // The text's own `<DISPLAYTIME>` fades its glyphs, timed from when it showed; the box follows at 125 x that alpha.
    gui::TextStyle style = textStyle();
    style.timeMs = static_cast<std::uint32_t>(shownMs());
    const std::string& text = wrapped(style, canvas.text.fonts);
    // Lay out once to learn the text's size, then place it so that its bottom sits the box's extra height above the
    // bottom; the box is the text's rectangle grown by the table's offset and size, drawn first.
    const gui::TextLayout measured = gui::layoutText(text, style, canvas.text.fonts);
    const float lineHeight = metricsOfHeight(kMessageTextHeight).height;
    const float textBottom = kHintBottom - kHintBoxExtra.height;
    const float textTop = textBottom - measured.height;
    style.y = textTop + lineHeight / 2.0F;
    if (canvas.flat != nullptr) {
        const float boxX = style.x + kHintBoxOffset.x;
        const float boxY = textTop + kHintBoxOffset.y;
        const float boxW = measured.width + kHintBoxExtra.width;
        const float boxH = measured.height + kHintBoxExtra.height;
        graphics::Rgba colour = kHintBoxColour;
        colour.a = static_cast<std::uint8_t>(static_cast<float>(kHintBoxDrawAlpha) * fade());
        canvas.flat->addSprite(
            guiSprite(boxX + boxW / 2.0F, boxY + boxH / 2.0F, boxW, boxH, graphics::UvRect{}, colour));
    }
    gui::addTextSprites(gui::layoutText(text, style, canvas.text.fonts), canvas.text.textBatch);
}

} // namespace coney::hud
