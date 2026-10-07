// SPDX-License-Identifier: GPL-3.0-or-later
// The sound matrix (docs/research/sound.md#sound-matrix): entries made by the preloads' bindings, alternatives played
// in turn, the default material's fallback, shared reverse pairs, and the players' columns into a recording sink.
#include "audio/sound_matrix.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "audio/material_sounds.h"

using Catch::Approx;
using coney::audio::MaterialSoundPlayer;
using coney::audio::MatrixSounds;
using coney::audio::SoundHandle;
using coney::audio::SoundMatrix;
using coney::audio::SoundPlay;
using coney::audio::SoundSink;
using coney::audio::SoundVec;

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
    SoundHandle play(std::uint32_t hash, const SoundPlay& how) override {
        played.push_back({hash, how});
        return SoundHandle{static_cast<std::uint32_t>(played.size())};
    }
};

// The first column of an alternative, or 0.
std::uint32_t first(const std::optional<MatrixSounds>& sounds) { return sounds ? sounds->sounds[0] : 0U; }

} // namespace

TEST_CASE("a material pair's alternatives play in turn and wrap", "[audio][matrix]") {
    SoundMatrix matrix;
    matrix.newMaterialSlots(9, 10, 3, 1, kFull);
    for (std::uint32_t i = 0; i < 3; ++i) {
        matrix.newMaterialSound(i, 9, 10, Columns{100 + i, std::nullopt, std::nullopt});
    }
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 100);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 101);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 102);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 100);
    // Fewer alternatives in use, and the turn starts again.
    matrix.setMaterialSlotCount(9, 10, 2);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 100);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 101);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 100);
    // Never more than were made.
    matrix.setMaterialSlotCount(9, 10, 7);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 100);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 101);
    CHECK(first(matrix.nextMaterialSounds(9, 10, 5)) == 102);
}

TEST_CASE("none empties a column, nil leaves it, and unmade columns hold nothing", "[audio][matrix]") {
    SoundMatrix matrix;
    matrix.newMaterialSlots(2, 2, 1, 2, {0.5F, 0.25F, 1.0F});
    matrix.newMaterialSound(0, 2, 2, Columns{11U, 12U, 13U});
    std::optional<MatrixSounds> sounds = matrix.nextMaterialSounds(2, 2, 5);
    REQUIRE(sounds.has_value());
    if (!sounds) {
        return;
    }
    CHECK(sounds->sounds == std::array<std::uint32_t, 3>{11, 12, 0});
    CHECK(sounds->volumes[0] == Approx(0.5F));
    CHECK(sounds->volumes[1] == Approx(0.25F));
    matrix.newMaterialSound(0, 2, 2, Columns{0U, std::nullopt, std::nullopt});
    sounds = matrix.nextMaterialSounds(2, 2, 5);
    REQUIRE(sounds.has_value());
    if (sounds) {
        CHECK(sounds->sounds == std::array<std::uint32_t, 3>{0, 12, 0});
    }
    // An index past the alternatives, a pair with no entry, and an out-of-range material change nothing.
    matrix.newMaterialSound(4, 2, 2, Columns{99U, 99U, 99U});
    matrix.newMaterialSound(0, 3, 3, Columns{99U, 99U, 99U});
    matrix.newMaterialSlots(400, 2, 1, 1, kFull);
    CHECK(matrix.materialEntries() == 1);
}

TEST_CASE("a second material of none, or an empty pair, falls back to the default", "[audio][matrix]") {
    SoundMatrix matrix;
    matrix.newMaterialSlots(8, 5, 1, 1, kFull);
    matrix.newMaterialSound(0, 8, 5, Columns{55U, std::nullopt, std::nullopt});
    CHECK(first(matrix.nextMaterialSounds(8, 0, 5)) == 55);
    CHECK(first(matrix.nextMaterialSounds(8, 1, 5)) == 55);
    CHECK(first(matrix.nextMaterialSounds(8, 40, 5)) == 55);
    CHECK_FALSE(matrix.nextMaterialSounds(8, 40, 16).has_value());
    CHECK_FALSE(matrix.nextMaterialSounds(7, 5, 5).has_value());
}

