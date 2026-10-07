// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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

/// How many dynamic animation slots a human has (`+0x3c8`, docs/research/animation.md#dynamic-slots).
inline constexpr std::size_t kAnimOverrideSlots = 7;

/// One dynamic animation slot (0x28 bytes at `+0x3c8`): the clip's file name, empty for a free slot, and the anim id it
/// stands for (`+0x24`).
struct AnimOverride {
    std::string clip;
    std::uint32_t animId = 0;
};

/// The scripts' state on one human.
// The fields stay beside the bindings that set them rather than ordered by size: there is one per human, so the few
// dozen padding bytes cost nothing worth the lost readability.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
struct ScriptState {
    /// Set up for interrogation (`HuSetInterrogation`): its four speech files (`+0x590`-`+0x59c`, empty for none) and
    /// the Lua function the success calls (`+0x5a0`; empty: not interrogable); the icon is interrogationIcon.
    /// **Coney stand-in**: the grab's shake-down is not built, so it is only kept.
    std::array<std::string, 4> interrogationLines;
    std::string interrogationCallback;
    /// Arrested (state `0x20000`, `HuSetArrested`): it loops 320 `ANIM_ARRESTED_IDLE` where it stands and does nothing
    /// until released (docs/research/crimes.md#arrest).
    bool arrested = false;
    /// Knocked out (state `0x40000`, `HuSetConscious(h, false)`): not alive, and its brain off until brought round.
    /// **Coney stand-in**: the knocked-out and get-up clips are not played, and it does not wake by itself after the
    /// original's 14 s (whose reader is not on the page), so it lies as it is until a script wakes it.
    bool knockedOut = false;
    /// Wounded (state `0x10000`, `HuSetWounded`), and (woundedUntilMs) the game time it was wounded at plus 14 s
    /// (record `+0xf0`). **Coney stand-in**: the wounded clips (layer `0xd`) are not played and nothing reads the time
    /// yet (its reader is not on the page), so a wounded human only has its health cut.
    bool wounded = false;
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
    /// When the wound's 14 s run out (see `wounded`).
    std::uint64_t woundedUntilMs = 0;
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
    /// The hat he wears (`+0x364`) by handle, and its object type; 0 (NilHandle) for none. The play mode makes its
    /// object (a spawn record of that handle) and draws it on his head (docs/research/characters.md#hats).
    double hat = 0;
    std::string hatName;
    /// The head-look asked for, if any. **Coney stand-in**: no head-look controller yet.
    bool looking = false;
    LookOrder look;
    /// The dynamic clips replacing anim ids (`+0x3c8`, `HuUseAnim` and `HuUseAnyAnim`), set by setAnimOverride(). The
    /// play mode plays the idle's (`0x184`) through Human::setIdleClip() and the others wherever their ids play.
    std::array<AnimOverride, kAnimOverrideSlots> animOverrides;
    /// May say speech commands (`+0x199`, `HuEnableSoundCommands`): clear, `SoundPlayCommand` says nothing for it.
    bool soundCommands = true;
    /// The pocket (`HuPutItemInPocket`, `HuRemoveItemInPocket`): the inventory item id (`+0x250`, 0 for none) and the
    /// count (`+0x254`, a byte). **Coney stand-in**: no mugging or knock-down drop reads it yet, so it is only kept.
    int pocketItem = 0;
    int pocketCount = 0;
    /// May be mugged (`+0x5b0`, `HuSetMug`); the mugging asks it unless the human is set up for interrogation.
    /// **Coney choice** until set (the default is not on the page): true.
    bool muggable = true;
    /// The pedestrian's reaction style (brain `+0x270`, `HuSetPedReaction`); no reader is on the page, so it is only
    /// kept.
    int pedReaction = 0;
    /// An AI human's handcuffs (`+0x378`, 0-9, `HuGiveCuffs`).
    int cuffs = 0;
    /// The unlockable gear a player wears (`HuAttachGear`): brass knuckles on both hands (`dyn_brassknkl`) and
    /// steel-toe boots on both feet (`dyn_steeltoe`). **Coney stand-in**: the gear objects are not drawn yet.
    bool knuckles = false;
    bool boots = false;
    /// Offers the uncuff action on the human (`HuSetUnarrestable`, with flag::kUnarrestable). **Coney stand-in**:
    /// Coney has no context actions yet, so no prompt shows.
    bool uncuffOffered = false;
    /// In the fight stance by a script (`HuSetCombatMode`, state flags `0x3`). **Coney stand-in**: Coney's fight
    /// stance is not a state of its own, so it is only kept.
    bool combatMode = false;
    /// Working out (state flag `0x20000000000`, `HuWorkout`): the human neither moves nor acts by its stick or pad.
    bool workingOut = false;

    /// The clip replacing anim `id`, empty for none (`Human_GetDynamicAnim`'s first look).
    /// @orig 0x00221a00 Human_GetDynamicAnim (unknown)
    [[nodiscard]] std::string_view animOverride(std::uint32_t id) const {
        const auto found = std::ranges::find_if(
            animOverrides, [id](const AnimOverride& slot) { return !slot.clip.empty() && slot.animId == id; });
        return found != animOverrides.end() ? std::string_view(found->clip) : std::string_view();
    }
    /// Puts `clip` in the slot for anim `id`: the slot already holding `id`, else the first free one; an empty `clip`
    /// frees that slot. False when no slot is free, or there is nothing to free.
    /// @orig 0x00221aa0 Human_SetAnimOverride (unknown)
    bool setAnimOverride(std::uint32_t id, std::string_view clip) {
        auto found = std::ranges::find_if(
            animOverrides, [id](const AnimOverride& slot) { return !slot.clip.empty() && slot.animId == id; });
        if (clip.empty()) {
            if (found == animOverrides.end()) {
                return false;
            }
            *found = AnimOverride{};
            return true;
        }
        if (found == animOverrides.end()) {
            found = std::ranges::find_if(animOverrides, [](const AnimOverride& slot) { return slot.clip.empty(); });
        }
        if (found == animOverrides.end()) {
            return false;
        }
        *found = AnimOverride{.clip = std::string(clip), .animId = id};
        return true;
    }
    /// The spinning icon over a human set up for interrogation (`HuSetInterrogation`'s last argument).
    bool interrogationIcon = false;
};

} // namespace coney::human
