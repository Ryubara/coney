// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every record of the Character List in warriors.glr resolves to its three
// resources; every distinct model decodes (33 frames, a 32-node HAnim hierarchy, a 32-bone PS2 skin, the 544-byte bone
// offsets) with every material's texture in its texture dictionary (read with librw on its NULL device); every
// distinct character data resource resolves its anim table; and each character is posed with its first clip and
// skinned over the reference pose, the joints checked with characters::jointMismatch(), and its bind rotations compared
// with the reference pose (bone 2's angle). docs/research/characters.md has the results. It runs
// only when the environment variable CONEY_DISC names the disc and skips otherwise, so CI never needs the game. It
// prints counts only, never data (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <numbers>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_assets.h"
#include "characters/character_rig.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/wad.h"
#include "platform/render_engine.h"
#include "platform/texture_dictionary.h"

namespace {

// Totals over the Character List.
struct CharacterTotals {
    std::uint64_t records = 0;
    std::uint64_t loaded = 0;   // records whose model and character data loaded
    std::uint64_t failures = 0; // records that did not
    std::set<std::uint32_t> models;
    std::set<std::uint32_t> data;
    std::set<std::uint32_t> textures;
    std::uint64_t shapeMismatches = 0; // models not of 33 frames, 32 nodes, 32 skin bones
    std::uint64_t vertices = 0;        // distinct vertices over the distinct models
    std::uint64_t triangles = 0;
    std::uint64_t texturedMaterials = 0; // materials naming a texture, over the distinct models
    std::uint64_t texturesFound = 0;     // records whose dictionary holds a texture named after the character
    std::uint64_t texturesMissing = 0;   // ... and records whose dictionary does not
    std::size_t mostTextures = 0;        // in one character's dictionary
    std::uint64_t emptyDictionaries = 0; // records whose dictionary holds no texture at all
    std::uint64_t dictionaryFailures = 0;
    std::uint64_t clips = 0;       // over the distinct character data resources
    std::uint64_t idsResolved = 0; // anim ids with a clip of their own
    std::uint64_t posed = 0;       // models posed with their first clip
    float worstMismatch = 0.0F;    // the largest jointMismatch() over them, metres
    double mismatchSum = 0.0;
    float lowest = 1e9F;       // the lowest skinned vertex height over them
    float highest = -1e9F;     // the highest
    float bone2Largest = 0.0F; // the largest angle between a model's bind rotation of bone 2 and the reference, degrees
    float bone2Smallest = 1e9F; // the smallest
    std::uint64_t bone2Off = 0; // models whose bone 2 bind is more than 1 degree from the reference
    std::uint64_t bonesOff = 0; // bones (2-33, over the models) whose bind is more than 10 degrees from it
};

// Whether a model has the shape characters.md gives every skinned character.
bool hasCharacterShape(const coney::characters::CharacterModel& model) {
    return model.clump.frames.size() == 33 && model.clump.hierarchy.size() == 32 && model.clump.skin.boneCount == 32 &&
           model.clump.skin.maxWeights <= 4;
}

// Poses a model with `clip` halfway through and skins it, adding the joint check and the height range to the totals.
// The angle between two rotations, degrees.
float angleBetween(coney::anim::Quat a, coney::anim::Quat b) {
    const float d = std::min(1.0F, std::fabs(coney::anim::dot(a, b)));
    return 2.0F * std::acos(d) * 180.0F / std::numbers::pi_v<float>;
}

// Compares a model's bind rotations (Coney's skeleton) with the game's reference pose, which fills the bones a clip
// leaves out: bone 2 above all, which the grab clips leave out (docs/research/formats/animation.md#reference-pose).
void compareBind(const coney::characters::CharacterModel& model, CharacterTotals& totals) {
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(model);
    const auto reference = coney::anim::referenceRotations();
    const float bone2 = angleBetween(skeleton.bindRotations[2], reference[2]);
    totals.bone2Largest = std::max(totals.bone2Largest, bone2);
    totals.bone2Smallest = std::min(totals.bone2Smallest, bone2);
    totals.bone2Off += bone2 > 1.0F ? 1 : 0;
    for (std::size_t bone = 2; bone < coney::anim::kPoseBones; ++bone) {
        totals.bonesOff += angleBetween(skeleton.bindRotations[bone], reference[bone]) > 10.0F ? 1 : 0;
    }
}

void poseAndSkin(const coney::characters::CharacterModel& model, const coney::anim::AnimClip& clip,
                 CharacterTotals& totals) {
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(model);
    const coney::anim::Pose pose =
        coney::anim::samplePose(clip, clip.duration * 0.5F, coney::anim::referenceRotations());
    const auto bones = coney::anim::boneTransforms(skeleton, pose);
    const auto matrices = coney::characters::skinningMatrices(model, bones);
    std::vector<coney::anim::Vec3> positions(model.vertices.size());
    std::vector<coney::anim::Vec3> normals(model.vertices.size());
    coney::characters::skinVertices(model, matrices, positions, normals);
    const float mismatch = coney::characters::jointMismatch(model, matrices);
    totals.worstMismatch = std::max(totals.worstMismatch, mismatch);
    totals.mismatchSum += mismatch;
    for (const coney::anim::Vec3& p : positions) {
        totals.lowest = std::min(totals.lowest, p.z);
        totals.highest = std::max(totals.highest, p.z);
    }
    ++totals.posed;
}

// Checks a character's texture dictionary: how many textures it holds (Coney draws the first: the models' materials
// name none), and whether one is named after the character (its name's hash is the record's).
void checkTextures(const coney::characters::CharacterRecord& record,
                   const std::vector<coney::platform::TextureDictionary>& dictionaries, CharacterTotals& totals) {
    std::size_t count = 0;
    bool named = false;
    for (const coney::platform::TextureDictionary& dictionary : dictionaries) {
        for (const coney::graphics::Ps2TextureInfo& texture : dictionary.info().textures) {
            ++count;
            named = named || coney::characters::characterNameHash(texture.name) == record.nameHash;
        }
    }
    totals.mostTextures = std::max(totals.mostTextures, count);
    totals.emptyDictionaries += count == 0 ? 1 : 0;
    if (named) {
        ++totals.texturesFound;
    } else {
        ++totals.texturesMissing;
    }
}

} // namespace

