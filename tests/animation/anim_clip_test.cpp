// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_clip.h"

#include <cmath>
#include <memory>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/chunk_stacks.h"
#include "support/character_fixtures.h"

using Catch::Approx;
using coney::ErrorCode;
using coney::anim::parseAnimClip;
using coney::test::Bytes;
using coney::test::clipDescriptor;
using coney::test::clipEvent;
using coney::test::ClipFields;
using coney::test::clipKeys;

namespace {

// A clip animating bones 1 and 33: section A one channel of two keys, B one of two, C two channels (bone 1 with
// three keys, bone 33 with one), then one event.
struct TestClip {
    Bytes descriptor;
    Bytes keyframes;
};

// Builds the clip above; `channels` overrides the descriptor's channel count.
TestClip makeClip(std::uint16_t channels = 2) {
    const Bytes a = clipKeys({{0, 0, 1023, 0}, {10, 0, 2046, 0}});
    const Bytes b = clipKeys({{0, 0, 0, 2047}, {30, 1023, 0, 2047}});
    const Bytes c = clipKeys({{0, 0, 0, 0}, {10, 16384, 0, 0}, {20, 0, 0, 0}, {0, 0, 0, 32767}});
    TestClip clip;
    const std::uint64_t mask = (1ULL << 1U) | (1ULL << 33U);
    clip.descriptor = clipDescriptor(ClipFields{.name = "warr_test_walk",
                                                .displacementX = 0.0F,
                                                .displacementY = 0.5F,
                                                .duration = 1.0F,
                                                .mask = mask,
                                                .channels = channels,
                                                .events = 1,
                                                .flags = 0x10004},
                                     a.size(), b.size(), c.size());
    clip.keyframes.append(a.span()).append(b.span()).append(c.span()).append(clipEvent(12, 11).span());
    clip.keyframes.padTo((clip.keyframes.size() + 15) / 16 * 16); // the chunk's padding
    return clip;
}

// Descriptor fields with a mask and a channel count, the rest the fixture's defaults.
ClipFields fieldsWith(std::uint64_t mask, std::uint16_t channels) {
    ClipFields fields;
    fields.name = "bad";
    fields.mask = mask;
    fields.channels = channels;
    return fields;
}

} // namespace

TEST_CASE("a clip's descriptor and sections decode into channels with absolute frames", "[anim_clip]") {
    const TestClip data = makeClip();
    auto clip = parseAnimClip(data.descriptor.span(), data.keyframes.span());
    REQUIRE(clip.has_value());
    CHECK(clip->name == "warr_test_walk");
    CHECK(clip->duration == 1.0F);
    CHECK(clip->displacement.y == 0.5F);
    // The flags at +0x44: the sweeps' 0x10004, a capsule strike.
    CHECK(clip->flags == 0x10004U);
    CHECK((clip->flags & coney::anim::kClipCapsuleStrike) != 0);
    REQUIRE(clip->rootVelocity.size() == 2);
    CHECK(clip->rootVelocity[1].frame == 10);
    CHECK(clip->rootVelocity[1].value.y == Approx(2.0F)); // y / 1023
    REQUIRE(clip->rootTranslation.size() == 2);
    CHECK(clip->rootTranslation[0].value.z == Approx(1.0F)); // z / 2047
    CHECK(clip->rootTranslation[1].value.x == Approx(1.0F)); // x / 1023
    CHECK(clip->rootTranslation[1].frame == 30);

    // Section C: the channels in bone order, split at the key whose delta is 0.
    REQUIRE(clip->rotations.size() == 2);
    REQUIRE(clip->rotationBones.size() == 2);
    CHECK(clip->rotationBones[0] == 1);
    CHECK(clip->rotationBones[1] == 33);
    REQUIRE(clip->rotations[0].size() == 3);
    CHECK(clip->rotations[0][1].frame == 10);
    CHECK(clip->rotations[0][2].frame == 30); // deltas add up
    // x = 16384 / 32768 = 0.5, so w = sqrt(1 - 0.25).
    CHECK(clip->rotations[0][1].value.x == Approx(0.5F));
    CHECK(clip->rotations[0][1].value.w == Approx(std::sqrt(0.75F)));
    CHECK(clip->rotations[0][0].value.w == Approx(1.0F));
    // z = 32767 / 32768: w is the non-negative root, close to 0.
    CHECK(clip->rotations[1][0].value.w >= 0.0F);
    CHECK(clip->rotations[1][0].value.w < 0.01F);
    CHECK(clip->rotationChannel(33) == &clip->rotations[1]);
    CHECK(clip->rotationChannel(2) == nullptr);
    CHECK(clip->lastKeyFrame() == 30);

    REQUIRE(clip->events.size() == 1);
    CHECK(clip->events[0].frame == 12);
    CHECK(clip->events[0].type == 11);
    CHECK(clip->events[0].word == 7);
    CHECK(clip->events[0].position.x == Approx(1.0F));
    CHECK(clip->events[0].position.z == Approx(1.0F));
}