TEST_CASE("a duplicated pair shares its entry and its turn both ways", "[audio][matrix]") {
    SoundMatrix matrix;
    matrix.newMaterialSlots(9, 17, 2, 1, kFull);
    matrix.newMaterialSound(0, 9, 17, Columns{1U, std::nullopt, std::nullopt});
    matrix.newMaterialSound(1, 9, 17, Columns{2U, std::nullopt, std::nullopt});
    matrix.duplicateMaterials(9, 17);
    CHECK(first(matrix.nextMaterialSounds(9, 17, 5)) == 1);
    CHECK(first(matrix.nextMaterialSounds(17, 9, 5)) == 2);
    CHECK(matrix.materialEntries() == 2);
}

TEST_CASE("loading another matrix empties it; the same name keeps it", "[audio][matrix]") {
    SoundMatrix matrix;
    CHECK(matrix.name() == "sound");
    matrix.newAnimSlots(1, 1, 1, kFull);
    matrix.newAnimSound(0, 1, Columns{7U, std::nullopt, std::nullopt});
    CHECK_FALSE(matrix.load("sound"));
    CHECK(first(matrix.nextAnimSounds(1)) == 7);
    CHECK(matrix.load("armies"));
    CHECK(matrix.name() == "armies");
    CHECK_FALSE(matrix.nextAnimSounds(1).has_value());
    CHECK(matrix.animEntries() == 0);
    CHECK_FALSE(matrix.nextAnimSounds(SoundMatrix::kAnimSounds).has_value());
}

TEST_CASE("a material pair plays column 2 then column 1 at its volumes; a hit and an anim sound column 1",
          "[audio][matrix]") {
    SoundMatrix matrix;
    matrix.newMaterialSlots(2, 2, 1, 2, {0.5F, 0.8F, 1.0F});
    matrix.newMaterialSound(0, 2, 2, Columns{21U, 22U, std::nullopt});
    matrix.newAnimSlots(3, 1, 1, {0.9F, 1.0F, 1.0F});
    matrix.newAnimSound(0, 3, Columns{31U, std::nullopt, std::nullopt});
    RecordingSink sink;
    MaterialSoundPlayer player(matrix);
    player.playMaterialPairAt(2, 2, SoundVec{1, 2, 3}); // no sink yet: nothing
    player.setSink(&sink);
    player.playMaterialPairAt(2, 2, SoundVec{1, 2, 3});
    player.playMaterialHit(0.5F, 2, 2, SoundVec{});
    player.playAnimSound(2.0F, 3, SoundVec{4, 5, 6}, 9);
    player.playAnimSound(1.0F, 4, SoundVec{}); // no entry
    REQUIRE(sink.played.size() == 4);
    CHECK(sink.played[0].hash == 22);
    CHECK(sink.played[0].how.volume == Approx(0.8F));
    CHECK(sink.played[1].hash == 21);
    CHECK(sink.played[1].how.volume == Approx(0.5F));
    REQUIRE(sink.played[1].how.position.has_value());
    if (sink.played[1].how.position) {
        CHECK(sink.played[1].how.position->z == Approx(3.0F));
    }
    CHECK(sink.played[2].hash == 21);
    CHECK(sink.played[2].how.volume == Approx(0.25F));
    CHECK(sink.played[3].hash == 31);
    CHECK(sink.played[3].how.volume == Approx(1.8F));
    CHECK(sink.played[3].how.owner == 9);
    CHECK(player.started() == 4);
    CHECK(player.unmatched() == 1);
}

TEST_CASE("the footstep remap", "[audio][matrix]") {
    CHECK(MaterialSoundPlayer::remapFootMaterial(0x12).material == 0x6a);
    CHECK(MaterialSoundPlayer::remapFootMaterial(6).material == 5);
    const MaterialSoundPlayer::FootMaterial half = MaterialSoundPlayer::remapFootMaterial(0x79);
    CHECK(half.material == 0x23);
    CHECK(half.volume == Approx(0.5F));
    CHECK(MaterialSoundPlayer::remapFootMaterial(16).material == 16);
    CHECK(MaterialSoundPlayer::remapFootMaterial(16).volume == Approx(1.0F));
}
