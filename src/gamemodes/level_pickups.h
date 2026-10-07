// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "animation/anim_math.h"
#include "combat/stick_games.h"
#include "scripting/message_handlers.h"
#include "scripting/script_system.h"
#include "warriors/game_state.h"
#include "world_objects/object_types.h"
#include "world_objects/pickups.h"
#include "world_objects/spawn_records.h"

// A level's loose objects as a player uses them with triangle: an object's interaction prompt, the pick-up search over
// the spawn records, the take (loot into the inventory, a weapon into the hand) and the drop.
// Research: docs/research/crimes.md#triangle, docs/research/combat.md#breakables, docs/research/combat.md#bat,
// docs/research/player-state.md#pickup-callback

namespace coney {

/// What the search chose: the object and where it is.
struct PickupChoice {
    double handle = 0;
    anim::Vec3 position{};
    int clip = 0; ///< The pick-up clip (world_objects::pickupClip()).
};

/// The action object in a player's reach: a kind-1 context record's object and its prompt text.
struct ActionObject {
    double handle = 0;
    std::string prompt; ///< The text `SetMsgHandlerEx` gave it, markup and all.
};

/// What one triangle press came to (TriangleOutcome).
enum class TriangleResult : std::uint8_t {
    Nothing,  ///< Nothing to take and nothing in hand.
    Consumed, ///< An object's message 0 handler took the press.
    PickUp,   ///< Pick up TriangleOutcome::choice.
    Drop,     ///< Drop what is in hand.
};

/// What one triangle press came to, and the object to pick up.
struct TriangleOutcome {
    TriangleResult result = TriangleResult::Nothing;
    PickupChoice choice{};
};

/// What a take did.
enum class TakeResult : std::uint8_t {
    Gone,   ///< The handle names no record still there.
    Loot,   ///< A `TYPE_SPECIAL`: loot and money, the record removed.
    InHand, ///< Any other kind: the human holds it now.
};

/// The pick-up over a level's spawn records, its object types, its scripts, their message handlers (may be null) and
/// the players' state, all of which must outlive it.
class LevelPickups {
  public:
    /// The game's money multiplier for loot (game state `+0x380`, 1.0 in mission 1; what sets it is open).
    static constexpr float kLootMoneyFactor = 1.0F;
    /// A kind-1 context record's reach in the ground plane (`CfgActionDistance`'s default, 1.1 m), and how far from
    /// the feet + 1 m its object may be in height.
    static constexpr float kPromptReach = 1.1F;
    static constexpr float kPromptHeight = 1.5F;
    /// The kinds a player takes whatever he holds: `TYPE_SPECIAL` (12) and 24.
    static constexpr int kKindAnyHands = 24;
    /// What a stolen stereo pays, in dollars.
    static constexpr int kStereoMoney = 15;

    /// Where an object with an interaction prompt but no spawn record is (a tag spot's particle system, a flag);
    /// nothing when it is gone.
    using Locator = std::function<std::optional<anim::Vec3>(double object)>;

    LevelPickups(script::ScriptSystem& scripts, GameState& state, world_objects::SpawnRecords& records,
                 const world_objects::ObjectTypes& types, const script::MessageHandlers* messages = nullptr)
        : m_scripts(scripts), m_state(state), m_records(records), m_types(types), m_messages(messages) {}

    /// Locates the prompt objects that are not spawn records: a `SetMsgHandlerEx` on a tag spot (`level87`) or a
    /// flag registers a kind-1 context record just as one on a loose object does (docs/research/crimes.md#triangle).
    void setLocator(Locator locator) { m_locate = std::move(locator); }

    /// The action object for a player whose feet are at `feet`: of the objects with an interaction prompt, the
    /// nearest within kPromptReach in the ground plane whose height is within kPromptHeight of the feet + 1 m.
    /// Nothing for none. Its text is the player's action prompt and triangle hands it message 0.
    /// **Coney's reading**: the nearest such object stands for the human's current record (`+0x660`; the original
    /// keeps the current one while it still passes and otherwise takes the first of the kind's list).
    /// @orig 0x00418150 ContextActions_Pick (unknown)
    [[nodiscard]] std::optional<ActionObject> actionObject(anim::Vec3 feet) const;

    /// The triangle search (world_objects::searchPickup()) for a human at `feet` facing `facing`, over the records that
    /// are not removed, hidden, held or in a disabled zone, whose type is pickable. Nothing when none qualifies.
    [[nodiscard]] std::optional<PickupChoice> search(anim::Vec3 feet, anim::Vec3 facing,
                                                     const world_objects::SightBlocked& blocked) const;

