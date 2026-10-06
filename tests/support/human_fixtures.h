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
    float reach = 0.0F;           ///< A type-8 event's vector along +y (a climb clip's reach); 0 for no event.
    bool knockdown = false;       ///< A type-7 event (a reaction that knocks the victim down).
    float pairX = 0.0F;           ///< The type-8 event's x (with `reach` as its y: a pair event's offset).
    std::vector<std::array<std::uint16_t, 2>> markers{}; ///< More events with no vector: (frame, type) each.
};

/// The synthetic climb clips: for each of the four climbs (437 fence, 443 short fence, 449 wall, 455 short wall), a
/// standing chain (reach 0.8) and a running one three ids on (reach 2.0, the first clip moving 3 m/s forward). A
/// fence's second and third clips move the body by their root (3 m/s, 1 m/s); a wall's second clip has a 1 m
/// displacement and no root velocity, as the disc's wall clips have. All play at rate 0.75 (no range flag).
inline std::vector<LocomotionClip> climbClips() {
    std::vector<LocomotionClip> clips;
    for (const std::uint32_t first : {437U, 443U, 449U, 455U}) {
        const bool fence = first < 449U;
        for (const std::uint32_t form : {0U, 3U}) {
            const bool running = form != 0U;
            clips.push_back({.id = first + form,
                             .speed = 0.0F,
                             .duration = 0.3F,
                             .rootVelocity = running ? 3.0F : 0.0F,
                             .rangeFlags = 0,
                             .reach = running ? 2.0F : 0.8F});
            clips.push_back({.id = first + form + 1,
                             .speed = fence ? 0.0F : 1.0F / 0.6F,
                             .duration = 0.6F,
                             .rootVelocity = fence ? 3.0F : 0.0F,
                             .rangeFlags = 0,
                             .reach = 0.5F});
            clips.push_back({.id = first + form + 2,
                             .speed = 0.0F,
                             .duration = 0.3F,
                             .rootVelocity = fence ? 1.0F : 0.0F,
                             .rangeFlags = 0,
                             .reach = 0.0F});
        }
    }
    return clips;
}

/// The synthetic set: idle (388), sneak 1.2 (407), walk 1.5 (408), jog 4 (409), run 7.5 (410), sprint 10 (411), all
/// looping at rate 1 (flag 0x1000) but the idle; the walk start (413, 0.3 s, root velocity 1.0) and the run start
/// (414, 0.3 s, root velocity 4.0) at rate 0.75; the run stop (417, 0.6 s, root velocity 2.0); the drop cycle (428);
/// the jump loop (434), the jump end (435, root velocity 1.0) and the jump end running (436, root velocity 4.0); and
/// climbClips().
inline std::vector<LocomotionClip> locomotionClips() {
    std::vector<LocomotionClip> clips{
        {.id = 388, .speed = 0.0F, .duration = 2.0F, .rootVelocity = 0.0F, .rangeFlags = 0},
        {.id = 407, .speed = 1.2F, .duration = 1.0F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 408, .speed = 1.5F, .duration = 1.0F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 409, .speed = 4.0F, .duration = 0.5F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 410, .speed = 7.5F, .duration = 0.5F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 411, .speed = 10.0F, .duration = 0.5F, .rootVelocity = 0.0F, .rangeFlags = 0x1000},
        {.id = 413, .speed = 0.3F, .duration = 0.3F, .rootVelocity = 1.0F, .rangeFlags = 0},
        {.id = 414, .speed = 1.2F, .duration = 0.3F, .rootVelocity = 4.0F, .rangeFlags = 0},
        {.id = 417, .speed = 1.0F, .duration = 0.6F, .rootVelocity = 2.0F, .rangeFlags = 0},
        {.id = 428, .speed = 0.0F, .duration = 0.8F, .rootVelocity = 0.0F, .rangeFlags = 0},
        {.id = 434, .speed = 0.0F, .duration = 1.0F, .rootVelocity = 0.0F, .rangeFlags = 0},
        {.id = 435, .speed = 0.5F, .duration = 0.3F, .rootVelocity = 1.0F, .rangeFlags = 0},
        {.id = 436, .speed = 2.0F, .duration = 0.3F, .rootVelocity = 4.0F, .rangeFlags = 0},
    };
    const std::vector<LocomotionClip> climbs = climbClips();
    clips.insert(clips.end(), climbs.begin(), climbs.end());
    return clips;
}

