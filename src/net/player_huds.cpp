// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/player_huds.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "core/assert.h"
#include "graphics/render_device.h"
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

HudWords playerHudWords(const hud::PlayerHudState& state, NetId human) {
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
    values.at(at(HudField::Human)) = FieldValue{static_cast<std::int32_t>(netIdNumber(human))};
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

NetId playerHudHuman(const HudWords& words) {
    // The words a decoded snapshot holds are in their fields' ranges, so the number is one of a field of 22 bits.
    return netIdFromNumber(static_cast<std::uint32_t>(integer(hudValues(words), HudField::Human)));
}

std::vector<SnapshotHud> snapshotHuds(std::span<const hud::PlayerHudState> slots, std::span<const NetId> humans) {
    CONEY_ASSERT(slots.size() <= kMaxPlayerSlots);
    std::vector<SnapshotHud> huds;
    huds.reserve(slots.size());
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        // A slot past the humans given drives none.
        const NetId human = slot < humans.size() ? humans[slot] : NetId{};
        huds.push_back(
            SnapshotHud{.slot = static_cast<std::uint8_t>(slot), .words = playerHudWords(slots[slot], human)});
    }
    return huds;
}

namespace {

// An element's words, set field by field from the values of a kind's table: a number held within its field's range.
class ElementWriter {
  public:
    explicit ElementWriter(ReplicatedKind kind) : m_fields(fieldTable(kind)) {}

    // Bool field `f`.
    template <typename Field> void flag(Field f, bool on) { set(static_cast<std::size_t>(f), FieldValue{on}); }
    // Integer field `f`, `number` held within its range.
    template <typename Field> void number(Field f, std::int64_t number) {
        const auto i = static_cast<std::size_t>(f);
        const FieldSpec& field = m_fields[i];
        const std::int64_t held =
            std::clamp(number, static_cast<std::int64_t>(field.min), static_cast<std::int64_t>(field.max));
        set(i, FieldValue{static_cast<std::int32_t>(held)});
    }
    // Exact field `f`.
    template <typename Field> void exact(Field f, float value) { set(static_cast<std::size_t>(f), FieldValue{value}); }
    // The words set.
    [[nodiscard]] const HudElementWords& words() const { return m_words; }

  private:
    // Field `i`'s word for `value`.
    void set(std::size_t i, const FieldValue& value) { m_words.at(i) = fieldWord(m_fields[i], value); }

    std::span<const FieldSpec> m_fields;
    HudElementWords m_words{};
};

// The values an element's words stand for, field by field. Held words are valid (the decoder refused any other), so
// a word over its field's range is a programmer error.
class ElementReader {
  public:
    ElementReader(ReplicatedKind kind, const HudElementWords& words) : m_fields(fieldTable(kind)), m_words(words) {}

    // Bool field `f`.
    template <typename Field> [[nodiscard]] bool flag(Field f) const {
        return std::get<bool>(value(static_cast<std::size_t>(f)));
    }
    // Integer field `f`.
    template <typename Field> [[nodiscard]] std::int32_t number(Field f) const {
        return std::get<std::int32_t>(value(static_cast<std::size_t>(f)));
    }
    // Exact field `f`.
    template <typename Field> [[nodiscard]] float exact(Field f) const {
        return std::get<float>(value(static_cast<std::size_t>(f)));
    }

  private:
    // Field `i`'s value.
    [[nodiscard]] FieldValue value(std::size_t i) const {
        const auto read = fieldValue(m_fields[i], m_words.at(i));
        CONEY_ASSERT(read.has_value());
        return *read;
    }

