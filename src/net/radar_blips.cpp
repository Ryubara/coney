// SPDX-License-Identifier: GPL-3.0-or-later
// The radar's blips as the snapshots carry them (docs/guides/snapshots.md#hud-blips): hud::RadarBlip to its field
// table's words and back, the slot each blip keeps and its priority. Coney's own; the original has no network play.
#include "net/radar_blips.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <variant>
#include <vector>

#include "animation/anim_math.h"
#include "core/error.h"
#include "graphics/sprite_batch.h"
#include "hud/radar.h"
#include "net/net_id.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"

namespace coney::net {

namespace {

// Field `f`'s index into a blip's words.
constexpr std::size_t at(HudBlipField f) { return static_cast<std::size_t>(f); }

// The blip table's field `f`.
const FieldSpec& specOf(HudBlipField f) { return fieldTable(ReplicatedKind::HudBlip)[at(f)]; }

// Integer field `f`'s word for `number`, held within its range.
std::uint32_t wholeWord(HudBlipField f, std::int64_t number) {
    const FieldSpec& field = specOf(f);
    const std::int64_t held =
        std::clamp(number, static_cast<std::int64_t>(field.min), static_cast<std::int64_t>(field.max));
    return fieldWord(field, FieldValue{static_cast<std::int32_t>(held)});
}

// Float field `f`'s word for `value` (clamped to its range by the encoding).
std::uint32_t floatWord(HudBlipField f, float value) { return fieldWord(specOf(f), FieldValue{value}); }

// Bool field `f`'s word.
std::uint32_t flagWord(HudBlipField f, bool on) { return fieldWord(specOf(f), FieldValue{on}); }

// The value of field `f` in `words`. A word that came from a decoded snapshot is in its field's range; one that is not
// (a programmer's) reads as the field's zero.
FieldValue valueOf(const BlipWords& words, HudBlipField f) {
    const std::expected<FieldValue, Error> value = fieldValue(specOf(f), words.at(at(f)));
    return value ? *value : FieldValue{false};
}

// Whole number field `f` of `words`.
std::int32_t wholeOf(const BlipWords& words, HudBlipField f) {
    const FieldValue value = valueOf(words, f);
    return std::holds_alternative<std::int32_t>(value) ? std::get<std::int32_t>(value) : 0;
}

// Float field `f` of `words`.
float floatOf(const BlipWords& words, HudBlipField f) {
    const FieldValue value = valueOf(words, f);
    return std::holds_alternative<float>(value) ? std::get<float>(value) : 0.0F;
}

// Bool field `f` of `words`.
bool flagOf(const BlipWords& words, HudBlipField f) {
    const FieldValue value = valueOf(words, f);
    return std::holds_alternative<bool>(value) && std::get<bool>(value);
}

} // namespace

BlipWords blipWords(const hud::RadarBlip& blip, NetId follows) {
    BlipWords words{};
    words.at(at(HudBlipField::Type)) = wholeWord(HudBlipField::Type, blip.type);
    words.at(at(HudBlipField::Icon)) = wholeWord(HudBlipField::Icon, blip.icon);
    words.at(at(HudBlipField::Scale)) = floatWord(HudBlipField::Scale, blip.scale);
    words.at(at(HudBlipField::Red)) = wholeWord(HudBlipField::Red, blip.colour.r);
    words.at(at(HudBlipField::Green)) = wholeWord(HudBlipField::Green, blip.colour.g);
    words.at(at(HudBlipField::Blue)) = wholeWord(HudBlipField::Blue, blip.colour.b);
    words.at(at(HudBlipField::Alpha)) = wholeWord(HudBlipField::Alpha, blip.colour.a);
    words.at(at(HudBlipField::Flashing)) = flagWord(HudBlipField::Flashing, blip.flashing);
    words.at(at(HudBlipField::Blinking)) = flagWord(HudBlipField::Blinking, blip.flashCountdown > 0);
    words.at(at(HudBlipField::IconLock)) = flagWord(HudBlipField::IconLock, blip.iconLock);
    words.at(at(HudBlipField::Marked)) = flagWord(HudBlipField::Marked, blip.marked);
    words.at(at(HudBlipField::Marking)) = flagWord(HudBlipField::Marking, blip.markTimer > 0);
    words.at(at(HudBlipField::DotShown)) = flagWord(HudBlipField::DotShown, blip.dotShown);
    // The place: the human it follows when it has an id for him; else its fixed point or where it was last found; else
    // none, with the coordinates' words the zero place's so one blip has one set of words.
    const std::optional<anim::Vec3> place =
        follows.valid() ? std::nullopt : (blip.fixedPlace ? blip.fixedPlace : blip.place);
    words.at(at(HudBlipField::Follows)) = wholeWord(HudBlipField::Follows, netIdNumber(follows));
    words.at(at(HudBlipField::HasPlace)) = flagWord(HudBlipField::HasPlace, place.has_value());
    const anim::Vec3 found = place.value_or(anim::Vec3{});
    words.at(at(HudBlipField::PlaceX)) = place ? floatWord(HudBlipField::PlaceX, found.x) : blipNoPlaceWord();
    words.at(at(HudBlipField::PlaceY)) = place ? floatWord(HudBlipField::PlaceY, found.y) : blipNoPlaceWord();
    words.at(at(HudBlipField::PlaceZ)) = place ? floatWord(HudBlipField::PlaceZ, found.z) : blipNoPlaceWord();
    return words;
}

hud::RadarBlip radarBlipOf(const BlipWords& words) {
    hud::RadarBlip blip;
    blip.type = wholeOf(words, HudBlipField::Type);
    blip.icon = wholeOf(words, HudBlipField::Icon);
    blip.scale = floatOf(words, HudBlipField::Scale);
    blip.colour = graphics::Rgba{static_cast<std::uint8_t>(wholeOf(words, HudBlipField::Red)),
                                 static_cast<std::uint8_t>(wholeOf(words, HudBlipField::Green)),
                                 static_cast<std::uint8_t>(wholeOf(words, HudBlipField::Blue)),
                                 static_cast<std::uint8_t>(wholeOf(words, HudBlipField::Alpha))};
    blip.flashing = flagOf(words, HudBlipField::Flashing);
    blip.flashCountdown = flagOf(words, HudBlipField::Blinking) ? hud::kRadarObjectiveFlash : 0;
    blip.iconLock = flagOf(words, HudBlipField::IconLock);
    blip.marked = flagOf(words, HudBlipField::Marked);
    blip.markTimer = flagOf(words, HudBlipField::Marking) ? hud::kRadarMarkUpdates : 0;
    blip.dotShown = flagOf(words, HudBlipField::DotShown);
    blip.follows = static_cast<std::uint32_t>(wholeOf(words, HudBlipField::Follows));
    if (flagOf(words, HudBlipField::HasPlace)) {
        blip.fixedPlace = anim::Vec3{floatOf(words, HudBlipField::PlaceX), floatOf(words, HudBlipField::PlaceY),
                                     floatOf(words, HudBlipField::PlaceZ)};
    }
    return blip;
}

std::uint32_t blipPriority(const hud::RadarBlip& blip) {
    // The player steers by a mission objective (type 10) and a secondary one (type 1).
    return blip.type == 10 || blip.type == 1 ? 4 : 1;
}

std::map<double, std::uint8_t> BlipSlots::assign(const std::map<double, hud::RadarBlip>& blips) {
    // A blip gone gives its slot back.
    std::erase_if(m_slots, [this, &blips](const auto& entry) {
        if (blips.contains(entry.first)) {
            return false;
        }
        m_used.reset(entry.second);
        return true;
    });
    // A new blip takes the lowest free slot, in handle order; with none free it has none.
    for (const auto& [handle, blip] : blips) {
        if (m_slots.contains(handle)) {
            continue;
        }
        for (std::size_t slot = 0; slot < kMaxRadarBlips; ++slot) {
            if (!m_used.test(slot)) {
                m_used.set(slot);
                m_slots.emplace(handle, static_cast<std::uint8_t>(slot));
                break;
            }
        }
    }
    return m_slots;
}

void BlipSlots::clear() {
    m_slots.clear();
    m_used.reset();
}

std::vector<SnapshotBlip> snapshotBlips(const std::map<double, hud::RadarBlip>& blips, BlipSlots& slots,
                                        const BlipFollowed& followed) {
    const std::map<double, std::uint8_t> given = slots.assign(blips);
    std::vector<SnapshotBlip> taken;
    taken.reserve(given.size());
    for (const auto& [handle, blip] : blips) {
        const auto slot = given.find(handle);
        if (slot == given.end()) {
            continue;
        }
        const NetId human = followed ? followed(handle) : NetId{};
        taken.push_back(
            SnapshotBlip{.slot = slot->second, .words = blipWords(blip, human), .priority = blipPriority(blip)});
    }
    std::ranges::sort(taken, {}, &SnapshotBlip::slot);
    return taken;
}

std::map<double, hud::RadarBlip> hudBlips(const SnapshotBlips& held) {
    std::map<double, hud::RadarBlip> blips;
    for (const auto& [slot, words] : held) {
        blips.emplace(static_cast<double>(slot), radarBlipOf(words));
    }
    return blips;
}

} // namespace coney::net
