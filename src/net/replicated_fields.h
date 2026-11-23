// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <variant>

#include "core/error.h"
#include "net/bit_stream.h"

// The replicated-field tables: for each kind of replicated thing, its fields' names, encodings, bits and ranges,
// declared once so the snapshot encoder, its decoder and the tests read one definition, as Quake 3's netField_t
// tables do. Coney's own; the original has no network play (docs/guides/multiplayer.md#field-tables).

namespace coney::net {

/// How a field is encoded.
/// @coney-only network play; the original has none
enum class FieldEncoding : std::uint8_t {
    Bool,    ///< One bit.
    Integer, ///< A whole number in [min, max], sent as its distance from min in `bits` bits; exact.
    Float,   ///< A number clamped to [min, max] and quantized to `bits` bits (quantize()); ends exact, within half a
             ///< step.
    Angle,   ///< Radians, any value, wrapped onto one turn and quantized to `bits` bits; -pi and pi are the same angle.
    Exact,   ///< A finite float sent as its own 32 bits (`bits` 32), so the receiver has the very value: a script's
             ///< number a HUD shows or compares. An infinity or NaN is sent as 0, and -0 as 0.
};

/// One field of a replicated kind. `min` and `max` are the range of an Integer or Float field (whole numbers for an
/// Integer); a Bool, an Angle and an Exact field leave them 0.
/// @coney-only network play; the original has none
struct FieldSpec {
    std::string_view name;
    FieldEncoding encoding = FieldEncoding::Bool;
    int bits = 1;
    float min = 0.0F;
    float max = 0.0F;
};

/// A field's value: a bool for a Bool field, a whole number for an Integer one, radians or the number for an Angle,
/// Float or Exact one.
/// @coney-only network play; the original has none
using FieldValue = std::variant<bool, std::int32_t, float>;

/// Writes `value` as `field` says. A value of the wrong alternative, or an Integer out of its range, is a programmer
/// error (CONEY_ASSERT), since the writer encodes our own state; a Float out of range is clamped, as quantize() does.
/// @coney-only network play; the original has none
void writeField(BitWriter& writer, const FieldSpec& field, const FieldValue& value);

/// The bits writeField() writes for `value`, as a number below 2^bits: what a snapshot compares to see whether a field
/// changed (a change too small to reach the next step costs nothing) and what it sends. The same programmer errors
/// as writeField().
/// @coney-only network play; the original has none
[[nodiscard]] std::uint32_t fieldWord(const FieldSpec& field, const FieldValue& value);

/// The value a fieldWord() `word` stands for. Fails with Invalid when the word is over the field's bits, an Integer's
/// over its range, or an Exact one's an infinity or NaN, since the word comes from the network.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<FieldValue, Error> fieldValue(const FieldSpec& field, std::uint32_t word);

/// Reads a writeField() value of `field`. Fails with Truncated past the end, and with Invalid when an Integer is over
/// its range (the bits hold more than the range has) or an Exact value is an infinity or NaN, since the bytes come
/// from the network.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<FieldValue, Error> readField(BitReader& reader, const FieldSpec& field);

/// The step between two neighbouring values `field` can carry: what a round trip may be off by is half of it (0 for a
/// Bool, an Integer or an Exact field, which are exact).
/// @coney-only network play; the original has none
[[nodiscard]] float fieldStep(const FieldSpec& field);

/// Checks a table: every field named, the names different, its bits 1 to 32 (1 for a Bool, 32 for an Exact); a Float's
/// range not empty; an Integer's range whole numbers that fit its bits. Invalid with the first field's name otherwise.
/// @coney-only network play; the original has none
[[nodiscard]] std::expected<void, Error> validateFields(std::span<const FieldSpec> fields);

/// Bits one thing takes with every field written, without the changed bits a snapshot adds.
/// @coney-only network play; the original has none
[[nodiscard]] std::size_t fieldsBits(std::span<const FieldSpec> fields);

/// The kinds of replicated thing that carry fields (docs/guides/network-architecture.md#net-ids): a camera is named by
/// its player's slot and a sound, a particle or a message is an event, so none of those has a table. A player's HUD is
/// named by his slot, not by a network id, and each part of the HUD's standing state by its element key
/// (net::HudElement, docs/guides/snapshots.md#hud-standing).
/// @coney-only network play; the original has none
enum class ReplicatedKind : std::uint8_t {
    Human,        ///< A player or an AI human.
    WorldObject,  ///< A door, a prop, glass, a pick-up or a loose object, once changed from the level's data.
    Car,          ///< A car.
    PlayerHud,    ///< A player slot's HUD state (hud::PlayerHudState): what his panel is given each step.
    HudPlayer,    ///< A player slot's standing HUD state (hud::PlayerHudStanding): his panel's flags, prompt, radar.
    HudCounter,   ///< A counter panel (hud::CounterPanel).
    HudBar,       ///< A generic bar (hud::GenericBar).
    HudGauge,     ///< The chase gauge (hud::ChaseGauge).
    HudScoreRow,  ///< A row of the text scoreboard (hud::TextProgressRow).
    HudStopWatch, ///< The stopwatch's display and its time.
    HudSettings,  ///< The HUD's own switches: shown, the prompt's place, the radars' script switch and range.
};

/// Every replicated kind, in order.
inline constexpr std::array<ReplicatedKind, 11> kReplicatedKinds{
    ReplicatedKind::Human,       ReplicatedKind::WorldObject,  ReplicatedKind::Car,        ReplicatedKind::PlayerHud,
    ReplicatedKind::HudPlayer,   ReplicatedKind::HudCounter,   ReplicatedKind::HudBar,     ReplicatedKind::HudGauge,
    ReplicatedKind::HudScoreRow, ReplicatedKind::HudStopWatch, ReplicatedKind::HudSettings};

/// A kind's name, for logs and test names.
/// @coney-only network play; the original has none
[[nodiscard]] std::string_view replicatedKindName(ReplicatedKind kind);

/// A kind's field table, in field order (the order of its field enum below).
/// @coney-only network play; the original has none
[[nodiscard]] std::span<const FieldSpec> fieldTable(ReplicatedKind kind);

/// The animation layers a human carries: the newest, and the older ones it and the layers between fade in over. As
/// many as `anim::AnimTaskStack` shows at once (through overlapping fades), each with its clip, its playhead and, for
/// the locomotion's gait blend, its gait value: with them and the fades a guest can draw every blend the host does.
/// @coney-only network play; the original has none
inline constexpr std::size_t kHumanAnimLayers = 4;

/// A human's fields, indexes into fieldTable(ReplicatedKind::Human).
/// @coney-only network play; the original has none
enum class HumanField : std::uint8_t {
    PositionX,  ///< The feet, metres.
    PositionY,  ///< The feet, metres.
    PositionZ,  ///< The feet, metres (up).
    Heading,    ///< Radians, 0 facing +y.
    Health,     ///< Health points (record `+0x144`, a 16-bit number; docs/research/characters.md).
    HealthMax,  ///< Its maximum (record `+0x146`).
    Clip,       ///< The anim id playing: the newest animation layer.
    Playhead,   ///< How far through the clip, 0 to 1.
    Gait,       ///< A gait blend's value, 0 to 4 (walk, jog, run, sprint); 0 for any other task.
    Clip1,      ///< The anim id of the layer under the newest, which the newest fades in over; 0 with none.
    Playhead1,  ///< How far through layer 1's clip, 0 to 1; 0 with no layer 1.
    Gait1,      ///< Layer 1's gait value, as Gait; 0 with no layer 1.
    Fade1,      ///< How far the newest layer has faded in over layer 1 and the older ones, 0 to 1; 1 with no layer 1.
    Clip2,      ///< The anim id of layer 2, under layer 1, which layer 1 fades in over; 0 with none.
    Playhead2,  ///< How far through layer 2's clip, 0 to 1; 0 with no layer 2.
    Gait2,      ///< Layer 2's gait value, as Gait; 0 with no layer 2.
    Fade2,      ///< How far layer 1 has faded in over layer 2 and the older ones, 0 to 1; 1 with no layer 2.
    Clip3,      ///< The anim id of layer 3, the oldest, under layer 2; 0 with none.
    Playhead3,  ///< How far through layer 3's clip, 0 to 1; 0 with no layer 3.
    Gait3,      ///< Layer 3's gait value, as Gait; 0 with no layer 3.
    Fade3,      ///< How far layer 2 has faded in over layer 3, 0 to 1; 1 with no layer 3.
    Grounded,   ///< On the ground (not jumping or falling).
    KnockedOut, ///< Knocked out or wounded.
    MoveStyle,  ///< In the hidden move style (`HumanAnimator::stealthStyle()`): its gait blends' walk is 633.
    Character,  ///< Which character the human is: a Character List index plus one, 0 for none (character_id.h).
    Count,
};

/// A world object's fields, indexes into fieldTable(ReplicatedKind::WorldObject).
/// @coney-only network play; the original has none
enum class WorldObjectField : std::uint8_t {
    PositionX, ///< Metres.
    PositionY, ///< Metres.
    PositionZ, ///< Metres (up).
    Heading,   ///< Radians.
    Opening,   ///< A door's swing, radians.
    State,     ///< The object's own state number (a pick-up taken, glass cracked, ...).
    Broken,    ///< Broken.
    Hidden,    ///< Hidden (taken away, not drawn).
    Count,
};

/// A car's fields, indexes into fieldTable(ReplicatedKind::Car).
/// @coney-only network play; the original has none
enum class CarField : std::uint8_t {
    PositionX, ///< Metres.
    PositionY, ///< Metres.
    PositionZ, ///< Metres (up).
    Heading,   ///< Radians.
    Speed,     ///< Metres a second along its heading; negative backing up.
    Count,
};

/// A player slot's HUD fields, indexes into fieldTable(ReplicatedKind::PlayerHud): hud::PlayerHudState, what the HUD's
/// step is given for his panel (docs/research/hud.md#the-player-panel).
/// @coney-only network play; the original has none
enum class HudField : std::uint8_t {
    Rage,               ///< The rage meter (human `+0x650`; docs/research/hud.md#the-rage-meter).
    RageMax,            ///< Its maximum (the Warrior class's s16 `+0x00`).
    Raging,             ///< Raging (human `+0xe0` flag `0x80000`).
    AltBanner,          ///< The banner's blue-grey (human state flag `0x200000`; docs/research/hud.md#the-name-banner).
    Score,              ///< The score in the stats object (docs/research/hud.md#score-and-money).
    Money,              ///< Inventory item 2.
    Flash,              ///< The first item counter's count (docs/research/hud.md#item-counters).
    SprayPaint,         ///< The second's.
    Handcuffs,          ///< The third's.
    Keys,               ///< The fourth's.
    SelectShows,        ///< The per-player record's `+0x1b`: SELECT shows the panel.
    KnockedOut,         ///< Knocked out (`Human_IsKnockedOut`), which drops an Armies panel.
    CameraIgnoresStick, ///< His camera ignores the right stick: the fixed-camera icon
                        ///< (docs/research/hud.md#hud-fixed-cam-icon).
    Human,              ///< The human the slot drives: his NetId as one number (netIdNumber()), 0 for none
                        ///< (docs/guides/snapshots.md#hud-state; a client's camera follows him).
    Count,
};

/// A player slot's standing HUD fields, indexes into fieldTable(ReplicatedKind::HudPlayer): hud::PlayerHudStanding,
/// what the HUD holds for his panel, his action prompt and his radar between steps (his prompt's text travels as a
/// HUD text).
/// @coney-only network play; the original has none
enum class HudPlayerField : std::uint8_t {
    Attached,     ///< A player is attached to his panel (`+0x40f8`, `HUD_AttachPlayer`).
    Banner,       ///< The banner's sheet-table record the attach picked.
    Shown,        ///< The panel is shown (`+0x40fc`).
    MayShow,      ///< `HidePlayerHud` / `ShowPlayerHud` (`+0x4108`).
    ForceShow,    ///< `ForceShowPlayerHud` (`+0x4104`).
    FlashFrames,  ///< `FlashRageBar`'s frames (`+0x4114`); 0 for none.
    PromptHidden, ///< The action cycle's start hid the prompt's text, until the text changes.
    CycleOn,      ///< The prompt's icon cycle runs (prompt `+0x45c`).
    CycleFrames,  ///< Updates each icon shows (`+0x448`).
    CycleBlink,   ///< Updates of each blink half (`+0x458`); 0 for none.
    CycleIconA,   ///< The first icon (`+0x440`).
    CycleIconB,   ///< The second icon (`+0x444`).
    RadarOn,      ///< His radar is on (radar `+0x04`).
    RadarTint,    ///< His radar disc's tint (radar `+0x38`): 0 grey, 1 blue.
    Count,
};

/// A counter panel's fields, indexes into fieldTable(ReplicatedKind::HudCounter); its label travels as a HUD text. A
/// panel not taken has no element.
/// @coney-only network play; the original has none
enum class HudCounterField : std::uint8_t {
    Kind,     ///< Its layout (hud::CounterKind).
    BarWidth, ///< A bar panel's width.
    Value,    ///< Field 1.
    Target,   ///< Field 2: a `got / needed` panel's needed.
    Visible,  ///< Shown now.
    Count,
};

/// A generic bar's fields, indexes into fieldTable(ReplicatedKind::HudBar); its label travels as a HUD text. A bar that
/// does not exist has no element.
/// @coney-only network play; the original has none
enum class HudBarField : std::uint8_t {
    Shown,    ///< Kind 3's per-bar show.
    Fill,     ///< 0 to 1.
    Gradient, ///< The fill's colour follows the fill (HudBar `+0x60`).
    Red,      ///< The fill colour's red while the gradient is off (HudBar `+0x28`).
    Green,    ///< Its green.
    Blue,     ///< Its blue.
    Alpha,    ///< Its alpha.
    Width,    ///< GUI units.
    Height,   ///< GUI units.
    Count,
};

/// The chase gauge's fields, indexes into fieldTable(ReplicatedKind::HudGauge). No element while it does not exist.
/// @coney-only network play; the original has none
enum class HudGaugeField : std::uint8_t {
    EndIcon, ///< The icon at the track's right end (a sprite word).
    Track,   ///< The track's sprite word.
    Value,   ///< `+0x340`.
    Min,     ///< `+0x344`.
    Max,     ///< `+0x348`.
    Count,
};

/// A scoreboard row's fields, indexes into fieldTable(ReplicatedKind::HudScoreRow); its label travels as a HUD text.
/// A row not shown has no element.
/// @coney-only network play; the original has none
enum class HudScoreRowField : std::uint8_t {
    Score, ///< `+0x558`.
    Red,   ///< The text colour's red.
    Green, ///< Its green.
    Blue,  ///< Its blue.
    Alpha, ///< Its alpha.
    Count,
};

/// The stopwatch's fields, indexes into fieldTable(ReplicatedKind::HudStopWatch); its label travels as a HUD text. No
/// element while it is not shown.
/// @coney-only network play; the original has none
enum class HudStopWatchField : std::uint8_t {
    Time, ///< The mission stopwatch's time, ms (`W_GetStopWatchTime`), which the HUD draws.
    Count,
};

/// The HUD's own switches, indexes into fieldTable(ReplicatedKind::HudSettings): one element, always there.
/// @coney-only network play; the original has none
enum class HudSettingsField : std::uint8_t {
    Visible,        ///< The HUD is shown (`+0x177a0`: `HideHud`, `RestoreHud`).
    ClubActionText, ///< `HUDEnableClubActionText`: the prompt's text at the clubhouse's place.
    RadarScriptOn,  ///< The radars come back on their own (`+0x177ac`).
    RadarRest,      ///< The radius shown at rest (`HUDRadarSetRange`).
    RadarFast,      ///< The radius shown at full speed.
    RadarZoomScale, ///< `HUDSetRadarZoomScale`.
    Count,
};

} // namespace coney::net