    std::span<const FieldSpec> m_fields;
    const HudElementWords& m_words;
};

// A colour's four channels into an element's red, green, blue and alpha fields.
template <typename Field> void writeColour(ElementWriter& out, Field red, graphics::Rgba colour) {
    const auto first = static_cast<std::size_t>(red);
    out.number(static_cast<Field>(first), colour.r);
    out.number(static_cast<Field>(first + 1), colour.g);
    out.number(static_cast<Field>(first + 2), colour.b);
    out.number(static_cast<Field>(first + 3), colour.a);
}

// The colour in an element's red, green, blue and alpha fields.
template <typename Field> graphics::Rgba readColour(const ElementReader& in, Field red) {
    const auto first = static_cast<std::size_t>(red);
    return graphics::Rgba{static_cast<std::uint8_t>(in.number(static_cast<Field>(first))),
                          static_cast<std::uint8_t>(in.number(static_cast<Field>(first + 1))),
                          static_cast<std::uint8_t>(in.number(static_cast<Field>(first + 2))),
                          static_cast<std::uint8_t>(in.number(static_cast<Field>(first + 3)))};
}

// A player's element: his panel's flags, his prompt's hiding and cycle, his radar.
HudElementWords playerWords(const hud::PlayerHudStanding& player) {
    ElementWriter out(ReplicatedKind::HudPlayer);
    out.flag(HudPlayerField::Attached, player.panel.attached);
    out.number(HudPlayerField::Banner, player.panel.banner);
    out.flag(HudPlayerField::Shown, player.panel.shown);
    out.flag(HudPlayerField::MayShow, player.panel.mayShow);
    out.flag(HudPlayerField::ForceShow, player.panel.forceShow);
    out.number(HudPlayerField::FlashFrames, player.panel.flashFrames);
    out.flag(HudPlayerField::PromptHidden, player.promptHidden);
    out.flag(HudPlayerField::CycleOn, player.cycle.on);
    out.number(HudPlayerField::CycleFrames, player.cycle.framesPerIcon);
    out.number(HudPlayerField::CycleBlink, player.cycle.blinkFrames);
    out.number(HudPlayerField::CycleIconA, player.cycle.iconA);
    out.number(HudPlayerField::CycleIconB, player.cycle.iconB);
    out.flag(HudPlayerField::RadarOn, player.radarOn);
    out.number(HudPlayerField::RadarTint, static_cast<int>(player.radarTint));
    return out.words();
}

// The player state a player's element stands for, his prompt's text apart.
hud::PlayerHudStanding playerOf(const HudElementWords& words) {
    const ElementReader in(ReplicatedKind::HudPlayer, words);
    hud::PlayerHudStanding player;
    player.panel = hud::PanelFlags{.attached = in.flag(HudPlayerField::Attached),
                                   .banner = static_cast<std::uint32_t>(in.number(HudPlayerField::Banner)),
                                   .shown = in.flag(HudPlayerField::Shown),
                                   .mayShow = in.flag(HudPlayerField::MayShow),
                                   .forceShow = in.flag(HudPlayerField::ForceShow),
                                   .flashFrames = static_cast<std::uint32_t>(in.number(HudPlayerField::FlashFrames))};
    player.promptHidden = in.flag(HudPlayerField::PromptHidden);
    player.cycle = hud::ActionCycle{.on = in.flag(HudPlayerField::CycleOn),
                                    .framesPerIcon = static_cast<std::uint32_t>(in.number(HudPlayerField::CycleFrames)),
                                    .blinkFrames = static_cast<std::uint32_t>(in.number(HudPlayerField::CycleBlink)),
                                    .iconA = static_cast<std::uint32_t>(in.number(HudPlayerField::CycleIconA)),
                                    .iconB = static_cast<std::uint32_t>(in.number(HudPlayerField::CycleIconB))};
    player.radarOn = in.flag(HudPlayerField::RadarOn);
    player.radarTint = static_cast<hud::RadarTint>(in.number(HudPlayerField::RadarTint));
    return player;
}

// The element key `base` + `index`, as an element key.
std::uint8_t keyAt(std::uint8_t base, std::size_t index) { return static_cast<std::uint8_t>(base + index); }

} // namespace

