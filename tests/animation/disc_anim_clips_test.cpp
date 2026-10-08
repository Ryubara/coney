// SPDX-License-Identifier: GPL-3.0-or-later

// A check against the player's own disc: every animation clip in the WAD (a keyframe chunk 0x00 followed by its
// descriptor 0x02, in standalone resources and in packs) decoded with anim::parseAnimClip and sampled at its start,
// middle and end. Only the clip chunks are read; the walk skips everything else. docs/research/formats/animation.md
// has the expected counts (31,274 occurrences, 1,875 distinct). It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise, so CI never needs the game. It prints counts only, never data
// (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "animation/anim_clip.h"
#include "animation/anim_pose.h"
#include "core/chunk_types.h"
#include "core/name_hash.h"
#include "fileio/disc.h"
#include "fileio/reader.h"
#include "fileio/wad.h"

namespace {

// Totals over the disc.
struct ClipTotals {
    std::uint64_t entries = 0;                                  // chunk containers walked
    std::uint64_t occurrences = 0;                              // 0x00 + 0x02 pairs
    std::uint64_t failures = 0;                                 // pairs that did not decode
    std::uint64_t unpaired = 0;                                 // descriptors without a keyframe chunk before them
    std::uint64_t badSamples = 0;                               // samples with a rotation that is not a unit quaternion
    std::set<std::pair<std::uint32_t, std::uint32_t>> distinct; // (CRC-32 of the descriptor, of the keyframes)
    std::size_t mostKeys = 0;                                   // in one channel, over the distinct clips
    std::uint32_t longest = 0;                                  // last key frame
    std::uint32_t shortest = 0xFFFFFFFF;
    std::uint64_t withBone33 = 0; // distinct clips animating bone 33
    std::uint64_t withoutA = 0;   // distinct clips without a root velocity channel
    std::uint64_t withoutB = 0;   // ... without a root translation channel
    std::uint64_t events = 0;     // over the distinct clips
    std::uint64_t capsule = 0;    // distinct clips with the capsule strike flag 0x10000
    std::uint64_t oddCapsule = 0; // ... not a sweep (gen_rage_sweep, gen_sweep) with flags 0x10004
};

// Reads `size` bytes at the stream's position.
std::expected<std::vector<std::byte>, coney::Error> readBytes(coney::io::Stream& stream, std::uint32_t size) {
    std::vector<std::byte> bytes(size);
    if (auto read = stream.read(bytes); !read) {
        return std::unexpected(std::move(read.error()));
    }
    return bytes;
}

// Decodes one clip, samples it, and adds it to the totals.
void countClip(const std::vector<std::byte>& keyframes, const std::vector<std::byte>& descriptor, ClipTotals& totals) {
    ++totals.occurrences;
    auto clip = coney::anim::parseAnimClip(descriptor, keyframes);
    if (!clip) {
        ++totals.failures;
        std::printf("  a clip failed: %s\n", clip.error().message.c_str());
        return;
    }
    const auto key = std::pair{coney::crc32(std::span<const std::byte>(descriptor)),
                               coney::crc32(std::span<const std::byte>(keyframes))};
    if (!totals.distinct.insert(key).second) {
        return;
    }
    for (const auto& channel : clip->rotations) {
        totals.mostKeys = std::max(totals.mostKeys, channel.size());
    }
    totals.mostKeys = std::max({totals.mostKeys, clip->rootVelocity.size(), clip->rootTranslation.size()});
    totals.longest = std::max(totals.longest, clip->lastKeyFrame());
    totals.shortest = std::min(totals.shortest, clip->lastKeyFrame());
    totals.withBone33 += (clip->boneMask >> 33U) & 1U;
    totals.withoutA += clip->rootVelocity.empty() ? 1 : 0;
    totals.withoutB += clip->rootTranslation.empty() ? 1 : 0;
    totals.events += clip->events.size();
    // Only the two sweeps strike the target's capsule (docs/research/combat.md#capsule-strike).
    if ((clip->flags & coney::anim::kClipCapsuleStrike) != 0) {
        ++totals.capsule;
        const bool sweep = clip->name == "gen_rage_sweep" || clip->name == "gen_sweep";
        totals.oddCapsule += sweep && clip->flags == 0x10004U ? 0 : 1;
    }
    const std::array<coney::anim::Quat, coney::anim::kPoseBones> bind{};
    for (const float t : {0.0F, clip->duration * 0.5F, clip->duration}) {
        const coney::anim::Pose pose = coney::anim::samplePose(*clip, t, bind);
        for (const coney::anim::Quat& q : pose.rotations) {
            if (std::abs(coney::anim::dot(q, q) - 1.0F) > 1e-3F) {
                ++totals.badSamples;
            }
        }
    }
}

// Walks the chunks of one resource from the stream's position, reading only the clip chunks, and counts its clips
// once the whole resource has proved to be one: a header whose third word is 0 and whose second is the sum of the
// chunk sizes, chunks whose third word is 0 and whose size is a multiple of 16 (docs/research/formats/wad-contents.md).
// Returns false when the bytes are not a resource; the entry is then not a chunk container.
bool walkResource(coney::io::Stream& stream, ClipTotals& totals) {
    auto count = stream.readU32Le();
    auto dataSize = stream.readU32Le();
    auto zero = stream.readU32Le();
    if (!count || !dataSize || !zero || *zero != 0 || !stream.skip(4) || *count > 0x10000) {
        return false;
    }
    std::vector<std::pair<std::vector<std::byte>, std::vector<std::byte>>> clips; // (keyframes, descriptor)
    std::vector<std::byte> keyframes;
    bool haveKeyframes = false;
    std::uint64_t unpaired = 0;
    std::uint64_t sum = 0;
    for (std::uint32_t i = 0; i < *count; ++i) {
        auto type = stream.readU32Le();
        auto size = stream.readU32Le();
        auto chunkZero = stream.readU32Le();
        if (!type || !size || !chunkZero || *chunkZero != 0 || !stream.skip(4) ||
            *type >= coney::chunk::kChunkTypeCount || *size % 16 != 0 || *size > stream.remaining()) {
            return false;
        }
        sum += *size;
        if (*type == coney::anim::kAnimKeyframesChunk || *type == coney::anim::kAnimDataChunk) {
            auto bytes = readBytes(stream, *size);
            if (!bytes) {
                return false;
            }
            if (*type == coney::anim::kAnimKeyframesChunk) {
                keyframes = std::move(*bytes);
                haveKeyframes = true;
            } else if (haveKeyframes) {
                clips.emplace_back(keyframes, std::move(*bytes));
                haveKeyframes = false;
            } else {
                ++unpaired;
            }
        } else if (!stream.skip(*size)) {
            return false;
        }
    }
    if (sum != *dataSize) {
        return false;
    }
    for (const auto& [frames, descriptor] : clips) {
        countClip(frames, descriptor, totals);
    }
    totals.unpaired += unpaired;
    return true;
}

} // namespace

