// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "graphics/render_device.h"

namespace coney::scenes {
struct SceneHeader;
}

namespace coney::movies {

/// The kind word of a Subtitles record (docs/research/movies.md#caption-text).
enum class CaptionKind : std::uint32_t {
    Language = 0,   ///< A language section starts: the text is `ENGLISH`, `GERMAN`, ...
    Scene = 1,      ///< A scene's (or movie's) captions start: the text is its name (`l99_in_sub`).
    Emphasised = 2, ///< A caption drawn large and red in the middle of the screen, always.
    Ordinary = 3,   ///< A caption under the picture, only with the subtitle option on.
    Hidden = 4,     ///< As the current kind: no caption shows.
    Hidden5 = 5,    ///< Set like 4 by a caption event; nothing shows (inferred, as 4).
};

/// One record of a level's Subtitles chunk: a kind and a string.
struct CaptionRecord {
    std::uint32_t kind = 0; ///< As stored; a kind above 6 reads as Ordinary (captionKind()).
    std::string text;
};

/// The kind `stored` stands for: kinds above 6 read as 3.
[[nodiscard]] CaptionKind captionKind(std::uint32_t stored);

/// Reads chunk `0x51` Subtitles of a level file: a u16 length, then records (a u32 kind, a NUL-terminated string)
/// packed without padding, read while a record starts before `length - 1` (counted from the chunk's start). Fails with
/// ErrorCode::Truncated when the chunk is shorter than its length word or a record runs past its end.
/// @orig 0x001cab90 Subtitles_ChunkHandler (unknown)
/// Research: docs/research/movies.md#caption-text
[[nodiscard]] std::expected<std::vector<CaptionRecord>, Error> parseSubtitles(std::span<const std::byte> chunk);

/// The caption state of the original's caption system: which language's section is selected, which scene's captions
/// are running and which caption is current.
///
/// Research: docs/research/movies.md#captions
class Captions {
  public:
    /// No chunk: no captions ever show.
    Captions() = default;

    /// Over `records` (a level's Subtitles chunk), with the section of `language` (`ENGLISH`) selected.
    /// @orig 0x001cabc0 Captions_Init (unknown)
    Captions(std::vector<CaptionRecord> records, std::string_view language);

    /// Starts the captions of scene or movie `name` (`l99_in_sub`): reads on from the language's section to the scene
    /// record with that name; the next show takes the record after it. When the name is not found everything stays as
    /// it was and false is returned.
    /// @orig 0x001cad38 Captions_SelectScene (unknown)
    bool selectScene(std::string_view name);

    /// Applies caption command `command` of a scene's event 41: 0 shows the next record, 4 and 5 set that kind (no
    /// caption shows), 6 sets the flag then shows the next record. Other values do nothing.
    /// @orig 0x001cb010 Captions_SetKind (unknown)
    void command(int command);

    /// Makes the next record the current caption; past the end of the records nothing changes.
    /// @orig 0x001cb190 Captions_Next (unknown)
    void next();

    /// The caption to draw now, if any: none when no scene is selected, no record is current or the kind is 4 or 5;
    /// an ordinary caption only when `subtitlesOn` (the subtitle option, `W_GameState + 0x438`).
    /// @orig 0x001ca950 Captions_Draw (unknown)
    [[nodiscard]] const CaptionRecord* visible(bool subtitlesOn) const;

    /// Whether a scene's captions are running.
    [[nodiscard]] bool active() const { return m_active; }
    /// Whether command 6 set the flag (`0x001cb000`; meaning not traced).
    [[nodiscard]] bool flagged() const { return m_flag; }

  private:
    std::vector<CaptionRecord> m_records;
    std::size_t m_languageStart = 0;      // the record after the language marker; searches start here
    bool m_hasLanguage = false;           // the language's section was found
    std::size_t m_cursor = 0;             // the next record next() takes
    std::optional<std::size_t> m_current; // the current caption's record
    CaptionKind m_kind = CaptionKind::Hidden;
    bool m_active = false;
    bool m_flag = false;
};

/// How a caption of a kind is drawn: where its lines are centred, in GUI units (the screen about [0, 1]²), its text
/// size, its colour and the width it wraps at.
struct CaptionStyle {
    float x = 0.5F;
    float y = 0.75F;
    float scale = 1.0F;
    graphics::Rgba colour{178, 178, 178, 255};
    float wrapWidth = 0.7F;
};

/// The style of `kind` on the default interlaced 4:3 NTSC screen: an ordinary caption at (0.5, 0.75), size 1.0, grey
/// (178, 178, 178), wrapped at 0.7 of the screen; an emphasised one at the centre, size 1.2, red (134, 26, 26),
/// wrapped at 0.9.
/// @orig 0x001cb010 Captions_SetKind (unknown)
[[nodiscard]] CaptionStyle captionStyle(CaptionKind kind);

/// A movie's caption timing: the caption events (type 41) of its `<movie>_sub.scn` scene's camera track, fired as the
/// movie's time passes. The original advances the scene by the real time since the last advance once at least
/// 0.166 s has passed (`Movie_AdvanceCaptions`); Coney does the same with the movie's own time, so the captions keep
/// their rhythm in test mode.
///
/// Research: docs/research/movies.md#caption-timing
class CaptionTimeline {
  public:
    /// One caption event: its frame (1/30 s from the movie's start) and its command (`+4`).
    struct Event {
        std::uint32_t frame = 0;
        int command = 0;
    };

    /// The smallest step the scene is moved on by: 0.166 s.
    static constexpr double kAdvanceStep = 0.166;

    /// A timeline of `events`, sorted by frame (stable).
    explicit CaptionTimeline(std::vector<Event> events);

    /// The caption events of `scene`'s camera track (event type 41, command the u32 at `+4`).
    [[nodiscard]] static CaptionTimeline fromScene(const scenes::SceneHeader& scene);

    /// The movie has reached `seconds` from its start: when at least kAdvanceStep has passed since the scene last
    /// moved on, moves it to `seconds` and returns the commands of the events reached, in order; otherwise none.
    /// @orig 0x0042a718 Movie_AdvanceCaptions (PlayMovie.cpp)
    std::vector<int> advance(double seconds);

    /// How many events there are.
    [[nodiscard]] std::size_t size() const { return m_events.size(); }

  private:
    std::vector<Event> m_events;
    std::size_t m_next = 0;   // the first event not yet fired
    double m_sceneTime = 0.0; // where the scene stands, seconds
};

} // namespace coney::movies