SnapshotHudStanding snapshotHudStanding(const hud::HudStanding& standing) {
    SnapshotHudStanding parts;
    // An element, then its text when it has one not empty, cut to the longest a snapshot carries: keys rise, so each
    // list is in key order.
    const auto add = [&parts](std::uint8_t key, const HudElementWords& words) {
        parts.elements.push_back(SnapshotHudElement{.key = key, .words = words});
    };
    const auto addText = [&parts](std::uint8_t key, const std::string& text) {
        if (!text.empty()) {
            parts.texts.push_back(SnapshotHudText{.key = key, .text = text.substr(0, kMaxHudTextBytes)});
        }
    };
    for (std::size_t player = 0; player < hud::kPlayers; ++player) {
        add(keyAt(kHudElementPlayers, player), playerWords(standing.players.at(player)));
        addText(keyAt(kHudElementPlayers, player), standing.players.at(player).prompt);
    }
    const hud::SharedHudStanding& shared = standing.shared;
    for (std::size_t index = 0; index < shared.counters.size(); ++index) {
        const hud::CounterPanel& panel = shared.counters.at(index);
        if (!panel.used) {
            continue;
        }
        ElementWriter out(ReplicatedKind::HudCounter);
        out.number(HudCounterField::Kind, static_cast<int>(panel.kind));
        out.exact(HudCounterField::BarWidth, panel.barWidth);
        out.exact(HudCounterField::Value, panel.value);
        out.exact(HudCounterField::Target, panel.target);
        out.flag(HudCounterField::Visible, panel.visible);
        add(keyAt(kHudElementCounters, index), out.words());
        addText(keyAt(kHudElementCounters, index), panel.label);
    }
    for (std::size_t slot = 0; slot < shared.bars.size(); ++slot) {
        const hud::GenericBar& bar = shared.bars.at(slot);
        if (!bar.exists) {
            continue;
        }
        ElementWriter out(ReplicatedKind::HudBar);
        out.flag(HudBarField::Shown, bar.shown);
        out.exact(HudBarField::Fill, bar.fill);
        out.flag(HudBarField::Gradient, bar.gradient);
        writeColour(out, HudBarField::Red, bar.colour);
        out.exact(HudBarField::Width, bar.width);
        out.exact(HudBarField::Height, bar.height);
        add(keyAt(kHudElementBars, slot), out.words());
        addText(keyAt(kHudElementBars, slot), bar.label);
    }
    if (shared.gauge.exists) {
        ElementWriter out(ReplicatedKind::HudGauge);
        out.number(HudGaugeField::EndIcon, shared.gauge.endIcon);
        out.number(HudGaugeField::Track, shared.gauge.track);
        out.exact(HudGaugeField::Value, shared.gauge.value);
        out.exact(HudGaugeField::Min, shared.gauge.min);
        out.exact(HudGaugeField::Max, shared.gauge.max);
        add(kHudElementGauge, out.words());
    }
    for (std::size_t row = 0; row < shared.scores.size(); ++row) {
        const hud::TextProgressRow& score = shared.scores.at(row);
        if (!score.active) {
            continue;
        }
        ElementWriter out(ReplicatedKind::HudScoreRow);
        out.number(HudScoreRowField::Score, score.score);
        writeColour(out, HudScoreRowField::Red, score.colour);
        add(keyAt(kHudElementScoreRows, row), out.words());
        addText(keyAt(kHudElementScoreRows, row), score.label);
    }
    if (shared.stopWatch.shown) {
        ElementWriter out(ReplicatedKind::HudStopWatch);
        out.number(HudStopWatchField::Time, shared.stopWatchMs);
        add(kHudElementStopWatch, out.words());
        addText(kHudElementStopWatch, shared.stopWatch.label);
    }
    ElementWriter settings(ReplicatedKind::HudSettings);
    settings.flag(HudSettingsField::Visible, shared.visible);
    settings.flag(HudSettingsField::ClubActionText, shared.clubActionText);
    settings.flag(HudSettingsField::RadarScriptOn, shared.radarScriptOn);
    settings.exact(HudSettingsField::RadarRest, shared.radarRest);
    settings.exact(HudSettingsField::RadarFast, shared.radarFast);
    settings.exact(HudSettingsField::RadarZoomScale, shared.radarZoomScale);
    add(kHudElementSettings, settings.words());
    return parts;
}

