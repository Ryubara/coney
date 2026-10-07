// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "audio/mixer.h"
#include "audio/pcm_stream.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/movie_player.h"
#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "movies/captions.h"
#include "movies/movie_decoder.h"

namespace coney {
class GameModeStack;
}

namespace coney::movies {

/// Where a movie's picture is shown: a texture the platform refills with each frame.
class MovieScreen {
  public:
    virtual ~MovieScreen() = default;
    MovieScreen() = default;
    MovieScreen(const MovieScreen&) = delete;
    MovieScreen& operator=(const MovieScreen&) = delete;
    MovieScreen(MovieScreen&&) = delete;
    MovieScreen& operator=(MovieScreen&&) = delete;

    /// Copies a `width` × `height` RGBA frame into the texture and returns it for drawing; it stays valid until the
    /// next call. Null when it cannot be shown.
    virtual const graphics::Texture* upload(std::span<const std::uint8_t> rgba, int width, int height) = 0;
};

/// A movie's captions: the level's caption state with the movie's scene selected, and its timeline.
struct MovieCaptions {
    Captions captions;
    CaptionTimeline timeline{{}};
};

/// What a movie's captions come from: the loaded level's Subtitles chunk and the movie's `<movie>_sub.scn`. Nothing
/// when the movie has none (or no level's chunk is loaded).
using CaptionSource = std::function<std::optional<MovieCaptions>(std::string_view movie)>;

/// How movies are played in this run.
struct MovieSettings {
    /// Convert and draw each frame. Off in a headless run: the frames are still decoded, on the same clock, so the
    /// run takes the same steps, but nothing is converted or shown.
    bool present = true;
    /// Skip every movie at once, as if it had ended (`--skip-movies`): the behaviour before Coney played movies.
    bool skipAll = false;
    /// The subtitle option (`W_GameState + 0x438`): ordinary captions show only with it on.
    std::function<bool()> subtitlesOn;
};

/// The movie player (`Movie_Play`, `0x0042a938`, and `BinkMovie_Play`, `0x00429fe8`) as a game mode. The original
/// blocks its caller until the movie ends; Coney's playMovie() queues the movie and pushes this mode over the caller,
/// which waits beneath it, suspended, until the last queued movie ends and the mode leaves. Each step:
///
/// - the frame due at the movie's time (its frame count from the start at its own rate, 30 or 29.97 a second) is
///   decoded and shown, centred and unscaled on the 640 × 448 screen over black; the movie ends when the last frame is
///   due, which is decoded but never shown, as in the original;
/// - the sound decoded with the frames is streamed to the mixer at **80 % volume** (`0x6665` / `0x7fff`), after every
///   other sound is stopped (Movie_Play stops the music and every sound first);
/// - the captions move on (CaptionTimeline) and the current one is drawn under the picture;
/// - in a skippable movie (every one but `LOGO`), **any button** on any pad record ends it, the d-pad included, not the
///   sticks; a button already held when the movie starts ends it at once.
///
/// The movie's time is the game's fixed step, not the real clock (Coney's choice, for test mode: the original lets
/// Bink pace the frames by the real clock and sound). A movie that cannot be opened is skipped, as the original skips
/// one Bink cannot open. When the mode leaves, the screen it last drew is black.
///
/// Research: docs/research/movies.md
class MovieMode final : public GameMode, public MoviePlayer {
  public:
    /// The mode's id: Coney's (the original's movie player is no game mode).
    static constexpr std::uint32_t kId = 0x110;
    /// The movie's sound volume: `0x6665` of `0x7fff`.
    static constexpr float kMovieVolume = static_cast<float>(0x6665) / static_cast<float>(0x7fff);
    /// How much decoded sound the stream holds, in seconds: enough for Bink's lead of sound before the first frame.
    static constexpr double kStreamSeconds = 2.0;

    /// A player that pushes itself on `stack`, opens movies with `open`, draws on `device` through `screen` (null: show
    /// nothing) and plays their sound on `mixer` (null: none). `log` gets one line per movie.
    MovieMode(GameModeStack& stack, graphics::RenderDevice& device, MovieOpener open, MovieScreen* screen,
              audio::Mixer* mixer, MovieSettings settings, std::function<void(std::string_view)> log);

    /// Plays the movies' sound on `mixer` from the next movie on (null: none); it must outlive its use here.
    void setMixer(audio::Mixer* mixer) { m_mixer = mixer; }
    /// The captions of each movie come from `source` (empty: none).
    void setCaptionSource(CaptionSource source) { m_captionSource = std::move(source); }
    /// The captions are drawn in `font` (Coney's stand-in: the page does not name the caption font); null: none drawn.
    void setCaptionFont(const graphics::Font* font);

