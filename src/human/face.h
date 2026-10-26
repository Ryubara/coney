// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "core/game_random.h"

// A human's face: the mood and talk state of its record (`+0x1c`) and the looping face clips that play on the
// character instance's queue, under the body's clips. The face clips are anim ids 698-721, three per state: an even
// state is a mood's resting face (angry, neutral, scared, excited), the odd state above it the same mood speaking, a
// mouth that moves. A line that starts playing turns the state odd; its end turns it back. The face clips pose the face
// bones (7-15: brow, jaw, lips, mouth corners, eyelid) of the default pose that every bone the body's clips leave out
// takes, so the body's clips decide everything else.
// Research: docs/research/face.md

namespace coney::characters {
class AnimSet;
}

namespace coney::human {

class Human;

/// The first face clip's anim id (`ANIM_FACE_ANGRY_01`).
inline constexpr std::size_t kFirstFaceAnim = 698;
/// Face clips per state: a state `s` loops a random one of ids `698 + 3s` to `700 + 3s`.
inline constexpr std::size_t kFaceClipsPerState = 3;
/// The face states: four moods, each with its speaking state above it.
inline constexpr int kFaceStates = 8;

/// A human's mood as its face shows it: the even talk states. A brain sets it (ai::faceMoodOf()).
enum class FaceMood : std::uint8_t {
    Angry = 0,   ///< 698-700 (701-703 speaking): fighting, chasing, a hostile goal.
    Neutral = 2, ///< 704-706 (707-709): the default for an AI human.
    Scared = 4,  ///< 710-712 (713-715): down, cuffed, on fire, fleeing.
    Excited = 6, ///< 716-718 (719-721): bums, peddlers, rioters, dealers.
};

/// The record's talk state (`+0x1c`): a mood, plus one while a line plays. The record is cleared
/// when the human is made (`Human_Init`), so a new human's face is angry until its brain sets the mood.
class FaceState {
  public:
    /// The state, 0-7.
    [[nodiscard]] int value() const { return m_value; }
    /// Whether a line is playing (the state is odd).
    [[nodiscard]] bool talking() const { return (m_value & 1) != 0; }
    /// Sets the mood, keeping the speaking half when a line plays (an odd state takes the new mood's odd state).
    /// @orig 0x002235a0 Human_SetTalkState (unknown)
    void setMood(FaceMood mood);
    /// A line started playing: an even state becomes the odd one above it.
    /// @orig 0x002234f8 Human_BeginTalkPose (unknown)
    void beginTalk();
    /// The line ended: an odd state becomes the even one below it.
    /// @orig 0x00223548 Human_EndTalkPose (unknown)
    void endTalk();
    /// The first anim id of the state's face clips.
    [[nodiscard]] std::size_t firstClip() const {
        return kFirstFaceAnim + kFaceClipsPerState * static_cast<std::size_t>(m_value);
    }
    /// The last anim id of the state's face clips.
    [[nodiscard]] std::size_t lastClip() const { return firstClip() + kFaceClipsPerState - 1; }

  private:
    int m_value = 0; // +0x1c, cleared by Human_Init
};

/// The face's clips: a looping face task of the state's three clips and, when the state changes, a cross-fade from the
/// old one. Pure: it holds anim ids and times, and reads the clips from the human's anim set when asked.
class FaceLayer {
  public:
    /// A random whole number in [low, high], from the face's random stream (`0x006eb870`).
    using Draw = std::function<int(int low, int high)>;

