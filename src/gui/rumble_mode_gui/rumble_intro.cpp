// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/rumble_mode_gui/rumble_intro.h"

#include <algorithm>
#include <format>
#include <utility>

#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// The fraction of `lengthMs` that has run from `startMs` to `nowMs`, clamped to [0, 1].
float progress(std::uint64_t startMs, std::uint64_t nowMs, std::uint64_t lengthMs) {
    if (nowMs <= startMs || lengthMs == 0) {
        return nowMs > startMs ? 1.0F : 0.0F;
    }
    return std::min(1.0F, static_cast<float>(nowMs - startMs) / static_cast<float>(lengthMs));
}

// The announcer's numbered line `stem_NN`.
std::string numbered(std::string_view stem, int number) { return std::format("{}_{:02}", stem, number); }

} // namespace

std::string_view RumbleIntro::string(std::uint32_t id) const { return m_strings != nullptr ? m_strings->get(id) : ""; }

void RumbleIntro::setupLine(TextWidget& text, float y, std::string_view value) {
    text.setup(TextWidgetSetup{.x = 0.5F,
                               .y = y,
                               .scale = 1.0F,
                               .colour = graphics::kWhite,
                               .alignment = TextAlignment::Centre,
                               .fontSlot = kBigFontSlot});
    text.setText(value);
}

int RumbleIntro::draw(int low, int high) { return m_random != nullptr ? m_random->range(low, high) : low; }

bool RumbleIntro::speak(std::string_view name, std::uint64_t nowMs) {
    std::uint64_t lengthMs = kStandInVoiceMs;
    if (m_sounds.playVoice) {
        const std::optional<std::uint64_t> length = m_sounds.playVoice(name);
        if (!length) {
            return false;
        }
        lengthMs = *length;
    }
    m_voiceEndsMs = nowMs + lengthMs;
    m_voices.emplace_back(name);
    return true;
}

void RumbleIntro::open(std::string_view onDone, std::span<const std::string> names, std::array<int, 2> packs,
                       std::uint64_t nowMs) {
    m_onDone = onDone;
    m_names.clear();
    for (const std::string& name : names) {
        if (!name.empty() && m_names.size() < kMaxNames) {
            m_names.push_back(name);
        }
    }
    m_packs = packs;
    m_voices.clear();
    m_shown = 0;
    m_separator = false;
    m_firstGeneral = 0;
    m_startLine.clear();
    m_startPlayed = false;
    m_holdsPad = false;
    m_namesHidden = false;
    m_lastMs = nowMs;

    // A line per name, stacked down the screen, and a separator between each two.
    m_nameTexts.clear();
    m_separatorTexts.clear();
    const float pitch = kSecondNameY - kFirstNameY;
    for (std::size_t i = 0; i < m_names.size(); ++i) {
        auto text = std::make_unique<TextWidget>();
        setupLine(*text, kFirstNameY + (pitch * static_cast<float>(i)), m_names[i]);
        m_nameTexts.push_back(std::move(text));
        if (i + 1 < m_names.size()) {
            auto separator = std::make_unique<TextWidget>();
            setupLine(*separator, kSeparatorY + (pitch * static_cast<float>(i)), kSeparatorText);
            m_separatorTexts.push_back(std::move(separator));
        }
    }
    setupLine(m_prompt, kPromptY, string(kPromptString));
    setupLine(m_countdown, kCountdownY, "");

    m_phase = RumbleIntroPhase::Names;
    if (m_names.empty()) {
        startPrompt(nowMs);
    } else {
        startName(0, nowMs);
    }
}

void RumbleIntro::startName(std::size_t index, std::uint64_t nowMs) {
    m_shown = index + 1;
    m_separator = false;
    m_nameStartMs = nowMs;
    // The gang's own line, by its pack; when that cannot play a general one, not the first name's again.
    const bool gangLine = index < m_packs.size() && speak(std::format("dj_gang_{}", m_packs.at(index) + 1), nowMs);
    if (!gangLine) {
        constexpr int kGeneralLines = 30;
        int line = draw(1, kGeneralLines);
        if (index > 0 && line == m_firstGeneral) {
            line = (line % kGeneralLines) + 1;
        }
        if (index == 0) {
            m_firstGeneral = line;
        }
        if (!speak(numbered("dj_genintro", line), nowMs)) {
            m_voiceEndsMs = nowMs;
        }
    }
    if (m_sounds.playSound) {
        m_sounds.playSound(index % 2 == 0 ? "rumblesynth_01" : "rumblesynth_02");
    }
}

void RumbleIntro::startPrompt(std::uint64_t nowMs) {
    m_phase = RumbleIntroPhase::Prompt;
    m_phaseMs = nowMs;
}

