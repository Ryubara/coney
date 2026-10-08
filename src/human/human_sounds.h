// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "animation/anim_math.h"

namespace coney::human {

/// Collision materials (`MATERIAL.*` of enum_preload.lua) the humans' sounds name (docs/research/sound-events.md).
namespace material {
inline constexpr std::uint32_t kConcrete = 5;
inline constexpr std::uint32_t kShoe = 8;
inline constexpr std::uint32_t kFist = 9;
inline constexpr std::uint32_t kTorso = 10;
inline constexpr std::uint32_t kCarHood = 13;
inline constexpr std::uint32_t kHead = 17;
inline constexpr std::uint32_t kHuman = 26;
inline constexpr std::uint32_t kBag = 108;
inline constexpr std::uint32_t kJab = 111;
inline constexpr std::uint32_t kBigPunch = 112;
inline constexpr std::uint32_t kBigKick = 113;
inline constexpr std::uint32_t kBlock = 127;
inline constexpr std::uint32_t kTorsoProne = 159;
inline constexpr std::uint32_t kBossFist = 183;
inline constexpr std::uint32_t kDead = 184;
} // namespace material

/// The clip event type that carries an animation sound id (`SA.*`) in its word at +8
/// (docs/research/sound.md#anim-sounds).
inline constexpr std::uint16_t kEventAnimSound = 11;

/// A sound a human's update asks for. The human only reports it; the game's sound (audio) chooses and plays the sounds
/// (docs/research/sound-events.md), as the original's human messages and impact calls hand them to the audio manager.
struct HumanSound {
    enum class Kind : std::uint8_t {
        Anim,   ///< Clip event 11: the animation sound `animSound` (`Human_OnAnimSoundEvent`, `0x0021f700`).
        Impact, ///< A hit's or a contact's material pair (`Human_PlayImpactSound`, `0x00220ac8`).
    };
    Kind kind = Kind::Anim;
    std::uint32_t animSound = 0; ///< Anim: the `SA` id.
    std::uint32_t material1 = 0; ///< Impact: the striking material.
    std::uint32_t material2 = 0; ///< Impact: the struck material.
    float volume = 1.0F;         ///< Impact: the caller's volume.
    bool victimDown = false;     ///< Impact: the victim is knocked down (TORSO and HEAD become TORSO_PRONE).
    bool ownerIsPlayer = false;  ///< Impact: the human it sounds at (its owner) is a player.
    anim::Vec3 at;               ///< Impact: where it sounds (the owner's feet).
    /// Anim from a scene's role clip: where the scene holds the human, and the material under those feet, in place of
    /// his own (a scene poses him without moving his body).
    std::optional<anim::Vec3> sceneFeet;
    std::uint8_t sceneGround = 0;
};

/// The striking material of a landed strike (`Hit_ResolveBlock`, `0x00220df0`): by the striking limb and the hit's
/// strength `strength` (0-3): the head's 17 `HEAD`; a hand's `JAB`, `FIST`, `BIGPUNCH`, `BIGPUNCH`; a foot's `SHOE`,
/// `SHOE`, `BIGKICK`, `BIGKICK`; a boss-class attacker's punches `BOSS_FIST`.
enum class StrikeLimb : std::uint8_t { Hand, Foot, Head, Other };
[[nodiscard]] std::uint32_t strikeMaterial(StrikeLimb limb, int strength, bool bossClass);

/// The limb a strike clip strikes with, from its name: **Coney's stand-in** for the striking shape's bone (bones
/// 5-16 the head, 17-27 the forearms and hands, 28-33 the shins and feet, docs/research/sound-events.md#strike-human),
/// which Coney's reach-based strikes do not track: a name with `kick`, `stomp` or `knee` strikes with a foot, one with
/// `headbutt` or `head_butt` with the head, any other with a hand.
[[nodiscard]] StrikeLimb strikeLimbOfClip(std::string_view clipName);

} // namespace coney::human
