// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A synthetic character data resource with every clip the player's locomotion plays, built byte by byte: for the
// animation controller's and the human's tests. Each clip is still (no rotation channels), with a root displacement
// that gives it a round speed, and the start clips carry a constant root velocity (section A). No game data.

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "characters/character_data.h"
#include "core/chunk_system.h"
#include "fileio/stream.h"
#include "human/human_animator.h"
#include "support/character_fixtures.h"

namespace coney::test {

/// One clip of the synthetic locomotion set.
struct LocomotionClip {
    std::uint32_t id = 0;
    float speed = 0.0F;           ///< Root displacement along +y over one second of clip.
    float duration = 1.0F;        ///< Seconds.
    float rootVelocity = 0.0F;    ///< Section A's constant y velocity, m/s; 0 for no section A.
    std::uint16_t rangeFlags = 0; ///< The Anim Range List's rate flags.
};

/// The synthetic set: idle (388), sneak 1.2 (407), walk 1.5 (408), jog 4 (409), run 7.5 (410), sprint 10 (411), all
/// looping at rate 1 (flag 0x1000) but the idle; the walk start (413, 0.3 s, root velocity 1.0) and the run start
/// (414, 0.3 s, root velocity 4.0) at rate 0.75; the drop cycle (428).
inline std::vector<LocomotionClip> locomotionClips() {
    return {
        {.id = 388, .speed = 0.0F, .duration = 2.0F, .rootVelocity = 0.0F, .rangeFlags = 0},
        {.id = 407, .speed = 1.2F, .duration = 1.0F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 408, .speed = 1.5F, .duration = 1.0F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 409, .speed = 4.0F, .duration = 0.5F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 410, .speed = 7.5F, .duration = 0.5F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 411, .speed = 10.0F, .duration = 0.5F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 413, .speed = 0.3F, .duration = 0.3F, .rootVelocity = 1.0F, .rangeFlags = 0},
        {.id = 414, .speed = 1.2F, .duration = 0.3F, .rootVelocity = 4.0F, .rangeFlags = 0},
        {.id = 428, .speed = 0.0F, .duration = 0.8F, .rootVelocity = 0.0F, .rangeFlags = 0},
    };
}

/// The bytes of a character data resource holding `clips` in order, its Anim Range List and its Character Data
/// table, which gives clip i the slot value (count - 1 - i): the last clip loaded answers the smallest value.
inline Bytes locomotionResource(const std::vector<LocomotionClip>& clips) {
    std::vector<Bytes> chunks;
    for (const LocomotionClip& clip : clips) {
        // Section A: one key holding the root velocity (stored y × 1023); section B: the pelvis at rest.
        Bytes sectionA;
        if (clip.rootVelocity != 0.0F) {
            sectionA = clipKeys({{0, 0, static_cast<std::int16_t>(clip.rootVelocity * 1023.0F), 0}});
        }
        const Bytes sectionB = clipKeys({{0, 0, 0, 0}});
        Bytes keys = sectionA;
        keys.append(sectionB.span());
        ClipFields fields;
        fields.name = "synthetic";
        fields.displacementY = clip.speed * clip.duration;
        fields.duration = clip.duration;
        chunks.push_back(chunk(anim::kAnimKeyframesChunk, keys));
        chunks.push_back(chunk(anim::kAnimDataChunk, clipDescriptor(fields, sectionA.size(), sectionB.size(), 0)));
    }
    // The Anim Range List: a count, then 722 records of 16 bytes, flags at +0x0e.
    Bytes rangeList;
    rangeList.u32(static_cast<std::uint32_t>(characters::kAnimIds));
    for (std::size_t id = 0; id < characters::kAnimIds; ++id) {
        std::uint16_t flags = 0;
        for (const LocomotionClip& clip : clips) {
            flags = clip.id == id ? clip.rangeFlags : flags;
        }
        rangeList.fill(14, 0).u16(flags);
    }
    // The Character Data table.
    std::array<std::uint32_t, characters::kAnimIds> slots{};
    slots.fill(characters::kDefaultAnimSlot);
    for (std::size_t i = 0; i < clips.size(); ++i) {
        slots.at(clips[i].id) = static_cast<std::uint32_t>(clips.size() - 1 - i);
    }
    Bytes table;
    table.u32(0).u32(0);
    for (const std::uint32_t slot : slots) {
        table.u32(slot);
    }
    table.padTo(characters::kCharacterDataBytes);
    chunks.push_back(chunk(characters::kAnimRangeListChunk, rangeList));
    chunks.push_back(chunk(characters::kCharacterDataChunk, table));

    Bytes all;
    std::uint32_t dataBytes = 0;
    for (const Bytes& c : chunks) {
        dataBytes += static_cast<std::uint32_t>(c.size() - 16);
    }
    all.u32(static_cast<std::uint32_t>(chunks.size())).u32(dataBytes).fill(8, 0);
    for (const Bytes& c : chunks) {
        all.append(c.span());
    }
    return all;
}

/// The synthetic set loaded through the chunk system into a CharacterData, requiring success.
inline characters::CharacterData locomotionData(const std::vector<LocomotionClip>& clips = locomotionClips()) {
    const Bytes bytes = locomotionResource(clips);
    chunk::ChunkHandlerTable table;
    characters::addCharacterDataHandlers(table);
    chunk::ChunkStacks stacks;
    io::MemoryStream stream(bytes.span());
    REQUIRE(chunk::loadContainer(stream, table, stacks).has_value());
    auto object = stacks.popObject<characters::CharacterDataObject>();
    REQUIRE(object.has_value());
    return std::move((*object)->data());
}

/// The bind rotations a pose starts from: the identity for every bone.
inline std::array<anim::Quat, anim::kPoseBones> identityBind() { return {}; }

} // namespace coney::test