/// The synthetic combat clips, all still but 74: the attacks (11-17, 19, 0.6 s), the grab (69, 71-75, 0.3 s; the holds
/// 82-85, 1 s), the grab strikes and their reactions (51-56, 57-58, 0.5 s), the spins (78-81, 0.4 s), the let-go
/// (94, 95, 0.4 s), the throw and its reaction (147, 148, 0.6 s), the tackle (2, 4-6, 0.4 s), the ground (195, 196,
/// 199, 207, 210, 212), the mount (118, 119, 219-226, 244, 245, 248, 249; 0.4 s), the reactions 268-303 (0.4 s;
/// 288-303, the heavy and crushing ones, knock down), the stun (356 loop, 357 end), the fight idle (358), the miss's
/// 389 and the block (606, 607, looping).
inline std::vector<LocomotionClip> combatClips() {
    std::vector<LocomotionClip> clips;
    // A still clip of `id` lasting `duration`, looping at rate 1 with `loop`.
    const auto still = [&clips](std::uint32_t id, float duration, bool loop = false, bool knockdown = false) {
        clips.push_back({.id = id,
                         .speed = 0.0F,
                         .duration = duration,
                         .rootVelocity = 0.0F,
                         .rangeFlags = static_cast<std::uint16_t>(loop ? 0x1000 : 0),
                         .reach = 0.0F,
                         .knockdown = knockdown});
    };
    // The attacks, each with phase events as an attack's clip has them (docs/research/tasks.md#held-flags): the first
    // and second of a chain open a window (0x2c) at frame 5, every one ends (0x2d) at 12 and recovers (0x48) at 13.
    for (const std::uint32_t id : {11U, 12U, 13U, 14U, 15U, 16U, 17U, 19U}) {
        still(id, 0.6F);
        if (id == 11U || id == 12U || id == 16U) {
            clips.back().markers.push_back({5, 0x2c});
        }
        clips.back().markers.push_back({12, 0x2d});
        clips.back().markers.push_back({13, 0x48});
    }
    for (const std::uint32_t id : {69U, 71U, 72U, 73U, 75U}) {
        still(id, 0.3F);
    }
    // The strong grapple's connecting strikes and their reactions (front 657 / 658, rear 659 / 660).
    for (const std::uint32_t id : {657U, 658U, 659U, 660U}) {
        still(id, 0.4F);
    }
    // The rear connecting clip carries the grabber 0.8 m forward (2.65 m/s for 0.3 s of clip), closing the gap from
    // its reach (1.018 m) to the rear hold's (0.222 m) as the disc's 74 and 75 do between them.
    clips.push_back({.id = 74,
                     .speed = 0.0F,
                     .duration = 0.3F,
                     .rootVelocity = 2.65F,
                     .rangeFlags = 0,
                     .reach = 0.0F,
                     .knockdown = false});
    for (const std::uint32_t id : {82U, 83U, 84U, 85U, 196U, 207U, 356U, 358U, 606U, 607U}) {
        still(id, 1.0F, true);
    }
    // The mount's loop carries its pair event: the victim 0.120 m to the left and 0.032 m ahead.
    clips.push_back({.id = 210,
                     .speed = 0.0F,
                     .duration = 1.0F,
                     .rootVelocity = 0.0F,
                     .rangeFlags = 0x1000,
                     .reach = 0.032F,
                     .knockdown = false,
                     .pairX = -0.120F});
    // The combat walk's eight loops.
    for (std::uint32_t id = 380; id <= 387; ++id) {
        still(id, 0.7F, true);
    }
    // The block's reactions, and its duck with the counter window's events 0x25 on frames 6-13 (as the disc's 616).
    for (std::uint32_t id = 608; id <= 615; ++id) {
        still(id, 0.3F);
    }
    still(616, 0.7F);
    for (std::uint16_t frame = 6; frame <= 13; ++frame) {
        clips.back().markers.push_back({frame, 0x25});
    }
    // The duck counters.
    for (std::uint32_t id = 617; id <= 620; ++id) {
        still(id, 0.5F);
    }
    // The grabbed player's moves and their grabber sides: the counter, the reversals, the struggles, the escapes and
    // the strikes back.
    for (const std::uint32_t id :
         {76U, 77U, 90U, 91U, 92U, 93U, 96U, 97U, 100U, 101U, 104U, 105U, 108U, 109U, 112U, 113U, 116U, 117U}) {
        still(id, 0.5F);
    }
    for (std::uint32_t id = 51; id <= 60; ++id) {
        still(id, 0.5F);
        // The power strike opens its window (0x2c) at frame 5 for its extension and ends it (0x2d) at 12.
        if (id == 57U) {
            clips.back().markers.push_back({5, 0x2c});
            clips.back().markers.push_back({12, 0x2d});
        }
    }
    for (const std::uint32_t id :
         {78U,  79U,  80U,  81U,  94U,  95U,  2U,   4U,   5U,   6U,   195U, 199U, 212U, 357U, 389U,
          118U, 119U, 219U, 220U, 221U, 222U, 223U, 224U, 225U, 226U, 244U, 245U, 248U, 249U}) {
        still(id, 0.4F);
    }
    still(147, 0.6F);
    still(148, 0.6F);
    for (std::uint32_t id = 268; id <= 303; ++id) {
        still(id, 0.4F, false, id >= 288);
    }
    return clips;
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
        // A climb clip's reach or a pair's offset: one type-8 event at frame 0 whose vector is (pairX, reach, 0),
        // stored as a position key.
        if (clip.reach > 0.0F) {
            Bytes event;
            event.u16(0).u16(8).u16(0).u16(0);
            event.u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(clip.pairX * 1023.0F)));
            event.u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(clip.reach * 1023.0F))).u16(0);
            event.u16(0).u16(0).u16(0).u16(0).u16(0);
            keys.append(event.span());
        }
        // A knockdown: one type-7 event at frame 0.
        if (clip.knockdown) {
            Bytes event;
            event.u16(0).u16(7);
            event.fill(20, 0);
            keys.append(event.span());
        }
        ClipFields fields;
        fields.name = "synthetic";
        fields.displacementY = clip.speed * clip.duration;
        fields.duration = clip.duration;
        // Markers: an event each, with no vector.
        for (const std::array<std::uint16_t, 2>& marker : clip.markers) {
            Bytes event;
            event.u16(marker[0]).u16(marker[1]);
            event.fill(20, 0);
            keys.append(event.span());
        }
        fields.events = static_cast<std::uint16_t>((clip.reach > 0.0F ? 1U : 0U) + (clip.knockdown ? 1U : 0U) +
                                                   clip.markers.size());
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
