// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/text_layout.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>
#include <optional>
#include <system_error>
#include <utility>

#include "gui/markup.h"

namespace coney::gui {

namespace {

// `<DISPLAYTIME>` text fades out over its last second.
constexpr std::uint32_t kDisplayFadeMs = 1000;
// `<PULSE>` swings the colour between these factors.
constexpr float kPulseSwing = 0.5F;

// A whole number of milliseconds, or nothing.
std::optional<std::uint32_t> parseMs(std::string_view text) {
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// A decimal number such as `0.9` or `-1.5`, or nothing. Written by hand because floating-point from_chars is not in
// every standard library Coney builds with.
std::optional<float> parseDecimal(std::string_view text) {
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    float value = 0.0F;
    float scale = 1.0F;
    bool fraction = false;
    bool digits = false;
    for (const char c : text) {
        if (c == '.' && !fraction) {
            fraction = true;
        } else if (c >= '0' && c <= '9') {
            digits = true;
            if (fraction) {
                scale /= 10.0F;
                value += static_cast<float>(c - '0') * scale;
            } else {
                value = value * 10.0F + static_cast<float>(c - '0');
            }
        } else {
            return std::nullopt;
        }
    }
    if (!digits) {
        return std::nullopt;
    }
    return negative ? -value : value;
}

// `rrggbbaa` in hex, or nothing.
std::optional<graphics::Rgba> parseColour(std::string_view text) {
    std::uint32_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (text.size() != 8 || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return graphics::Rgba{static_cast<std::uint8_t>(value >> 24), static_cast<std::uint8_t>(value >> 16),
                          static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value)};
}

// A colour channel times a factor, kept in 0-255.
std::uint8_t scaleChannel(std::uint8_t channel, float factor) {
    return static_cast<std::uint8_t>(std::clamp(static_cast<float>(channel) * factor + 0.5F, 0.0F, 255.0F));
}

// A value with the values it replaced, so a closing tag can restore the previous one.
template <typename T> class Stacked {
  public:
    explicit Stacked(T initial) : m_values{std::move(initial)} {}
    void push(T value) { m_values.push_back(std::move(value)); }
    void pop() {
        if (m_values.size() > 1) {
            m_values.pop_back();
        }
    }
    [[nodiscard]] const T& current() const { return m_values.back(); }

  private:
    std::vector<T> m_values;
};

// Characters drawn with one font, size and colour, at an offset into their line.
struct Run {
    int slot;
    graphics::FontMetrics metrics;
    graphics::Rgba colour;
    std::string text;
    std::size_t line;
    float offset;
};

// One line: its measured width, its alignment and how far down the next line starts.
struct Line {
    float width = 0.0F;
    std::optional<TextAlignment> alignment;
    float advance = 0.0F;
};

// The state the tags change while a text is laid out, and the runs and lines measured so far.
class LayoutBuilder {
  public:
    LayoutBuilder(const TextStyle& style, const FontLookup& fonts, TextLayout& out)
        : m_style(style), m_fonts(fonts), m_out(out), m_slot(style.fontSlot), m_size(1.0F), m_colour(style.colour),
          m_pulse(0), m_alignment(style.alignment) {
        m_lines.emplace_back();
    }

    // The alpha factor of a text with `<DISPLAYTIME ms>`, applied to every colour.
    void setDisplayFactor(float factor) { m_displayFactor = factor; }

    // Adds characters with the current font, size and colour to the current line.
    void addText(std::string_view text) {
        if (text.empty()) {
            return;
        }
        const graphics::FontMetrics metrics = graphics::fontMetrics(m_style.scale * m_size.current());
        const graphics::Font* font = m_fonts ? m_fonts(m_slot.current()) : nullptr;
        Line& line = m_lines.back();
        if (!line.alignment) {
            line.alignment = m_alignment.current();
        }
        const std::uint32_t flags = m_style.proportional ? graphics::kFontProportional : 0;
        const float width = font != nullptr ? font->measure(text, metrics, flags) : 0.0F;
        m_runs.push_back(
            Run{m_slot.current(), metrics, effectiveColour(), std::string(text), m_lines.size() - 1, line.width});
        line.width += width;
    }

    // Applies one tag.
    void apply(MarkupTag tag, std::string_view argument) {
        if (const std::optional<std::uint8_t> character = markupCharacter(tag)) {
            const char c = static_cast<char>(*character);
            addText(std::string_view(&c, 1));
            return;
        }
        switch (tag) {
        case MarkupTag::Color:
            if (const auto colour = parseColour(argument)) {
                m_colour.push(*colour);
            } else {
                ++m_out.skippedTags;
            }
            break;
        case MarkupTag::Size:
            if (const auto size = parseDecimal(argument)) {
                m_size.push(*size);
            } else {
                ++m_out.skippedTags;
            }
            break;
        case MarkupTag::Pulse:
            m_pulse.push(parseMs(argument).value_or(0));
            break;
        case MarkupTag::Sound:
            m_out.sounds.emplace_back(argument);
            break;
        case MarkupTag::Freeze:
            m_out.freezeMs = std::max(m_out.freezeMs, parseMs(argument).value_or(0));
            break;
        case MarkupTag::DisplayTime:
            break; // read before the layout starts
        case MarkupTag::BigFont:
            m_slot.push(kBigFontSlot);
            break;
        case MarkupTag::Center:
        case MarkupTag::CCenter:
            m_alignment.push(TextAlignment::Centre);
            break;
        case MarkupTag::Right:
        case MarkupTag::RRight:
            m_alignment.push(TextAlignment::Right);
            break;
        case MarkupTag::Left:
            m_alignment.push(TextAlignment::Left);
            break;
        case MarkupTag::Cr:
        case MarkupTag::Cr2:
        case MarkupTag::Crm:
            newLine(0.0F);
            break;
        case MarkupTag::Cr3:
            newLine(parseDecimal(argument).value_or(0.0F));
            break;
        case MarkupTag::EndColor:
            m_colour.pop();
            break;
        case MarkupTag::EndSize:
            m_size.pop();
            break;
        case MarkupTag::EndPulse:
            m_pulse.pop();
            break;
        case MarkupTag::EndBigFont:
            m_slot.pop();
            break;
        case MarkupTag::EndCenter:
        case MarkupTag::EndRight:
        case MarkupTag::EndLeft:
            m_alignment.pop();
            break;
        default:
            ++m_out.skippedTags; // BOLD, MONEYFONT, BGFONT, AUTOINDENT, the sticks and their closing tags
            break;
        }
    }

    // Places the lines in the box and draws every run.
    void finish() {
        // The box grows to the widest line.
        float box = m_style.boxWidth;
        for (const Line& line : m_lines) {
            box = std::max(box, line.width);
        }
        m_out.width = box;
        m_out.lines = m_lines.size();
        // Each line's centre and left edge.
        std::vector<float> lineY;
        std::vector<float> lineX;
        float y = m_style.y;
        for (const Line& line : m_lines) {
            lineY.push_back(y);
            const TextAlignment alignment = line.alignment.value_or(m_style.alignment);
            float x = m_style.x;
            if (alignment == TextAlignment::Centre) {
                x += (box - line.width) / 2.0F;
            } else if (alignment == TextAlignment::Right) {
                x += box - line.width;
            }
            lineX.push_back(x);
            y += line.advance;
        }
        m_out.height = (y - m_style.y) + graphics::fontMetrics(m_style.scale * m_size.current()).height;
        // The runs, through the font's own drawing.
        const std::uint32_t flags = m_style.proportional ? graphics::kFontProportional : 0;
        std::vector<graphics::Sprite> sprites;
        for (const Run& run : m_runs) {
            const graphics::Font* font = m_fonts ? m_fonts(run.slot) : nullptr;
            if (font == nullptr) {
                continue;
            }
            sprites.clear();
            font->draw(sprites, run.text, lineX[run.line] + run.offset, lineY[run.line], run.metrics, flags, run.colour,
                       m_style.shadowAlpha);
            for (const graphics::Sprite& sprite : sprites) {
                m_out.sprites.push_back(TextSprite{run.slot, sprite});
            }
        }
    }

  private:
    // Ends the current line, moving down by h + lineGap of the current size plus `extra`.
    void newLine(float extra) {
        const graphics::FontMetrics metrics = graphics::fontMetrics(m_style.scale * m_size.current());
        m_lines.back().advance = metrics.height + metrics.lineGap + extra;
        m_lines.emplace_back();
    }

    // The colour in effect, with the pulse and the fades applied.
    [[nodiscard]] graphics::Rgba effectiveColour() const {
        graphics::Rgba colour = m_colour.current();
        if (const std::uint32_t period = m_pulse.current(); period != 0) {
            const double phase =
                2.0 * std::numbers::pi * static_cast<double>(m_style.timeMs % period) / static_cast<double>(period);
            const auto factor = static_cast<float>(1.0 + kPulseSwing * std::sin(phase));
            colour.r = scaleChannel(colour.r, factor);
            colour.g = scaleChannel(colour.g, factor);
            colour.b = scaleChannel(colour.b, factor);
        }
        colour.a = scaleChannel(colour.a, std::clamp(m_style.fade, 0.0F, 1.0F) * m_displayFactor);
        return colour;
    }

    const TextStyle& m_style;
    const FontLookup& m_fonts;
    TextLayout& m_out;
    Stacked<int> m_slot;
    Stacked<float> m_size;
    Stacked<graphics::Rgba> m_colour;
    Stacked<std::uint32_t> m_pulse;
    Stacked<TextAlignment> m_alignment;
    float m_displayFactor = 1.0F;
    std::vector<Run> m_runs;
    std::vector<Line> m_lines;
};

} // namespace

TextLayout layoutText(std::string_view text, const TextStyle& style, const FontLookup& fonts) {
    TextLayout layout;
    const std::vector<MarkupToken> tokens = parseMarkup(text);
    LayoutBuilder builder(style, fonts, layout);

    // `<DISPLAYTIME ms>` anywhere decides whether the text shows at all, and its fade-out.
    for (const MarkupToken& token : tokens) {
        if (token.kind != MarkupToken::Kind::Tag || token.tag != MarkupTag::DisplayTime) {
            continue;
        }
        if (const std::optional<std::uint32_t> end = parseMs(token.argument)) {
            if (style.timeMs >= *end) {
                layout.expired = true;
            } else if (*end - style.timeMs < kDisplayFadeMs) {
                builder.setDisplayFactor(static_cast<float>(*end - style.timeMs) / static_cast<float>(kDisplayFadeMs));
            }
        }
    }

    // Measure: runs and lines, tag by tag.
    for (const MarkupToken& token : tokens) {
        switch (token.kind) {
        case MarkupToken::Kind::Text:
            builder.addText(token.text);
            break;
        case MarkupToken::Kind::Tag:
            builder.apply(token.tag, token.argument);
            break;
        case MarkupToken::Kind::UnknownTag:
            ++layout.skippedTags;
            break;
        }
    }

    // Place and draw, unless the text has expired.
    builder.finish();
    if (layout.expired) {
        layout.sprites.clear();
    }
    return layout;
}

std::size_t addTextSprites(const TextLayout& layout, const std::function<graphics::SpriteBatch*(int slot)>& batches) {
    std::size_t dropped = 0;
    for (const TextSprite& sprite : layout.sprites) {
        graphics::SpriteBatch* batch = batches ? batches(sprite.fontSlot) : nullptr;
        if (batch != nullptr && !batch->addSprite(sprite.sprite)) {
            ++dropped;
        }
    }
    return dropped;
}

} // namespace coney::gui