TEST_CASE("every animation clip on the disc decodes and samples", "[disc][anim]") {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());

    ClipTotals totals;
    for (const coney::io::WadEntry& entry : wad->index().entries()) {
        auto stream = wad->openEntry(entry);
        REQUIRE(stream.has_value());
        if (stream->size() < 16) {
            continue;
        }
        // A pack is a header with the package marker, then resources; anything else is tried as one resource.
        auto first = stream->readU32Le();
        const bool skipped = first && stream->skip(8);
        auto marker = stream->readU32Le();
        if (!skipped || !marker) {
            continue;
        }
        if (*marker == coney::chunk::kPackageMarker) {
            bool ok = true;
            for (std::uint32_t r = 0; r < *first && ok; ++r) {
                ok = walkResource(*stream, totals);
            }
            totals.entries += ok ? 1 : 0;
        } else if (stream->seek(0)) {
            totals.entries += walkResource(*stream, totals) ? 1 : 0;
        }
    }

    std::printf("anim clips on the disc: %llu containers walked, %llu clip occurrences, %zu distinct, %llu failed, "
                "%llu descriptors without keyframes, %llu bad samples\n",
                static_cast<unsigned long long>(totals.entries), static_cast<unsigned long long>(totals.occurrences),
                totals.distinct.size(), static_cast<unsigned long long>(totals.failures),
                static_cast<unsigned long long>(totals.unpaired), static_cast<unsigned long long>(totals.badSamples));
    std::printf("  distinct clips: at most %zu keys in a channel, last key frame %u to %u, %llu animate bone 33, "
                "%llu without section A, %llu without section B, %llu events, %llu capsule strikes (%llu not a "
                "sweep)\n",
                totals.mostKeys, totals.shortest, totals.longest, static_cast<unsigned long long>(totals.withBone33),
                static_cast<unsigned long long>(totals.withoutA), static_cast<unsigned long long>(totals.withoutB),
                static_cast<unsigned long long>(totals.events), static_cast<unsigned long long>(totals.capsule),
                static_cast<unsigned long long>(totals.oddCapsule));
    CHECK(totals.occurrences > 0);
    CHECK(totals.capsule > 0);
    CHECK(totals.oddCapsule == 0);
    CHECK(totals.failures == 0);
    CHECK(totals.unpaired == 0);
    CHECK(totals.badSamples == 0);
}
