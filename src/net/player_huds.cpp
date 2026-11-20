// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/player_huds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "core/assert.h"
#include "hud/hud.h"
#include "net/protocol.h"
#include "net/replicated_fields.h"
#include "net/snapshot.h"

namespace coney::net {

namespace {

// Field `f`'s index into a HUD's words and values.
constexpr std::size_t at(HudField f) { return static_cast<std::size_t>(f); }

// `number` as field `f`'s Integer value, held within its range.
FieldValue held(HudField f, int number) {
    const FieldSpec& field = fieldTable(ReplicatedKind::PlayerHud)[at(f)];
    return FieldValue{std::clamp(number, static_cast<int>(field.min), static_cast<int>(field.max))};
}

// Field `f`'s Integer value in `values`.
int integer(const std::array<FieldValue, kHudFieldCount>& values, HudField f) {
    return std::get<std::int32_t>(values.at(at(f)));
}

// Field `f`'s Bool value in `values`.
bool flag(const std::array<FieldValue, kHudFieldCount>& values, HudField f) { return std::get<bool>(values.at(at(f))); }

} // namespace

HudWords playerHudWords(const hud::PlayerHudState& state) {
    const hud::PanelValues& panel = state.panel;
    std::array<FieldValue, kHudFieldCount> values{};
    values.at(at(HudField::Rage)) = held(HudField::Rage, panel.rage);
    values.at(at(HudField::RageMax)) = held(HudField::RageMax, panel.rageMax);
    values.at(at(HudField::Raging)) = panel.raging;
    values.at(at(HudField::AltBanner)) = panel.altBanner;
    values.at(at(HudField::Score)) = held(HudField::Score, panel.score);
    values.at(at(HudField::Money)) = held(HudField::Money, panel.money);
    values.at(at(HudField::Flash)) = held(HudField::Flash, panel.items.at(0));
    values.at(at(HudField::SprayPaint)) = held(HudField::SprayPaint, panel.items.at(1));
    values.at(at(HudField::Handcuffs)) = held(HudField::Handcuffs, panel.items.at(2));
    values.at(at(HudField::Keys)) = held(HudField::Keys, panel.items.at(3));
    values.at(at(HudField::SelectShows)) = panel.selectShows;
    values.at(at(HudField::KnockedOut)) = state.knockedOut;
    values.at(at(HudField::CameraIgnoresStick)) = state.cameraIgnoresStick;
    return hudWords(values);
}

hud::PlayerHudState playerHudState(const HudWords& words) {
    const std::array<FieldValue, kHudFieldCount> values = hudValues(words);
    hud::PlayerHudState state;
    state.panel.rage = integer(values, HudField::Rage);
    state.panel.rageMax = integer(values, HudField::RageMax);
    state.panel.raging = flag(values, HudField::Raging);
    state.panel.altBanner = flag(values, HudField::AltBanner);
    state.panel.score = integer(values, HudField::Score);
    state.panel.money = integer(values, HudField::Money);
    state.panel.items = {integer(values, HudField::Flash), integer(values, HudField::SprayPaint),
                         integer(values, HudField::Handcuffs), integer(values, HudField::Keys)};
    state.panel.selectShows = flag(values, HudField::SelectShows);
    state.panel.promptWakes = false;
    state.knockedOut = flag(values, HudField::KnockedOut);
    state.cameraIgnoresStick = flag(values, HudField::CameraIgnoresStick);
    return state;
}

std::vector<SnapshotHud> snapshotHuds(std::span<const hud::PlayerHudState> slots) {
    CONEY_ASSERT(slots.size() <= kMaxPlayerSlots);
    std::vector<SnapshotHud> huds;
    huds.reserve(slots.size());
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        huds.push_back(SnapshotHud{.slot = static_cast<std::uint8_t>(slot), .words = playerHudWords(slots[slot])});
    }
    return huds;
}

} // namespace coney::net
