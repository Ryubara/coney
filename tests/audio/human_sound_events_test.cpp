// SPDX-License-Identifier: GPL-3.0-or-later
// The humans' animation and hit sounds (docs/research/sound-events.md): footsteps on the remapped ground, a player's
// doubled volume, the vocal ids' women's entries, the speech commands, and the hits' prone torso, into a recording
// sink and a recording voice.
#include "audio/human_sound_events.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/material_sounds.h"
#include "audio/sound_matrix.h"
#include "human/human_sounds.h"

using Catch::Approx;
using coney::audio::HumanSoundEvents;
using coney::audio::HumanTraits;
using coney::audio::HumanVoices;
using coney::audio::MaterialSoundPlayer;
using coney::audio::SoundHandle;
using coney::audio::SoundMatrix;
using coney::audio::SoundPlay;
using coney::audio::SoundSink;
using coney::audio::SoundVec;
using coney::script::HumanSoundCall;

namespace {

using Columns = std::array<std::optional<std::uint32_t>, 3>;
constexpr std::array<float, 3> kFull{1.0F, 1.0F, 1.0F};

// A sink that records each sound it is asked to start.
class RecordingSink final : public SoundSink {
  public:
    struct Played {
        std::uint32_t hash = 0;
        SoundPlay how;
    };
    std::vector<Played> played;
    std::vector<SoundHandle> stopped;
    SoundHandle play(std::uint32_t hash, const SoundPlay& how) override {
        played.push_back({hash, how});
        return SoundHandle{static_cast<std::uint32_t>(played.size())};
    }
    void stop(SoundHandle sound) override { stopped.push_back(sound); }
};

// Voices that record the lines and commands asked for.
class RecordingVoices final : public HumanVoices {
  public:
    struct Line {
        std::uint32_t hash = 0;
        float volume = 0.0F;
        bool cut = false;
    };
    struct Command {
        std::uint32_t command = 0;
        float volume = 0.0F;
    };
    std::vector<Line> lines;
    std::vector<Command> commands;
    bool talking = false;
    bool commandsAnswer = true;
    bool gestures = true;
    bool scene = false;
    int stops = 0;

    [[nodiscard]] bool speaking(double /*human*/) const override { return talking; }
    void stopLine(double /*human*/) override { ++stops; }
    bool sayLine(const HumanSoundCall& /*who*/, std::uint32_t hash, float volume, bool cut) override {
        lines.push_back({hash, volume, cut});
        return true;
    }
    bool sayCommand(const HumanSoundCall& /*who*/, std::uint32_t command, float volume, bool /*interrupt*/,
                    bool /*duckable*/) override {
        commands.push_back({command, volume});
        return commandsAnswer;
    }
    bool mayGesture(const HumanSoundCall& /*who*/) override { return gestures; }
    [[nodiscard]] bool scenePlaying() const override { return scene; }
};

// A matrix, players, voices and the events over them.
struct Rig {
    SoundMatrix matrix;
    RecordingSink sink;
    MaterialSoundPlayer sounds{matrix, &sink};
    RecordingVoices voices;
    HumanSoundEvents events{sounds, voices, [](std::int32_t low, std::int32_t /*high*/) { return low; }};

    // One material pair with columns 1 and 2.
    void pair(std::uint32_t m1, std::uint32_t m2, std::uint32_t first, std::uint32_t second) {
        matrix.newMaterialSlots(m1, m2, 1, 2, kFull);
        matrix.newMaterialSound(0, m1, m2, Columns{first, second, std::nullopt});
    }
    // One animation entry with column 1.
    void anim(std::uint32_t id, std::uint32_t sound, float volume = 1.0F) {
        matrix.newAnimSlots(id, 1, 1, {volume, 1.0F, 1.0F});
        matrix.newAnimSound(0, id, Columns{sound, std::nullopt, std::nullopt});
    }
};

// A human standing on `ground`.
HumanSoundCall human(std::uint32_t ground, bool player = false) {
    HumanSoundCall call;
    call.human = 7.0;
    call.player = player;
    call.ground = ground;
    call.position = {1.0F, 2.0F, 3.0F};
    return call;
}

} // namespace

