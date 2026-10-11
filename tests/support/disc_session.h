// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The game session as `coney` runs it, for the disc tests that play a level: the session (platform/game_session.h)
// the story, `--play-level` and the debug menus' jumps all run in, headless (the null renderer), in test mode (fixed
// step, the offline sound output with the disc's sound data), its movies skipped. A level started directly plays its
// pad script from its first frame of play, as `coney --play-level --input-script` plays it; a story plays it from the
// first frame too, the pad idle until then. Disc playthrough tests start here by default
// (docs/guides/testing.md#test-through-the-players-path). They print counts only (LEGAL.md).

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "audio/object_sounds.h"
#include "audio/sound_engine.h"
#include "core/assert.h"
#include "core/chunk_system.h"
#include "core/deferred_input.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gui/global_strings.h"
#include "platform/audio_output.h"
#include "platform/game_session.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "world/sector_budget.h"

namespace coney::test {

/// One run of the game session over the disc with a pad script or a driver; nothing runs until startAtLevel() or
/// startStory(). One at a time: a run starts the renderer, which a second run cannot start while the first lives.
class DiscSession {
  public:
    /// Everything set up over `wad` (which must outlive it); the pad script must parse. Movies are skipped unless
    /// `settings` says otherwise.
    DiscSession(const io::Wad& wad, std::string_view padScript,
                const platform::GameSessionSettings& settings = defaultSettings())
        : DiscSession(wad, parseScript(padScript), nullptr, settings) {}
    /// The same with player 1's pad from `driver` (which must outlive the run), an adaptive playthrough's pad that
    /// decides each frame's input from what the game shows (docs/guides/testing.md#playthrough-tests).
    DiscSession(const io::Wad& wad, InputSource& driver,
                const platform::GameSessionSettings& settings = defaultSettings())
        : DiscSession(wad, nullptr, &driver, settings) {}
    DiscSession(const DiscSession&) = delete;
    DiscSession& operator=(const DiscSession&) = delete;
    DiscSession(DiscSession&&) = delete;
    DiscSession& operator=(DiscSession&&) = delete;
    ~DiscSession() = default;

    /// Runs `frames` more frames, each as `coney`'s loop runs one: the session's jumps first, then the step, then the
    /// sound's end of frame.
    void run(std::uint64_t frames) {
        for (std::uint64_t i = 0; i < frames && !m_stack.empty(); ++i) {
            m_frames += m_stack.runUntilEmpty(
                m_timer,
                [this] {
                    m_session->beginFrame();
                    return true;
                },
                1);
            m_audio->endFrame(1);
        }
    }
    /// Runs until `done` says yes, at most `limit` more frames; whether it did.
    bool runUntil(const std::function<bool()>& done, std::uint64_t limit) {
        for (std::uint64_t i = 0; i < limit && !done() && !m_stack.empty(); ++i) {
            run(1);
        }
        return done();
    }
    /// Runs until the level plays (its loading screen and intro movie done), at most `limit` frames.
    bool runUntilPlay(std::uint64_t limit = kPlayLimit) {
        return runUntil([this] { return m_session->inPlay(); }, limit);
    }
    /// Runs until the pad script has played `frame` frames (its own frame `frame` is next), at most `limit` more.
    bool runToPlayFrame(std::uint64_t frame, std::uint64_t limit = kPlayLimit) {
        return runUntil([this, frame] { return m_input->framesPlayed() >= frame; }, limit);
    }

    /// The session.
    [[nodiscard]] platform::GameSession& session() { return *m_session; }
    /// The sound output.
    [[nodiscard]] platform::AudioOutput& audio() { return *m_audio; }
    /// The bank in sound RAM now; empty without the sound engine.
    [[nodiscard]] std::string loadedBank() {
        const audio::SoundEngine* engine = m_audio->sounds().engine();
        return engine != nullptr ? engine->bankName() : std::string{};
    }
    /// The glass panes' and doors' sounds.
    [[nodiscard]] const audio::ObjectSounds& objectSounds() const { return m_objectSounds; }
    /// The mode stack.
    [[nodiscard]] GameModeStack& stack() { return m_stack; }
    /// Every line the session logged.
    [[nodiscard]] const std::vector<std::string>& log() const { return m_log; }
    /// Frames run so far.
    [[nodiscard]] std::uint64_t frames() const { return m_frames; }
    /// Frames the pad script has played (0 before the first frame of play).
    [[nodiscard]] std::uint64_t playFrames() const { return m_input->framesPlayed(); }

    /// How long a level may take to start playing: 2 minutes of frames, longer than any loading screen.
    static constexpr std::uint64_t kPlayLimit = 3600;

  private:
    // The settings a run uses unless given others: the movies skipped.
    static platform::GameSessionSettings defaultSettings() {
        return platform::GameSessionSettings{.skipMovies = true, .rumble = std::nullopt, .profiles = std::nullopt};
    }

    // The pad script `text`, parsed; it must parse.
    static std::unique_ptr<ScriptedInput> parseScript(std::string_view text) {
        auto parsed = parseInputScript(text);
        REQUIRE(parsed.has_value());
        return std::make_unique<ScriptedInput>(parsed ? std::move(*parsed) : std::vector<InputEvent>{});
    }

    // The set-up both constructors share: player 1's pad is `script` when given, else `driver`.
    DiscSession(const io::Wad& wad, std::unique_ptr<ScriptedInput> script, InputSource* driver,
                const platform::GameSessionSettings& settings)
        : m_handlers(chunk::ChunkHandlerTable::withDefaults()), m_budget(world::kSectorPoolSize),
          m_script(std::move(script)) {
        platform::addTextureDictionaryHandlers(m_handlers);
        platform::addSpriteSheetHandlers(m_handlers);
        auto engine = platform::RenderEngine::start(platform::RenderBackend::Null, {});
        REQUIRE(engine.has_value());
        m_engine = std::move(*engine);
        auto audio = platform::AudioOutput::start(platform::AudioSink::Offline);
        REQUIRE(audio.has_value());
        m_audio = std::move(*audio);
        REQUIRE(m_audio->startEngine(wad).has_value());
        // Exactly one of the two constructors gives a pad; the assertion tells the analyzer the pointer is set.
        InputSource* pad = m_script != nullptr ? m_script.get() : driver;
        CONEY_ASSERT(pad != nullptr);
        // The pad waits for the first frame of play, as `--play-level` plays one.
        m_input = std::make_unique<DeferredInput>(*pad, [this] { return m_session->inPlay(); });
        m_stack.setInput(m_input.get());
        m_session = std::make_unique<platform::GameSession>(
            *m_engine, m_stack, wad, m_handlers, m_budget, m_strings, m_objectSounds, settings,
            [this](std::string_view line) { m_log.emplace_back(line); });
        m_session->attachAudio(m_audio.get());
        m_timer.setFixedStep(true);
    }

    chunk::ChunkHandlerTable m_handlers;
    std::unique_ptr<platform::RenderEngine> m_engine;
    gui::GlobalStrings m_strings;
    world::SectorBudget m_budget;
    audio::ObjectSounds m_objectSounds;
    std::unique_ptr<platform::AudioOutput> m_audio; // before the session, which points at it
    std::unique_ptr<ScriptedInput> m_script;
    std::unique_ptr<DeferredInput> m_input;
    GameModeStack m_stack;
    std::vector<std::string> m_log;
    std::unique_ptr<platform::GameSession> m_session; // after everything it refers to
    GameTimer m_timer;
    std::uint64_t m_frames = 0;
};

} // namespace coney::test