hud::HudStanding hudStanding(const SnapshotHudElements& elements, const SnapshotHudTexts& texts) {
    hud::HudStanding standing;
    // Element `key`'s words, or null when it is not held; text `key`, or empty.
    const auto element = [&elements](std::uint8_t key) -> const HudElementWords* {
        const auto found = elements.find(key);
        return found == elements.end() ? nullptr : &found->second;
    };
    const auto text = [&texts](std::uint8_t key) {
        const auto found = texts.find(key);
        return found == texts.end() ? std::string() : found->second;
    };
    for (std::size_t player = 0; player < hud::kPlayers; ++player) {
        const std::uint8_t key = keyAt(kHudElementPlayers, player);
        hud::PlayerHudStanding& mine = standing.players.at(player);
        if (const HudElementWords* words = element(key)) {
            mine = playerOf(*words);
        }
        mine.prompt = text(key);
    }
    hud::SharedHudStanding& shared = standing.shared;
    for (std::size_t index = 0; index < shared.counters.size(); ++index) {
        const std::uint8_t key = keyAt(kHudElementCounters, index);
        if (const HudElementWords* words = element(key)) {
            const ElementReader in(ReplicatedKind::HudCounter, *words);
            shared.counters.at(index) =
                hud::CounterPanel{.used = true,
                                  .kind = static_cast<hud::CounterKind>(in.number(HudCounterField::Kind)),
                                  .label = text(key),
                                  .barWidth = in.exact(HudCounterField::BarWidth),
                                  .value = in.exact(HudCounterField::Value),
                                  .target = in.exact(HudCounterField::Target),
                                  .visible = in.flag(HudCounterField::Visible),
                                  .hideAtMs = {}};
        }
    }
    for (std::size_t slot = 0; slot < shared.bars.size(); ++slot) {
        const std::uint8_t key = keyAt(kHudElementBars, slot);
        if (const HudElementWords* words = element(key)) {
            const ElementReader in(ReplicatedKind::HudBar, *words);
            shared.bars.at(slot) = hud::GenericBar{.exists = true,
                                                   .shown = in.flag(HudBarField::Shown),
                                                   .label = text(key),
                                                   .fill = in.exact(HudBarField::Fill),
                                                   .gradient = in.flag(HudBarField::Gradient),
                                                   .colour = readColour(in, HudBarField::Red),
                                                   .width = in.exact(HudBarField::Width),
                                                   .height = in.exact(HudBarField::Height)};
        }
    }
    if (const HudElementWords* words = element(kHudElementGauge)) {
        const ElementReader in(ReplicatedKind::HudGauge, *words);
        shared.gauge = hud::ChaseGauge{.exists = true,
                                       .endIcon = static_cast<std::uint32_t>(in.number(HudGaugeField::EndIcon)),
                                       .track = static_cast<std::uint32_t>(in.number(HudGaugeField::Track)),
                                       .value = in.exact(HudGaugeField::Value),
                                       .min = in.exact(HudGaugeField::Min),
                                       .max = in.exact(HudGaugeField::Max)};
    }
    for (std::size_t row = 0; row < shared.scores.size(); ++row) {
        const std::uint8_t key = keyAt(kHudElementScoreRows, row);
        if (const HudElementWords* words = element(key)) {
            const ElementReader in(ReplicatedKind::HudScoreRow, *words);
            shared.scores.at(row) =
                hud::TextProgressRow{.active = true,
                                     .label = text(key),
                                     .score = static_cast<std::uint32_t>(in.number(HudScoreRowField::Score)),
                                     .colour = readColour(in, HudScoreRowField::Red)};
        }
    }
    if (const HudElementWords* words = element(kHudElementStopWatch)) {
        const ElementReader in(ReplicatedKind::HudStopWatch, *words);
        shared.stopWatch = hud::StopWatchDisplay{.shown = true, .label = text(kHudElementStopWatch)};
        shared.stopWatchMs = in.number(HudStopWatchField::Time);
    }
    if (const HudElementWords* words = element(kHudElementSettings)) {
        const ElementReader in(ReplicatedKind::HudSettings, *words);
        shared.visible = in.flag(HudSettingsField::Visible);
        shared.clubActionText = in.flag(HudSettingsField::ClubActionText);
        shared.radarScriptOn = in.flag(HudSettingsField::RadarScriptOn);
        shared.radarRest = in.exact(HudSettingsField::RadarRest);
        shared.radarFast = in.exact(HudSettingsField::RadarFast);
        shared.radarZoomScale = in.exact(HudSettingsField::RadarZoomScale);
    }
    return standing;
}

// @coney-only network play; the original has none
hud::HudStanding standingForSlot(const hud::HudStanding& host, std::size_t slot) {
    hud::HudStanding own;
    own.shared = host.shared;
    if (slot < host.players.size()) {
        own.players.at(0) = host.players.at(slot);
    }
    return own;
}

// @coney-only network play; the original has none
hud::PlayerHudState playerHudForSlot(const SnapshotHuds& huds, std::size_t slot) {
    // A slot is a byte on the wire; no player has one past 255.
    if (slot > std::numeric_limits<std::uint8_t>::max()) {
        return {};
    }
    const auto found = huds.find(static_cast<std::uint8_t>(slot));
    return found != huds.end() ? playerHudState(found->second) : hud::PlayerHudState{};
}

} // namespace coney::net
