// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc what is heard while the hub's mission cutscene plays (#404,
// docs/research/sound.md#scene-world-sound). `level95` plays at checkpoint 2 in the game session as
// `--play-level level95 --checkpoint 2` plays it (support/disc_session.h): once the opening's pad lock is over, player
// 1 is steered at 60 % onto the yellow marker (flag 5, "Start Next Level") and presses triangle there, which runs the
// hub's mission action (`story.EndMissionActionFlag`, then `story.NextMissionAction`: `SoundStopAmbientTrack`,
// `events.Mute(true, true)`, `ScenePlayCinematic`). While the cinematic runs, the test samples every live sound each
// frame and checks that no ambient bed plays, no Warrior starts a line, and both listeners sit at the scene camera.
// It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts, hashes
// and levels only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "audio/game_sound.h"
#include "audio/sound_engine.h"
#include "camera/cameras.h"
#include "core/name_hash.h"
#include "fileio/wad.h"
#include "gamemodes/gameplay_mode.h"
#include "platform/game_session.h"
#include "platform/play_level_mode.h"
#include "scenes/scene_player.h"
#include "world_objects/radios.h"
#include "support/disc_play_fixtures.h"
#include "support/disc_session.h"

namespace {

// The play frames: steered from the end of the pad lock (frame 828 at checkpoint 2) toward a point 1 m past the
// marker, so he stops on it. The first triangle clears the chairs he stops at on the way (their own triangle
// handlers); the next two are pressed on the marker.
constexpr const char* kPad = "840 steer 60 -165.2,96.3\n"
                             "1100 tap triangle\n"
                             "1400 tap triangle\n"
                             "1450 tap triangle\n";
// How long the cutscene may take to start, and to play.
constexpr std::uint64_t kStartLimit = 900;
constexpr std::uint64_t kSceneLimit = 3600;
// The ambient bed's 2 s fade-out, in frames, after which nothing of it may be heard.
constexpr std::uint64_t kBedFadeFrames = 60;
// A sent level below this is silence.
constexpr float kAudible = 0.001F;

// What one class of sound did while the scene played.
struct ClassHeard {
    std::uint64_t frames = 0; // frames with one of its sounds audible
    float loudest = 0.0F;     // the loudest side sent
    bool positional = false;
    std::uint32_t loudestHash = 0; // the loudest one's name hash
};

} // namespace

TEST_CASE("the disc's hub mission cutscene: no ambient bed, no chat, the listener at the scene camera",
          "[disc][story][hub][audio][scenes]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    coney::test::DiscSession run(*wad, kPad);
    run.session().startAtLevel("level95", 2);
    REQUIRE(run.runUntilPlay());
    coney::audio::SoundEngine* engine = run.audio().sounds().engine();
    REQUIRE(engine != nullptr);
    coney::audio::GameSound& game = run.audio().game();

    // Until the mission action starts the cinematic.
    REQUIRE(run.runUntil([engine] { return engine->cinematic(); }, kStartLimit + 1500));
    const std::uint64_t linesBefore = game.speech().said();
    const coney::audio::SoundHandle bedAtStart = engine->ambientTrack();

    // The beds level95's sound boxes play (docs/research/sound-events.md#level95).
    const std::uint32_t outdoorBed = coney::crc32("vags/ambient/city/distant_traffic_loop");
    const std::uint32_t roomBed = coney::crc32("vags/ambient/city/inside_room1_loop");

    std::map<int, ClassHeard> heard;
    std::uint64_t sceneFrames = 0;
    std::uint64_t bedFrames = 0;
    std::uint64_t listenerOff = 0;
    std::uint64_t scenesPlaying = 0;
    std::map<int, std::uint64_t> radioStates; // frames per radio state
    std::uint64_t radioSounding = 0;          // frames a radio's sound is live
    std::uint64_t sceneCameraFrames = 0;
    float farthestFromPlayer = 0.0F;
    while (engine->cinematic() && sceneFrames < kSceneLimit) {
        run.run(1);
        ++sceneFrames;
        if (!engine->cinematic()) {
            break;
        }
        // Every audible sound, by class; the bed once its fade-out had time to end.
        for (const coney::audio::LiveSound& sound : engine->liveSounds()) {
            const float level = std::max(sound.sent[0], sound.sent[1]);
            if (sound.virtualPlay || level < kAudible) {
                continue;
            }
            ClassHeard& entry = heard[sound.soundClass];
            ++entry.frames;
            if (level > entry.loudest) {
                entry.loudest = level;
                entry.loudestHash = sound.hash;
            }
            entry.positional = entry.positional || sound.positional;
            if (sceneFrames > kBedFadeFrames && (sound.hash == outdoorBed || sound.hash == roomBed)) {
                ++bedFrames;
            }
        }
        // What the radios do: a clip (the announcement) is stopped by a scene, a track halved.
        const coney::GameplayMode& gameplay = run.session().gameplay();
        if (gameplay.scenes() != nullptr && gameplay.scenes()->playing()) {
            ++scenesPlaying;
        }
        for (const coney::world_objects::Radio& radio : gameplay.radios().all()) {
            ++radioStates[radio.state];
            if (radio.sound != 0.0 && game.soundPlaying(radio.sound)) {
                ++radioSounding;
            }
        }
        // The listener is the scene camera, wherever the player stands.
        const coney::camera::Cameras* cameras = run.session().gameplay().cameras();
        const coney::platform::PlayLevelMode* play = run.session().play();
        if (cameras != nullptr && play != nullptr &&
            cameras->current().kind == coney::camera::CameraKind::Scene) {
            ++sceneCameraFrames;
            const coney::anim::Vec3 eye = cameras->view().position;
            const coney::audio::SoundVec at = game.listener().position;
            if (std::hypot(at.x - eye.x, at.y - eye.y, at.z - eye.z) > 0.01F) {
                ++listenerOff;
            }
            const coney::anim::Vec3 feet = play->playerFeet();
            farthestFromPlayer = std::max(farthestFromPlayer, std::hypot(at.x - feet.x, at.y - feet.y));
        }
    }
    const std::uint64_t linesDuring = game.speech().said() - linesBefore;

    std::printf("  level95 hub cutscene: %llu frames (%llu on the scene camera), bed at start %d, bed heard %llu "
                "frames, %llu lines said, listener off the camera %llu frames, up to %.1f m from the player\n",
                static_cast<unsigned long long>(sceneFrames), static_cast<unsigned long long>(sceneCameraFrames),
                bedAtStart.valid() ? 1 : 0, static_cast<unsigned long long>(bedFrames),
                static_cast<unsigned long long>(linesDuring), static_cast<unsigned long long>(listenerOff),
                farthestFromPlayer);
    std::printf("    scenes playing %llu frames, a radio's sound live %llu frames; radio states:",
                static_cast<unsigned long long>(scenesPlaying), static_cast<unsigned long long>(radioSounding));
    for (const auto& [state, frames] : radioStates) {
        std::printf(" %d x%llu", state, static_cast<unsigned long long>(frames));
    }
    std::printf("\n");
    for (const auto& [soundClass, entry] : heard) {
        std::printf("    class %d (%s): audible %llu frames, loudest %.3f (%#010x)\n", soundClass,
                    entry.positional ? "positional" : "2D", static_cast<unsigned long long>(entry.frames),
                    entry.loudest, entry.loudestHash);
    }

    CHECK(sceneFrames > kBedFadeFrames);
    CHECK(sceneCameraFrames > 0);
    CHECK(bedFrames == 0);
    CHECK_FALSE(engine->ambientTrack().valid());
    CHECK(linesDuring == 0);
    CHECK(listenerOff == 0);
}