    /// Steps 4 and 5 of triangle for human `human` at `feet` facing `facing`, `holding` whether something is in hand:
    /// the action object (actionObject()) gets message 0, and a true result ends the press; then every object within
    /// the search's reach gets message 0 in turn, the same way; then the search. Its choice is picked up unless the
    /// human holds something and it is a kind other than 12 or 24; with something in hand and nothing taken, the press
    /// drops it.
    /// @orig 0x0024d810 Pickup_Search (unknown)
    TriangleOutcome triangle(double human, anim::Vec3 feet, anim::Vec3 facing, bool holding,
                             const world_objects::SightBlocked& blocked);

    /// Player `player` (0 or 1) takes object `handle`, at its pick-up clip's event. A `TYPE_SPECIAL` adds item 10
    /// (loot) ×1 with notify, then its value × kLootMoneyFactor in money without, and the record is removed for good.
    /// Any other kind (a bat's 3 among them) is one `Human_PickUpObject` has no case for, so it goes into the hand
    /// (`0x00227010`): its record stays, held. **Coney choices**: the named mission items (model hashes the original
    /// checks first) are not listed, so every `TYPE_SPECIAL` is loot; item 10's pickup sound is not played.
    /// @orig 0x0023bf00 Human_PickUpObject (unknown)
    TakeResult take(double handle, int player);

    /// The object `handle` leaves the hand at `at` (a dropped weapon). **Coney stand-in**: it lands where it is put,
    /// with no physics.
    /// @orig 0x00257f38 Human_DropHeld (unknown)
    void drop(double handle, anim::Vec3 at);

    /// The anim set the type named `typeName` applies in hand (ObjectType::animSet); 0 for none or an unknown type.
    [[nodiscard]] int animSetOf(std::string_view typeName) const;
    /// The type name of object `handle`'s record; empty for none.
    [[nodiscard]] std::string typeOf(double handle) const;
    /// Whether `handle` is an object in a hand.
    [[nodiscard]] bool inHand(double handle) const { return m_inHand.contains(handle); }

    /// Player `player` (0 or 1), human `human`, stole the stereo of car `car`: $15 and a car stereo (item 11), then
    /// `CfgSetSteroTheftHandler`'s callback with the human and the car. **Coney's reading**: both gifts notify (the
    /// research does not say).
    /// @orig 0x0022e020 StereoTheft_End (unknown)
    void stereoStolen(int player, double human, double car);

    /// The mugging record `SetInterrogateParam` set (set 0-2's override) while its required time is not 0; nothing for
    /// the defaults (docs/research/combat.md#mugging).
    [[nodiscard]] std::optional<combat::MuggingParams> muggingOverride() const;

    /// Player `player` (0 or 1), human `mugger`, ended a mugging of a human no player controls (`success` or not). On
    /// success the victim's `victimMoney` dollars go to the player (item 2) and it is left with none; then the mugger's
    /// `callback` (`HuSetMugCallback`) is called with the mugger and the success (1 or nil), as for every end of a
    /// mugging. **Coney's reading**: the money notifies as a pickup does, and the callback runs when the game ends
    /// rather than at the end of its clip; a victim's interrogation (`+0x5a0`) and pocket items are not modelled.
    /// @orig 0x0022ceb8 Mugging_End (unknown)
    void mugEnded(int player, double mugger, const std::string& callback, int& victimMoney, bool success);

    /// A scene moved object `handle` to `position`, turned by `rotation`: its record keeps the pose. **Coney's
    /// reading**: with no object tasks, the record's pose stands for the object's (the original writes it when the
    /// object is stored).
    void placeObject(double handle, anim::Vec3 position, anim::Quat rotation = {});

  private:
    // Where prompt object `object` is: its spawn record's place (none when removed or in a hand), else the locator's.
    [[nodiscard]] std::optional<anim::Vec3> promptPosition(double object) const;
    // Delivers message 0 from `human` to `object`; whether its handler took the press.
    bool interact(double object, double human);

    script::ScriptSystem& m_scripts;
    GameState& m_state;
    world_objects::SpawnRecords& m_records;
    const world_objects::ObjectTypes& m_types;
    const script::MessageHandlers* m_messages;
    std::set<double> m_inHand; // the objects held, whose records stay
    Locator m_locate;          // the prompt objects that are not spawn records
};

} // namespace coney