TEST_CASE("a footstep plays the shoe against the remapped ground, twice as loud for a player", "[audio][human]") {
    Rig rig;
    rig.pair(8, 5, 501, 502);                         // SHOE x CONCRETE
    rig.pair(8, 35, 601, 602);                        // SHOE x GRASS (carpet's footstep)
    rig.events.animSound(human(6), HumanTraits{}, 1); // asphalt walks as concrete
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[0].hash == 502); // column 2 first
    CHECK(rig.sink.played[1].hash == 501);
    CHECK(rig.sink.played[1].how.volume == Approx(1.0F));
    const std::optional<SoundVec> stepAt = rig.sink.played[1].how.position;
    REQUIRE(stepAt.has_value());
    if (!stepAt) {
        return;
    }
    CHECK(stepAt->x == Approx(1.0F));

    // A player's run step on carpet: 1.25 x 0.5 (carpet) x 2 (player).
    rig.sink.played.clear();
    rig.events.animSound(human(121, true), HumanTraits{}, 3);
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[1].hash == 601);
    CHECK(rig.sink.played[1].how.volume == Approx(1.25F));

    // Sneaking in shadow: half volume, no doubling.
    rig.sink.played.clear();
    HumanSoundCall sneaking = human(5, true);
    sneaking.hiddenInShadow = true;
    rig.events.animSound(sneaking, HumanTraits{}, 1);
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[1].how.volume == Approx(0.5F));
}

TEST_CASE("a landing and a body fall play their body materials", "[audio][human]") {
    Rig rig;
    rig.pair(164, 5, 701, 0); // FEET_LAND x CONCRETE
    rig.pair(26, 5, 801, 0);  // HUMAN x CONCRETE
    rig.events.animSound(human(5), HumanTraits{}, 37);
    rig.events.animSound(human(5), HumanTraits{}, 13);
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[0].hash == 701);
    CHECK(rig.sink.played[1].hash == 801);
}

TEST_CASE("a player's animation sound adds column 2 at twice the volume", "[audio][human]") {
    Rig rig;
    rig.matrix.newAnimSlots(60, 1, 2, {0.5F, 0.25F, 1.0F});
    rig.matrix.newAnimSound(0, 60, Columns{11U, 12U, std::nullopt});
    rig.events.animSound(human(5), HumanTraits{}, 60);
    REQUIRE(rig.sink.played.size() == 1);
    CHECK(rig.sink.played[0].hash == 11);
    CHECK(rig.sink.played[0].how.volume == Approx(0.5F));
    CHECK(rig.sink.played[0].how.owner == 7);

    rig.sink.played.clear();
    rig.events.animSound(human(5, true), HumanTraits{}, 60);
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[0].how.volume == Approx(1.0F));
    CHECK(rig.sink.played[1].hash == 12);
    CHECK(rig.sink.played[1].how.volume == Approx(0.5F));
}

TEST_CASE("cloth sounds only for a player or a boss; swooshes for anyone", "[audio][human]") {
    Rig rig;
    rig.anim(4, 40);
    rig.anim(5, 50);
    rig.events.animSound(human(5), HumanTraits{}, 4);
    CHECK(rig.sink.played.empty());
    rig.events.animSound(human(5), HumanTraits{.bossClass = true}, 4);
    rig.events.animSound(human(5), HumanTraits{}, 5);
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[0].hash == 40);
    CHECK(rig.sink.played[1].hash == 50);
}

TEST_CASE("a vocal id is said as the human's line, a woman's from her entry", "[audio][human]") {
    Rig rig;
    rig.anim(22, 220, 0.8F); // grunt
    rig.anim(108, 1080);     // grunt_female
    rig.events.animSound(human(5), HumanTraits{}, 22);
    rig.events.animSound(human(5), HumanTraits{.female = true}, 22);
    REQUIRE(rig.voices.lines.size() == 2);
    CHECK(rig.voices.lines[0].hash == 220);
    CHECK(rig.voices.lines[0].volume == Approx(0.8F));
    CHECK(rig.voices.lines[1].hash == 1080);
    CHECK(rig.sink.played.empty());

    // A silenced human says nothing; expire never plays over a line.
    rig.voices.lines.clear();
    rig.events.animSound(human(5), HumanTraits{.canSpeak = false}, 22);
    rig.anim(115, 1150);
    rig.voices.talking = true;
    rig.events.animSound(human(5), HumanTraits{}, 115);
    CHECK(rig.voices.lines.empty());
    // A grunt plays over a line, stopping it first.
    rig.events.animSound(human(5), HumanTraits{}, 22);
    CHECK(rig.voices.stops == 1);
    CHECK(rig.voices.lines.size() == 1);
}

TEST_CASE("a dying human's line waits out a scene", "[audio][human]") {
    Rig rig;
    rig.anim(58, 580);
    rig.voices.scene = true;
    rig.events.animSound(human(5), HumanTraits{}, 58);
    CHECK(rig.voices.lines.empty());
    rig.voices.scene = false;
    rig.events.animSound(human(5), HumanTraits{}, 58);
    REQUIRE(rig.voices.lines.size() == 1);
    CHECK(rig.voices.lines[0].cut);
}