TEST_CASE("a clip with damaged sizes or channels is refused", "[anim_clip]") {
    const TestClip data = makeClip();

    // A descriptor that is not 80 bytes.
    auto shortDescriptor = parseAnimClip(data.descriptor.span().first(79), data.keyframes.span());
    REQUIRE_FALSE(shortDescriptor.has_value());
    CHECK(shortDescriptor.error().code == ErrorCode::Invalid);

    // Keyframes cut before the events end.
    auto cut = parseAnimClip(data.descriptor.span(), data.keyframes.span().first(80));
    REQUIRE_FALSE(cut.has_value());
    CHECK(cut.error().code == ErrorCode::Truncated);

    // A channel count that disagrees with the mask.
    const TestClip miscounted = makeClip(3);
    auto mismatch = parseAnimClip(miscounted.descriptor.span(), miscounted.keyframes.span());
    REQUIRE_FALSE(mismatch.has_value());
    CHECK(mismatch.error().code == ErrorCode::Invalid);

    // A section that does not start with a channel's first key (delta 0).
    const Bytes c = clipKeys({{4, 0, 0, 0}});
    const Bytes descriptor = clipDescriptor(fieldsWith(2, 1), 0, 0, c.size());
    auto noStart = parseAnimClip(descriptor.span(), c.span());
    REQUIRE_FALSE(noStart.has_value());
    CHECK(noStart.error().code == ErrorCode::Invalid);

    // Two channels in section A.
    const Bytes a = clipKeys({{0, 0, 0, 0}, {0, 0, 0, 0}});
    const Bytes twoA = clipDescriptor(fieldsWith(0, 0), a.size(), 0, 0);
    auto doubled = parseAnimClip(twoA.span(), a.span());
    REQUIRE_FALSE(doubled.has_value());
    CHECK(doubled.error().code == ErrorCode::Invalid);

    // A mask bit above bone 33.
    const Bytes high = clipDescriptor(fieldsWith(1ULL << 34U, 1), 0, 0, 8);
    auto beyond = parseAnimClip(high.span(), clipKeys({{0, 0, 0, 0}}).span());
    REQUIRE_FALSE(beyond.has_value());
    CHECK(beyond.error().code == ErrorCode::Invalid);
}

TEST_CASE("the Anim Data handler pops the descriptor and the keyframes and pushes the clip", "[anim_clip]") {
    const TestClip data = makeClip();
    coney::chunk::ChunkStacks stacks;
    stacks.pushChunk(coney::chunk::ChunkData{coney::anim::kAnimKeyframesChunk, 0, data.keyframes.data(), nullptr});
    stacks.pushChunk(coney::chunk::ChunkData{coney::anim::kAnimDataChunk, 0, data.descriptor.data(), nullptr});
    REQUIRE(coney::anim::onAnimDataLoaded(stacks, coney::anim::kAnimDataChunk).has_value());
    CHECK(stacks.chunks().empty());
    auto object = stacks.popObject<coney::anim::AnimClipObject>();
    REQUIRE(object.has_value());
    CHECK((*object)->clip().name == "warr_test_walk");

    // Without the keyframes before it, the handler fails.
    stacks.pushChunk(coney::chunk::ChunkData{coney::anim::kAnimDataChunk, 0, data.descriptor.data(), nullptr});
    CHECK_FALSE(coney::anim::onAnimDataLoaded(stacks, coney::anim::kAnimDataChunk).has_value());
}
