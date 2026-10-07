// SPDX-License-Identifier: GPL-3.0-or-later
#include "movies/movie_mode.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "core/pads.h"
#include "gamemodes/game_mode_stack.h"
#include "graphics/screen.h"

namespace coney::movies {

namespace {

// Sprites a caption can take: a few lines of text.
constexpr std::size_t kCaptionSprites = 512;
// The caption batch's depth in the 2D pass; there is nothing else to sort against.
constexpr float kCaptionDepth = 9000.0F;
// The shadow under each caption glyph (docs/research/movies.md#caption-drawing).
constexpr std::uint8_t kCaptionShadow = 128;

// Splits `text` into lines no wider than `width` (GUI units) in `font`, breaking at spaces; a word wider than the line
// gets a line of its own.
std::vector<std::string> wrapCaption(const graphics::Font& font, std::string_view text,
                                     const graphics::FontMetrics& metrics, float width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t space = text.find(' ', pos);
        const std::size_t end = space == std::string_view::npos ? text.size() : space;
        const std::string_view word = text.substr(pos, end - pos);
        pos = end + 1;
        if (word.empty()) {
            continue;
        }
        std::string candidate = line.empty() ? std::string(word) : line + " " + std::string(word);
        if (!line.empty() && font.measure(candidate, metrics, graphics::kFontProportional) > width) {
            lines.push_back(std::move(line));
            candidate = std::string(word);
        }
        line = std::move(candidate);
    }
    if (!line.empty()) {
        lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace

bool movieSkippable(std::string_view name) { return name != "LOGO"; }

MovieMode::MovieMode(GameModeStack& stack, graphics::RenderDevice& device, MovieOpener open, MovieScreen* screen,
                     audio::Mixer* mixer, MovieSettings settings, std::function<void(std::string_view)> log)
    : m_stack(stack), m_device(device), m_open(std::move(open)), m_screen(screen), m_mixer(mixer),
      m_settings(std::move(settings)), m_log(std::move(log)) {}

void MovieMode::setCaptionFont(const graphics::Font* font) {
    m_font = font;
    m_captionBatch.reset();
    if (font != nullptr) {
        m_captionBatch.emplace(font->sheet(), kCaptionSprites, kCaptionDepth);
    }
}

void MovieMode::playMovie(std::string_view name) {
    if (m_settings.skipAll) {
        m_log(std::format("movie: {} skipped (--skip-movies)\n", name));
        return;
    }
    m_queue.emplace_back(name);
    // The caller waits beneath the player, as the original's caller waits inside Movie_Play.
    if (!m_onStack) {
        m_onStack = true;
        m_stack.push(*this);
    }
}

void MovieMode::exit() {
    if (m_current) {
        finish(false);
    }
    m_queue.clear();
    m_onStack = false;
}

const CaptionRecord* MovieMode::caption() const {
    if (!m_current) {
        return nullptr;
    }
    // Through a local, so the optional checked is visibly the one read.
    const std::optional<MovieCaptions>& movieCaptions = m_current->captions;
    if (!movieCaptions) {
        return nullptr;
    }
    const MovieCaptions& captions = *movieCaptions;
    const bool subtitles = m_settings.subtitlesOn ? m_settings.subtitlesOn() : false;
    return captions.captions.visible(subtitles);
}

bool MovieMode::start(std::string name) {
    auto decoder = m_open(name);
    if (!decoder) {
        // As BinkOpen failing: the movie is silently skipped (Coney logs why).
        ++m_counts.failed;
        m_log(std::format("movie: {} not played ({})\n", name, decoder.error().message));
        return false;
    }
    auto playing = std::make_unique<Playing>();
    playing->name = std::move(name);
    playing->skippable = movieSkippable(playing->name);
    playing->decoder = std::move(*decoder);
    playing->info = playing->decoder->info();
    // Movie_Play stops the music and every sound before Bink takes the sound hardware: the game's sound engine first,
    // so its tasks end with their voices, then whatever else the mixer plays.
    if (m_soundStop) {
        m_soundStop();
    }
    if (m_mixer != nullptr) {
        m_mixer->stopAll();
        if (playing->info.hasAudio) {
            const auto capacity =
                static_cast<std::uint32_t>(kStreamSeconds * static_cast<double>(playing->info.sampleRate));
            auto stream = audio::PcmStream::create(playing->info.channels, playing->info.sampleRate, capacity);
            if (stream) {
                playing->stream = std::move(*stream);
                audio::VoiceParams params;
                params.volume = kMovieVolume;
                params.bus = audio::Bus::Music;
                params.priority = 255;
                playing->voice = m_mixer->play(playing->stream, params);
            }
        }
    }
    if (m_captionSource) {
        playing->captions = m_captionSource(playing->name);
    }
    if (m_settings.present) {
        m_pixels.assign(static_cast<std::size_t>(playing->info.width) * playing->info.height * 4, 0);
    }
    m_texture = nullptr;
    m_shownCaption = nullptr;
    ++m_counts.movies;
    m_log(std::format("movie: {} playing ({} frames at {:.2f} a second{})\n", playing->name, playing->info.frameCount,
                      playing->info.frameRate(), playing->info.hasAudio ? ", with sound" : ""));
    m_current = std::move(playing);
    return true;
}

void MovieMode::finish(bool skipped) {
    if (!m_current) {
        return;
    }
    if (m_mixer != nullptr && m_current->voice.valid()) {
        m_mixer->stop(m_current->voice);
    }
    if (skipped) {
        ++m_counts.skipped;
    }
    m_log(std::format("movie: {} {} after {} frames\n", m_current->name, skipped ? "skipped" : "ended",
                      m_current->decoded));
    m_current.reset();
    // Black until whoever called fades in (Movie_Play leaves both buffers black).
    m_texture = nullptr;
    m_shownCaption = nullptr;
    m_pass.empty();
}

void MovieMode::feedAudio() {
    Playing& playing = *m_current;
    const std::size_t taken = playing.decoder->takeAudio(playing.pending);
    m_counts.samples += taken;
    if (!playing.stream || playing.pending.empty()) {
        if (!playing.stream) {
            playing.pending.clear(); // no sound output: decode and drop
        }
        return;
    }
    const std::size_t frames = playing.stream->write(playing.pending);
    const auto used = static_cast<std::ptrdiff_t>(frames * static_cast<std::size_t>(playing.info.channels));
    playing.pending.erase(playing.pending.begin(), playing.pending.begin() + used);
}

bool MovieMode::anyButton(const GameModeStack& stack) {
    // Movie_CheckSkip: the first record with any button down skips; the sticks are not buttons.
    for (std::size_t record = 0; record < Pads::kRecords; ++record) {
        if (stack.pads().record(record).buttons() != 0) {
            return true;
        }
    }
    return false;
}

bool MovieMode::step(const GameModeStack& stack, const FrameTime& frame) {
    Playing& playing = *m_current;
    // The frame due now, by the movie's own rate; the last frame ends the movie without being shown.
    const auto due = static_cast<std::int64_t>(std::floor(playing.seconds * playing.info.frameRate() + 1e-9));
    if (due >= static_cast<std::int64_t>(playing.info.frameCount) - 1) {
        finish(false);
        return false;
    }
    // Decode up to the due frame; only the due frame is converted, and only when it is shown.
    while (playing.decoded <= due) {
        const bool convert = m_settings.present && playing.decoded == due;
        auto decoded =
            playing.decoder->decodeFrame(convert ? std::span<std::uint8_t>(m_pixels) : std::span<std::uint8_t>());
        if (!decoded || !*decoded) {
            // A read error ends the movie (BinkMovie_Play's I/O error check), as does a file shorter than its count.
            if (!decoded) {
                ++m_counts.failed;
                m_log(std::format("movie: {} stopped ({})\n", playing.name, decoded.error().message));
            }
            finish(false);
            return false;
        }
        ++playing.decoded;
        ++m_counts.framesDecoded;
        if (convert) {
            ++m_counts.framesShown;
            m_frameWidth = playing.info.width;
            m_frameHeight = playing.info.height;
            m_texture = m_screen != nullptr ? m_screen->upload(m_pixels, m_frameWidth, m_frameHeight) : nullptr;
        } else if (playing.decoded - 1 == due) {
            ++m_counts.framesShown;
        }
    }
    feedAudio();
    // The captions move on with the movie's time.
    if (playing.captions) {
        for (const int command : playing.captions->timeline.advance(playing.seconds)) {
            playing.captions->captions.command(command);
        }
    }
    layOutCaption();
    // Movie_CheckSkip, after the frame, every pass.
    if (playing.skippable && anyButton(stack)) {
        if (playing.captions) {
            playing.captions->captions.command(4); // the skip clears the caption
        }
        finish(true);
        return false;
    }
    playing.seconds += frame.seconds;
    return true;
}

ModeResult MovieMode::update(GameModeStack& stack, const FrameTime& frame) {
    // Start the next queued movie that opens. A movie that ends lets the next one start on the next step, as the
    // original returns from Movie_Play between two movies, so the press that skipped one is not counted twice.
    while (!m_current) {
        if (m_queue.empty()) {
            return ModeResult::Leave;
        }
        std::string name = std::move(m_queue.front());
        m_queue.pop_front();
        start(std::move(name));
    }
    if (!step(stack, frame) && m_queue.empty()) {
        return ModeResult::Leave;
    }
    return ModeResult::Stay;
}

void MovieMode::layOutCaption() {
    m_pass.empty();
    const CaptionRecord* record = caption();
    if (record != m_shownCaption && record != nullptr) {
        ++m_counts.captionsShown;
    }
    m_shownCaption = record;
    if (record == nullptr || m_font == nullptr || !m_captionBatch) {
        return;
    }
    // Captions_Draw: the lines wrapped at the style's width, centred on x, the block centred on y.
    const CaptionStyle style = captionStyle(captionKind(record->kind));
    const graphics::FontMetrics metrics = graphics::fontMetrics(style.scale);
    const std::vector<std::string> lines = wrapCaption(*m_font, record->text, metrics, style.wrapWidth);
    const float lineStep = metrics.height + metrics.lineGap;
    float y = style.y - lineStep * static_cast<float>(lines.size() - 1) / 2.0F;
    std::vector<graphics::Sprite> sprites;
    for (const std::string& line : lines) {
        m_font->draw(sprites, line, style.x, y, metrics, graphics::kFontCentred | graphics::kFontProportional,
                     style.colour, kCaptionShadow);
        y += lineStep;
    }
    for (const graphics::Sprite& sprite : sprites) {
        m_captionBatch->addSprite(sprite);
    }
    m_pass.queue(*m_captionBatch);
}

void MovieMode::render(const RenderTime& /*time*/) {
    m_device.beginFrame(graphics::kBlack);
    if (m_texture != nullptr) {
        // Unscaled, centred on the logical screen, cut to its height (Movie_BuildUploadPacket).
        const float height = std::min(static_cast<float>(m_frameHeight), graphics::kLogicalHeight);
        const graphics::LogicalQuad quad{(graphics::kLogicalWidth - static_cast<float>(m_frameWidth)) / 2.0F,
                                         (graphics::kLogicalHeight - height) / 2.0F,
                                         static_cast<float>(m_frameWidth),
                                         height,
                                         graphics::UvRect{0.0F, 0.0F, 1.0F, height / static_cast<float>(m_frameHeight)},
                                         graphics::kWhite};
        m_device.drawQuads(m_texture, std::span(&quad, 1));
    }
    m_pass.draw(m_device, m_camera);
    m_device.present();
}

} // namespace coney::movies
