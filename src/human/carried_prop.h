// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "animation/anim_clip.h"
#include "animation/anim_math.h"

// The prop a clip makes a human carry for the length of a move (Rembrandt's spray can in his power move 59), and the
// clip events that make it, place it and remove it.
// Research: docs/research/combat-grabs.md#power-move-props, docs/research/characters.md (human +0x348, +0x34c)

namespace coney::human {

/// Clip event `0x30`: makes the object `dyn_` + the event's text and attaches it at kCarryPoint.
inline constexpr std::uint16_t kEventPropCreate = 0x30;
/// Clip event `0x31`: removes the carried prop when it is of the type the event names.
inline constexpr std::uint16_t kEventPropRemove = 0x31;
/// Clip event `0x37` (message `0xb7`): moves the carried prop to the attach point in the event's word, at the event's
/// position (× the human's scale) and rotation.
inline constexpr std::uint16_t kEventPropPlace = 0x37;
/// Clip event `0x45` (message `0xc5`): stops the sound the human holds (the spray loop,
/// docs/research/sound-events.md#spray-loop).
inline constexpr std::uint16_t kEventStopHeldSound = 0x45;

/// The attach point a new carried prop goes on: `0x13`, pose bone 19, the left hand.
inline constexpr std::size_t kCarryPoint = 0x13;

/// The object type kind (`+0x65`, ObjectType::pickupAnim) a carried prop must have: 5. Kind 6 is ignored by the prop
/// events; any other kind goes into the hand instead.
inline constexpr int kCarriedPropKind = 5;

/// Where event `0x37` put the prop: its pose in the attach point's frame, the position already × the human's scale.
struct PropPlace {
    anim::Vec3 position;
    anim::Quat rotation;
};

/// The prop a human carries (human `+0x348` and `+0x34c`, both one handle once attached). The human keeps what its
/// clips asked for; the level makes the object, draws it at the attach point and removes it when this goes.
struct CarriedProp {
    std::string typeName;            ///< The object type: `dyn_` + the creating event's text (`dyn_s_spraycan`).
    std::size_t point = kCarryPoint; ///< The attach point (pose bone) it hangs from.
    /// Event `0x37`'s place; until one comes, the type's own held offset (`Human_AttachCarriedProp`).
    std::optional<PropPlace> place;
};

/// The object type a prop event names: `dyn_` + its text (`ObjectDb_FindByName` is asked for that name).
[[nodiscard]] inline std::string propTypeName(const anim::ClipEvent& event) {
    return std::string("dyn_") + std::string(anim::eventText(event));
}

} // namespace coney::human