TEST_CASE("every character in the Character List loads, skins and finds its textures", "[disc][characters]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());

    auto list = coney::characters::loadCharacterList(*wad);
    REQUIRE(list.has_value());
    coney::chunk::ChunkHandlerTable table = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::characters::addCharacterDataHandlers(table);
    coney::platform::addTextureDictionaryHandlers(table);

    CharacterTotals totals;
    for (const coney::characters::CharacterRecord& record : list->records()) {
        ++totals.records;
        auto assets = coney::characters::loadCharacterAssets(*wad, record, table);
        if (!assets) {
            ++totals.failures;
            std::printf("  record %llu: %s\n", static_cast<unsigned long long>(totals.records),
                        assets.error().message.c_str());
            continue;
        }
        ++totals.loaded;
        const bool newModel = totals.models.insert(record.modelHash).second;
        const bool newData = totals.data.insert(record.dataHash).second;
        totals.textures.insert(record.texturesHash);
        if (newModel) {
            totals.shapeMismatches += hasCharacterShape(assets->model) ? 0 : 1;
            totals.vertices += assets->model.vertices.size();
            totals.triangles += assets->model.triangles.size();
            // Pose with the character's first clip, as the viewer does by default.
            if (!assets->data.clips().empty()) {
                poseAndSkin(assets->model, *assets->data.clips().front(), totals);
            }
            compareBind(assets->model, totals);
        }
        if (newData) {
            totals.clips += assets->data.clips().size();
            for (std::size_t id = 0; id < coney::characters::kAnimIds; ++id) {
                totals.idsResolved += assets->data.animation(id) != nullptr ? 1 : 0;
            }
        }
        if (newModel) {
            for (const coney::characters::ClumpMaterial& material : assets->model.clump.materials) {
                totals.texturedMaterials += material.texture.empty() ? 0 : 1;
            }
        }
        auto dictionaries = coney::platform::loadTextureDictionaries(*wad, *assets->textures, table);
        if (dictionaries) {
            checkTextures(record, *dictionaries, totals);
        } else {
            ++totals.dictionaryFailures;
        }
    }

    std::printf("characters: %llu records, %llu loaded, %llu failed; %zu distinct models, %zu character data, %zu "
                "texture dictionaries\n",
                static_cast<unsigned long long>(totals.records), static_cast<unsigned long long>(totals.loaded),
                static_cast<unsigned long long>(totals.failures), totals.models.size(), totals.data.size(),
                totals.textures.size());
    std::printf(
        "  models: %llu not of the 33-frame, 32-bone shape; %llu vertices, %llu triangles; %llu materials name "
        "a texture\n  textures: %llu dictionaries hold one named after their character, %llu do not (at most "
        "%zu textures in one, %llu empty), %llu failed\n",
        static_cast<unsigned long long>(totals.shapeMismatches), static_cast<unsigned long long>(totals.vertices),
        static_cast<unsigned long long>(totals.triangles), static_cast<unsigned long long>(totals.texturedMaterials),
        static_cast<unsigned long long>(totals.texturesFound), static_cast<unsigned long long>(totals.texturesMissing),
        totals.mostTextures, static_cast<unsigned long long>(totals.emptyDictionaries),
        static_cast<unsigned long long>(totals.dictionaryFailures));
    std::printf("  character data: %llu clips, %llu anim ids with a clip of their own\n",
                static_cast<unsigned long long>(totals.clips), static_cast<unsigned long long>(totals.idsResolved));
    std::printf("  posed with their first clip: %llu models, joint mismatch mean %.4f m, worst %.4f m; skinned heights "
                "%.2f to %.2f m\n",
                static_cast<unsigned long long>(totals.posed),
                totals.posed == 0 ? 0.0 : totals.mismatchSum / static_cast<double>(totals.posed),
                static_cast<double>(totals.worstMismatch), static_cast<double>(totals.lowest),
                static_cast<double>(totals.highest));
    std::printf(
        "  bind against the reference pose: bone 2 off by %.1f to %.1f degrees, %llu of %llu models more than 1 "
        "degree; %llu bones (2-33) more than 10 degrees off\n",
        static_cast<double>(totals.bone2Smallest), static_cast<double>(totals.bone2Largest),
        static_cast<unsigned long long>(totals.bone2Off), static_cast<unsigned long long>(totals.models.size()),
        static_cast<unsigned long long>(totals.bonesOff));
    CHECK(totals.failures == 0);
    CHECK(totals.shapeMismatches == 0);
    CHECK(totals.emptyDictionaries == 0);
    CHECK(totals.dictionaryFailures == 0);
    CHECK(totals.worstMismatch < 0.25F);
}
