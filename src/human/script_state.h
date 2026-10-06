// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>

// What the level scripts keep on a human besides its flags (human/human_flags.h): the arrest, the pad's lock and its
// disabled commands, the money and the item it carries, the callbacks, the marker icon, the head-look and the
// animation overrides. Each field cites where the original keeps it. Some only wait for a subsystem Coney does not
// have yet (the objects in hands, the icons, the head-look controller); the fields say so.
// Research: docs/references/bindings/character.md, docs/research/characters.md#the-record

namespace coney::human {

/// The head-look a script asked for (`HuSetLookTarget`, through the controller at `+0x284`).
struct LookOrder {
    double target = 0;         ///< The handle looked at.
    std::int64_t timeMs = -1;  ///< How long; -1 for no limit.
    float weight = 1.0F;       ///< Passed to the controller (its role is inferred).
    std::uint32_t options = 0; ///< `0x20` and `0x40` (their meaning is not traced).
};

/// The animation slots `HuUseAnim` can replace, by its slot argument: the idle (anim `0x184`), `0x198`, `0x19a` and
/// `0x29c`.
inline constexpr std::array<std::uint32_t, 4> kUseAnimIds{0x184, 0x198, 0x19a, 0x29c};

/// The scripts' state on one human.
struct ScriptState {
    /// Arrested (state `0x20000`, `HuSetArrested`): **Coney stand-in**, the arrest's clips are not researched, so an
    /// arrested human stands still and does nothing until released.
    bool arrested = false;
    /// The pad's buttons locked (per-player `+0x1e`, `HuLockPad`): its commands, sprint and actions are not taken.
    bool padLocked = false;
    /// The pad's left stick locked (per-player `+0x1f`, `HuLockPadMovement`): it reads as centred, the buttons still
    /// act.
    bool movementLocked = false;
    /// The blob shadow drawn (character instance `+0x2b4`, `HuShadow`). **Coney choice** until set (the default is not
    /// traced): drawn.
    bool shadow = true;
    /// The paint colour it tags with (`+0x640`, `HuTagColor`), `0xRRGGBBAA`.
    std::uint32_t tagColour = 0;
    /// The commands disabled for the pad that drives the human (`EnableCommand`), a bit per command id 1-63.
    std::uint64_t disabledCommands = 0;
    /// Can be pushed aside by others (`+0x3bf`, `HuSetPushable`). **Coney stand-in**: Coney's bodies do not push
    /// each other yet, so it is only kept.
    bool pushable = true;
    /// Can be picked as a target, as its gang allows (brain `+0x120`, `GangSetTargetable`): kept on the human so the
    /// target search, which sees humans, reads it.
    bool targetable = true;
    /// Money carried, 0-999 (`+0x370`, `HuSetMoney`).
    int money = 0;
    /// The object it carries and drops, and the chance (`+0x257`, `+0x278`; `HuSetCarriedItem`); "none" for nothing.
    std::string carriedItem;
    int carriedChance = 0;
    /// The Lua function called when it mugs someone (`+0x5a4`, `HuSetMugCallback`); empty for none.
    std::string mugCallback;
    /// The marker spinning above its head and its spawn number (`+0x360`, `HuAttachSpinningIcon`); empty for none.
    /// **Coney stand-in**: the icon object is not drawn yet.
    std::string icon;
    int iconParam = 0;
    /// The object in its hands (`+0x338`) by handle, and its name; 0 (NilHandle) for none. **Coney stand-in**: no
    /// object is drawn or used in a hand yet.
    double heldObject = 0;
    std::string heldObjectName;
    /// The head-look asked for, if any. **Coney stand-in**: no head-look controller yet.
    bool looking = false;
    LookOrder look;
    /// The dynamic clips replacing kUseAnimIds (`+0x3c8`, `HuUseAnim`); empty for none. The play mode plays slot 0's
    /// in place of the idle (Human::setIdleClip()); the other slots are only kept.
    std::array<std::string, kUseAnimIds.size()> animOverrides;
    /// May say speech commands (`+0x199`, `HuEnableSoundCommands`): clear, `SoundPlayCommand` says nothing for it.
    bool soundCommands = true;
    /// The pocket (`HuPutItemInPocket`, `HuRemoveItemInPocket`): the inventory item id (`+0x250`, 0 for none) and the
    /// count (`+0x254`, a byte). **Coney stand-in**: no mugging or knock-down drop reads it yet, so it is only kept.
    int pocketItem = 0;
    int pocketCount = 0;
};

} // namespace coney::human