    /// Starts the state's face clips when they are not the ones playing and the face is shown: the first time a
    /// face task of the state's three ids from the first; later the new task under a fade of the old one over 1/30 s
    /// (none when the new state is a speaking one, and never longer than the new first clip takes to play). `shown` is
    /// the queued pose's rule (Human_UsesQueuedPose): while it is false nothing changes.
    /// @orig 0x00259368 Human_UpdateFaceClip (unknown)
    void update(const FaceState& state, const characters::AnimSet& anims, bool shown);
    /// Moves the clips on by `seconds`: on the step whose rounded frame reaches a clip's frame count the loop takes a
    /// random one of its ids (`draw`) from its start instead, at the playback rate its first id gave it; the old loop
    /// plays on under the fade until the first step past the fade's length drops it.
    /// @orig 0x0010b6e0 FaceTask_Update (unknown)
    /// @orig 0x0010b858 FaceBlendOut_Update (unknown)
    void advance(float seconds, const characters::AnimSet& anims, const Draw& draw);
    /// The default pose with the face clips sampled over `reference`: the rotations every bone the body's clips leave
    /// out takes. Only the head and the face bones up to 13 take the face's sample; the eyelid (14), the upper lip
    /// (15) and every other bone keep `reference`. Without a face task, `reference` itself.
    /// @orig 0x00176d60 CharacterInstance_Sample (unknown)
    /// @orig 0x001047d0 Instance_CopyRootSlots (unknown)
    [[nodiscard]] std::array<anim::Quat, anim::kPoseBones>
    defaults(std::span<const anim::Quat, anim::kPoseBones> reference, const characters::AnimSet& anims) const;
    /// The anim id the state's face clips start from (record `+0x24`), or nothing before the first.
    [[nodiscard]] std::optional<std::size_t> current() const { return m_current; }
    /// The id the loop plays now, or nothing.
    /// @coney-only a query for the tests and the debug tools; the original keeps it in the task's cursor
    /// @coney-only a query for the tests and the debug tools; the original keeps it in the task's cursor
    [[nodiscard]] std::optional<std::size_t> playing() const;
    /// The time into the clip playing, seconds of clip; 0 with none.
    [[nodiscard]] float loopTime() const { return m_loop ? m_loop->time : 0.0F; }
    /// Whether a fade from an older loop is under way.
    [[nodiscard]] bool fading() const { return m_old.has_value(); }

  private:
    // One face task (type 15): its id range, the clip it plays, the time in it and its rate.
    struct Loop {
        std::size_t first = 0;
        std::size_t last = 0;
        std::size_t id = 0;
        float time = 0.0F;
        float rate = 1.0F;
    };
    // Moves `loop` on by `seconds`, taking a new random id at a clip's end.
    // @orig 0x0010b6e0 FaceTask_Update (unknown)
    // @orig 0x0010b6e0 FaceTask_Update (unknown)
    static void advanceLoop(Loop& loop, float seconds, const characters::AnimSet& anims, const Draw& draw);

    std::optional<std::size_t> m_current; // record +0x24 (-1: none)
    std::optional<Loop> m_loop;
    std::optional<Loop> m_old;  // the loop being faded out (type 16)
    float m_fadeSeconds = 0.0F; // the fade's length
    float m_fadeElapsed = 0.0F;
};

/// What every human of one step shares for its face (human::Humans owns it): whose face shows and the random stream the
/// face clips draw from.
struct FaceRules {
    /// Whether a human's face may show (`Human_UsesQueuedPose` without its down-or-dead test, which the human makes):
    /// in the original, a human in no scene that is an AI Warrior (brain type 3) or within 17.5 m of the camera.
    /// Empty: every face shows (**Coney stand-in** for a step with no camera: the sandbox, the tests).
    std::function<bool(const Human& human)> shown;
    /// The face clips' stream, the first of the original's ten (`0x006eb870`), which nothing else here draws from.
    GameRandom random;
};

/// How near the camera a human's face clips show, metres: a quarter of the 70 m draw distance (`0x0050cc60`), unless
/// the human is an AI Warrior (Human_UsesQueuedPose, `0x00223440`).
inline constexpr float kFaceShownDistance = 70.0F * 0.25F;

/// The face's fade when the state changes into a resting one, seconds (1/30: `0x3d088889`).
inline constexpr float kFaceFadeSeconds = 1.0F / 30.0F;

} // namespace coney::human
