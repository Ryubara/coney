// SPDX-License-Identifier: GPL-3.0-or-later
// The game events' one call (docs/guides/multiplayer.md#events): raise() keeps events only while recording, in the
// order raised, and every kind has its fields, its delivery and its name.
#include "core/game_events.h"

#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using coney::GameEvent;
using coney::GameEventKind;
using coney::GameEvents;

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("game events: offline, raise keeps nothing", "[core][game_events]") {
    GameEvents events;
    events.raise(GameEvent{.kind = GameEventKind::Sound2D, .id = 7});
    coney::raiseGameEvent(&events, GameEvent{.kind = GameEventKind::MusicStop});
    coney::raiseGameEvent(nullptr, GameEvent{.kind = GameEventKind::MusicStop});
    CHECK_FALSE(events.recording());
    CHECK(events.size() == 0);
    CHECK(events.take().empty());
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("game events: recording keeps them in the order raised until taken", "[core][game_events]") {
    GameEvents events;
    events.setRecording(true);
    events.raise(GameEvent{.kind = GameEventKind::SceneStart, .id = 3});
    coney::raiseGameEvent(&events, GameEvent{.kind = GameEventKind::Sound2D, .id = 0x1234});
    const std::vector<GameEvent> taken = events.take();
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].kind == GameEventKind::SceneStart);
    CHECK(taken[1].id == 0x1234);
    CHECK(events.take().empty());
    // Turning recording off, or on again, drops what is kept.
    events.raise(GameEvent{.kind = GameEventKind::MusicStop});
    events.setRecording(false);
    events.setRecording(true);
    CHECK(events.size() == 0);
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("game events: a text over the limit is cut to it", "[core][game_events]") {
    GameEvents events;
    events.setRecording(true);
    events.raise(GameEvent{.kind = GameEventKind::HudMessage, .text = std::string(400, 'x')});
    CHECK(events.take().at(0).text.size() == coney::kMaxGameEventText);
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("game events: one-shots go unreliably, music, loops, messages, speech, hints, scenes and movies reliably",
          "[core][game_events]") {
    using enum GameEventKind;
    for (const GameEventKind kind :
         {Sound2D, Sound3D, HumanSound, Hit, ParticleStart, ObjectBreak, MaterialPair, Cue}) {
        CHECK_FALSE(coney::reliableGameEvent(kind));
    }
    for (const GameEventKind kind : {MusicStart, MusicStop, LoopStart, LoopStop, HudMessage, Objective, SceneStart,
                                     SceneEnd, MovieStart, MovieEnd, Speech, Command, SpeechStop, Hint}) {
        CHECK(coney::reliableGameEvent(kind));
    }
    CHECK_FALSE(coney::validGameEventKind(0));
    CHECK(coney::validGameEventKind(1));
    CHECK(coney::validGameEventKind(22));
    CHECK_FALSE(coney::validGameEventKind(23));
    CHECK(coney::gameEventFields(MusicStop) == 0);
    CHECK(coney::gameEventFields(Sound3D) == (coney::kEventId | coney::kEventPosition));
    // A particle system carries its turn, colour and rectangle, so a guest starts it the same way.
    CHECK(coney::gameEventFields(ParticleStart) == (coney::kEventSubject | coney::kEventId | coney::kEventValue |
                                                    coney::kEventPosition | coney::kEventText | coney::kEventRotation));
}

// Expected: unverified, #547 (Coney's own network play; the original has none)
TEST_CASE("game events: an event's line names its kind and the fields it carries", "[core][game_events]") {
    CHECK(coney::describe(GameEvent{.kind = GameEventKind::Sound3D, .id = 0xabc, .position = {1.0F, -2.5F, 0.25F}}) ==
          "sound3d id=0xabc at=1.000,-2.500,0.250");
    CHECK(coney::describe(GameEvent{.kind = GameEventKind::MovieStart, .value = 1, .text = "intro"}) ==
          "movie_start value=1 text=\"intro\"");
    CHECK(coney::describe(GameEvent{.kind = GameEventKind::ParticleStart,
                                    .subject = 3,
                                    .id = 0xff00ff80,
                                    .value = 42,
                                    .position = {1.0F, 2.0F, 3.0F},
                                    .text = "spark",
                                    .rotation = {0.0F, 0.0F, 0.5F, 0.866F}}) ==
          "particle_start subject=3 id=0xff00ff80 value=42 at=1.000,2.000,3.000 rot=0.000,0.000,0.500,0.866 "
          "text=\"spark\"");
    CHECK(coney::describe(GameEvent{.kind = GameEventKind::Speech, .subject = 9, .value = 1, .text = "l99_hi"}) ==
          "speech subject=9 value=1 text=\"l99_hi\"");
    // A field the kind does not carry is not shown.
    CHECK(coney::describe(GameEvent{.kind = GameEventKind::MusicStop, .id = 5}) == "music_stop");
}