TEST_CASE("speech command ids say their command, falling back to the line", "[audio][human]") {
    Rig rig;
    rig.anim(28, 280);
    HumanSoundCall at = human(5);
    at.targetIsPlayer = true;
    rig.events.animSound(at, HumanTraits{}, 28); // grunt_x: pain, louder at the player
    REQUIRE(rig.voices.commands.size() == 1);
    CHECK(rig.voices.commands[0].command == 12);
    CHECK(rig.voices.commands[0].volume == Approx(1.5F));
    CHECK(rig.voices.lines.empty());
    // No pain line: the grunt is said instead.
    rig.voices.commandsAnswer = false;
    rig.events.animSound(at, HumanTraits{}, 28);
    CHECK(rig.voices.lines.size() == 1);
    // A gesture waits for a free slot near the camera.
    rig.voices.gestures = false;
    rig.events.animSound(at, HumanTraits{}, 97);
    CHECK(rig.voices.commands.size() == 2);
    rig.voices.gestures = true;
    rig.events.animSound(at, HumanTraits{}, 97);
    REQUIRE(rig.voices.commands.size() == 3);
    CHECK(rig.voices.commands[2].command == 15);
}

TEST_CASE("the fixed kick pairs play the shoe on the prone torso and the head", "[audio][human]") {
    Rig rig;
    rig.pair(8, 159, 901, 0);
    rig.pair(8, 17, 902, 0);
    rig.events.animSound(human(5), HumanTraits{}, 46);
    rig.events.animSound(human(5), HumanTraits{}, 65);
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[0].hash == 901);
    CHECK(rig.sink.played[1].hash == 902);
}

TEST_CASE("the spray loop keeps one sound per human", "[audio][human]") {
    Rig rig;
    rig.anim(83, 830);
    rig.events.animSound(human(5), HumanTraits{}, 83);
    rig.events.animSound(human(5), HumanTraits{}, 83);
    CHECK(rig.sink.played.size() == 2);
    REQUIRE(rig.sink.stopped.size() == 1);
    CHECK(rig.sink.stopped[0].valid());
}

TEST_CASE("a hit plays column 1, a player's columns 1 and 2 doubled, on a downed victim the prone torso",
          "[audio][human]") {
    namespace mat = coney::human::material;
    Rig rig;
    rig.pair(mat::kFist, mat::kTorso, 1001, 1002);
    rig.pair(mat::kFist, mat::kTorsoProne, 1101, 1102);
    HumanSoundCall call = human(5);
    call.sound = coney::human::HumanSound{.kind = coney::human::HumanSound::Kind::Impact,
                                          .material1 = mat::kFist,
                                          .material2 = mat::kTorso,
                                          .volume = 1.0F,
                                          .victimDown = false,
                                          .ownerIsPlayer = false,
                                          .at = {4.0F, 5.0F, 6.0F},
                                          .sceneFeet = std::nullopt};
    rig.events.play(call, HumanTraits{});
    REQUIRE(rig.sink.played.size() == 1);
    CHECK(rig.sink.played[0].hash == 1001);
    const std::optional<SoundVec> hitAt = rig.sink.played[0].how.position;
    REQUIRE(hitAt.has_value());
    if (!hitAt) {
        return;
    }
    CHECK(hitAt->x == Approx(4.0F));

    rig.sink.played.clear();
    call.sound.ownerIsPlayer = true;
    call.sound.victimDown = true;
    rig.events.play(call, HumanTraits{});
    REQUIRE(rig.sink.played.size() == 2);
    CHECK(rig.sink.played[0].hash == 1101);
    CHECK(rig.sink.played[0].how.volume == Approx(2.0F));
    CHECK(rig.sink.played[1].hash == 1102);
}

TEST_CASE("a strike's material comes from its limb and strength", "[human][sound]") {
    using coney::human::StrikeLimb;
    namespace mat = coney::human::material;
    CHECK(coney::human::strikeMaterial(StrikeLimb::Hand, 0, false) == mat::kJab);
    CHECK(coney::human::strikeMaterial(StrikeLimb::Hand, 1, false) == mat::kFist);
    CHECK(coney::human::strikeMaterial(StrikeLimb::Hand, 3, false) == mat::kBigPunch);
    CHECK(coney::human::strikeMaterial(StrikeLimb::Hand, 2, true) == mat::kBossFist);
    CHECK(coney::human::strikeMaterial(StrikeLimb::Foot, 1, false) == mat::kShoe);
    CHECK(coney::human::strikeMaterial(StrikeLimb::Foot, 2, true) == mat::kBigKick);
    CHECK(coney::human::strikeMaterial(StrikeLimb::Head, 3, false) == mat::kHead);
    CHECK(coney::human::strikeMaterial(StrikeLimb::Other, 1, false) == 0);
    CHECK(coney::human::strikeLimbOfClip("pc_KickFront") == StrikeLimb::Foot);
    CHECK(coney::human::strikeLimbOfClip("stomp_down") == StrikeLimb::Foot);
    CHECK(coney::human::strikeLimbOfClip("HeadButt") == StrikeLimb::Head);
    CHECK(coney::human::strikeLimbOfClip("punch_jab") == StrikeLimb::Hand);
}