    /// Queues the movie `name` and puts the player on the stack if it is not there yet.
    /// @orig 0x0042a938 Movie_Play (unknown)
    void playMovie(std::string_view name) override;

    [[nodiscard]] std::uint32_t id() const override { return kId; }
    /// The game clock stands still while a movie plays: nothing else runs while `Movie_Play` blocks.
    [[nodiscard]] bool stopsGameClock() const override { return true; }
    ModeResult update(GameModeStack& stack, const FrameTime& frame) override;
    /// Draws the due frame unscaled, centred on the screen over black, then the caption.
    /// @orig 0x00429b18 Movie_BuildUploadPacket (BinkMovie.cpp)
    void render(const RenderTime& time) override;
    void exit() override;

    /// What the player has done, for tests and the log.
    struct Counts {
        std::uint64_t movies = 0;        ///< Movies started.
        std::uint64_t skipped = 0;       ///< Movies a button ended.
        std::uint64_t failed = 0;        ///< Movies that could not be opened or read.
        std::uint64_t framesDecoded = 0; ///< Over every movie.
        std::uint64_t framesShown = 0;   ///< Frames that were the due frame of a step.
        std::uint64_t samples = 0;       ///< Sound samples decoded (interleaved), over every movie.
        std::uint64_t captionsShown = 0; ///< Caption changes to a visible caption.
    };
    /// The counts so far.
    [[nodiscard]] const Counts& counts() const { return m_counts; }
    /// Whether a movie is playing or queued.
    [[nodiscard]] bool busy() const { return m_current != nullptr || !m_queue.empty(); }
    /// The caption on screen now, if any.
    [[nodiscard]] const CaptionRecord* caption() const;

  private:
    /// The movie being played.
    struct Playing {
        std::string name;
        bool skippable = true;
        std::unique_ptr<MovieDecoder> decoder;
        MovieInfo info;
        double seconds = 0.0;     // the movie's time at this step
        std::int64_t decoded = 0; // frames decoded
        std::shared_ptr<audio::PcmStream> stream;
        audio::VoiceHandle voice;
        std::vector<std::int16_t> pending; // decoded sound the stream had no room for yet
        std::optional<MovieCaptions> captions;
    };

    /// Opens the next queued movie; false when it cannot be played (it is then counted and dropped).
    bool start(std::string name);
    /// Ends the movie playing: its sound stops and its captions go.
    void finish(bool skipped);
    /// One step of the movie playing: the frame loop's pass; false once it has ended.
    /// @orig 0x00429fe8 BinkMovie_Play (BinkMovie.cpp)
    bool step(const GameModeStack& stack, const FrameTime& frame);
    /// Hands the stream as much of the pending sound as fits.
    void feedAudio();
    /// Whether any pad record holds a button.
    /// @orig 0x0042a820 Movie_CheckSkip (PlayMovie.cpp)
    [[nodiscard]] static bool anyButton(const GameModeStack& stack);
    /// Lays out the current caption into the caption batch.
    void layOutCaption();

    GameModeStack& m_stack;
    graphics::RenderDevice& m_device;
    MovieOpener m_open;
    MovieScreen* m_screen;
    audio::Mixer* m_mixer;
    MovieSettings m_settings;
    std::function<void(std::string_view)> m_log;
    CaptionSource m_captionSource;

    std::deque<std::string> m_queue;
    std::unique_ptr<Playing> m_current;
    std::vector<std::uint8_t> m_pixels;           // the due frame's RGBA
    const graphics::Texture* m_texture = nullptr; // the due frame on the screen's texture; null: black
    int m_frameWidth = 0;
    int m_frameHeight = 0;

    const graphics::Font* m_font = nullptr;
    std::optional<graphics::SpriteBatch> m_captionBatch;
    graphics::OverlayCamera m_camera;
    graphics::OverlayPass m_pass;
    const CaptionRecord* m_shownCaption = nullptr;
    bool m_onStack = false; // pushed and not yet popped
    Counts m_counts;
};

/// Whether the movie `name` can be skipped: every movie but `LOGO`, the only one the original plays with the skippable
/// argument 0 (`main`'s first call); every other caller passes 1 or the binding's default, true.
/// Research: docs/research/movies.md#callers
[[nodiscard]] bool movieSkippable(std::string_view name);

} // namespace coney::movies