void RumbleIntro::update(const GuiFrame& frame) {
    const std::uint64_t nowMs = frame.timeMs;
    m_lastMs = nowMs;
    switch (m_phase) {
    case RumbleIntroPhase::Closed:
        return;
    case RumbleIntroPhase::Names:
        // The newest name fades up; when its line ends the separator speaks, then the next name, then the prompt.
        if (nowMs < m_voiceEndsMs) {
            break;
        }
        if (!m_separator && m_shown < m_names.size()) {
            m_separator = true;
            if (!speak(numbered("dj_vs", draw(1, 15)), nowMs)) {
                m_voiceEndsMs = nowMs;
            }
        } else if (m_separator) {
            startName(m_shown, nowMs);
        } else {
            startPrompt(nowMs);
        }
        break;
    case RumbleIntroPhase::Prompt: {
        // Once the prompt is nearly opaque the screen takes the pad and hides the names; accept moves on.
        const float fade = progress(m_phaseMs, nowMs, kPromptFadeMs);
        if (!m_holdsPad && fade >= kPromptTakeLevel) {
            m_holdsPad = true;
            m_namesHidden = true;
            m_input.focus(nowMs);
        }
        if (m_holdsPad && frame.pad != nullptr && m_input.dispatch(*frame.pad, nowMs) == MenuCommand::Accept) {
            if (!speak(numbered("dj_ready", draw(1, 5)), nowMs)) {
                m_voiceEndsMs = nowMs;
            }
            m_phase = RumbleIntroPhase::Ready;
        }
        break;
    }
    case RumbleIntroPhase::Ready:
        if (nowMs >= m_voiceEndsMs) {
            m_phase = RumbleIntroPhase::Countdown;
            m_phaseMs = nowMs;
            m_startLine = numbered("dj_start", draw(1, 10));
        }
        break;
    case RumbleIntroPhase::Countdown: {
        const std::uint64_t elapsed = nowMs - m_phaseMs;
        // The start line plays with the last word.
        if (!m_startPlayed && elapsed >= 3 * kCountdownStepMs) {
            m_startPlayed = true;
            (void)speak(m_startLine, nowMs);
        }
        if (elapsed >= kCountdownMs) {
            m_phase = RumbleIntroPhase::Closed;
            m_holdsPad = false;
            if (m_done) {
                m_done(m_onDone);
            }
            return;
        }
        break;
    }
    }

    // The texts' fades for this frame.
    for (std::size_t i = 0; i < m_nameTexts.size(); ++i) {
        m_nameTexts[i]->setFade(i + 1 == m_shown ? progress(m_nameStartMs, nowMs, kNameFadeMs) : 1.0F);
        m_nameTexts[i]->update(frame);
    }
    for (const std::unique_ptr<TextWidget>& separator : m_separatorTexts) {
        separator->update(frame);
    }
    m_prompt.setFade(m_phase == RumbleIntroPhase::Prompt ? progress(m_phaseMs, nowMs, kPromptFadeMs) : 1.0F);
    m_prompt.update(frame);
    if (m_phase == RumbleIntroPhase::Countdown) {
        const std::uint64_t elapsed = nowMs - m_phaseMs;
        m_countdown.setText(countdownWord(nowMs));
        m_countdown.setFade(1.0F - progress(0, elapsed % kCountdownStepMs, kCountdownStepMs));
        m_countdown.update(frame);
    }
}

std::string RumbleIntro::countdownWord(std::uint64_t nowMs) const {
    if (m_phase != RumbleIntroPhase::Countdown || nowMs < m_phaseMs) {
        return {};
    }
    const std::uint64_t step = (nowMs - m_phaseMs) / kCountdownStepMs;
    if (step >= 3) {
        return std::string(string(kGoString));
    }
    return std::to_string(3 - step);
}

void RumbleIntro::render(const GuiCanvas& canvas) const {
    if (!isOpen() || !visible()) {
        return;
    }
    if (!m_namesHidden) {
        for (std::size_t i = 0; i < m_shown && i < m_nameTexts.size(); ++i) {
            m_nameTexts[i]->render(canvas);
        }
        // Separator i shows once the line after name i + 1 has started.
        for (std::size_t i = 0; i < m_separatorTexts.size(); ++i) {
            if (i + 1 < m_shown || (i + 1 == m_shown && m_separator)) {
                m_separatorTexts[i]->render(canvas);
            }
        }
    }
    if (m_phase == RumbleIntroPhase::Prompt || m_phase == RumbleIntroPhase::Ready) {
        m_prompt.render(canvas);
    }
    if (m_phase == RumbleIntroPhase::Countdown) {
        m_countdown.render(canvas);
    }
}

} // namespace coney::gui
