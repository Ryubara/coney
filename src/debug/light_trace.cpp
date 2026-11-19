// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/light_trace.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include "core/parse_number.h"

namespace coney::debug {

namespace {

// The trace's names of its lists, in TraceList's order (light_trace.py LIST_NAMES).
constexpr std::array<std::string_view, kTraceLists> kListNames{"a", "b", "c", "objects", "world"};
// A light's fields in a row, after the call's (type, radius, r, g, b, a, x, y, z).
constexpr std::size_t kLightFields = 9;
// The selections file's fields before the first light: caller to count.
constexpr std::size_t kCallFields = 11;
// The two inputs files' headers.
constexpr std::string_view kCallsHeader = "call,tick,caller,glow,a,b,c,objects,world";
constexpr std::string_view kListsHeader = "call,list,slot,type,radius,r,g,b,a,x,y,z";
// The glow is added for a glow byte above this (docs/research/lighting.md#glow).
constexpr std::uint8_t kGlowAbove = 10;
// How far two values of the same light may be apart: a float's rounding of the trace's text, and half a step of a
// colour byte for the glow, whose colour Coney rebuilds from bytes.
constexpr float kValueTolerance = 1e-4F;
constexpr float kColourTolerance = 0.5F / 255.0F;

// The lines of `text`, without their line ends; a last empty line (the file's final newline) is dropped.
std::vector<std::string_view> linesOf(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        lines.push_back(line);
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
    return lines;
}

// The comma-separated fields of `line`.
std::vector<std::string_view> fieldsOf(std::string_view line) {
    std::vector<std::string_view> fields;
    while (true) {
        const std::size_t comma = line.find(',');
        fields.push_back(line.substr(0, comma));
        if (comma == std::string_view::npos) {
            return fields;
        }
        line.remove_prefix(comma + 1);
    }
}

// The failure for line `line` (from 1) of the file `file`.
std::unexpected<Error> bad(std::string_view file, std::size_t line, std::string_view what) {
    return fail(ErrorCode::Invalid, std::format("light trace: {} line {}: {}", file, line, what));
}

// The whole number `text` spells in `base`, or nothing.
std::optional<std::uint32_t> wholeNumber(std::string_view text, int base = 10) {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
    if (text.empty() || error != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// The caller's address as the trace writes it (`0x001746f8`), or nothing.
std::optional<std::uint32_t> address(std::string_view text) {
    if (!text.starts_with("0x")) {
        return std::nullopt;
    }
    return wholeNumber(text.substr(2), 16);
}

// The float `text` spells, or nothing.
std::optional<float> decimal(std::string_view text) {
    const std::optional<double> value = parseDecimal(text);
    if (!value) {
        return std::nullopt;
    }
    return static_cast<float>(*value);
}

// The light in `fields` from `first` on (type, radius, colour, position), or nothing for a field that is no number.
std::optional<TraceLight> lightOf(std::span<const std::string_view> fields, std::size_t first) {
    if (fields.size() < first + kLightFields) {
        return std::nullopt;
    }
    std::array<float, kLightFields - 1> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const std::optional<float> value = decimal(fields[first + 1 + i]);
        if (!value) {
            return std::nullopt;
        }
        values.at(i) = *value;
    }
    const std::optional<std::uint32_t> type = wholeNumber(fields[first]);
    if (!type) {
        return std::nullopt;
    }
    TraceLight light;
    light.type = *type;
    light.radius = values[0];
    light.colour = graphics::LightColour{values[1], values[2], values[3], values[4]};
    light.position = world::Vec3{values[5], values[6], values[7]};
    return light;
}

// The selections file's header (light_trace.py `selection_columns`, the lab's `selectionColumns`).
std::string selectionsHeader() {
    std::string header = "caller,dist_sq,sphere_x,sphere_y,sphere_z,sphere_radius,flags,point_lights,pulse,human,count";
    for (std::size_t slot = 0; slot < graphics::kMaxSelectedLights; ++slot) {
        for (const std::string_view field : {"type", "radius", "r", "g", "b", "a", "x", "y", "z"}) {
            header += std::format(",light{}_{}", slot, field);
        }
    }
    return header;
}

// One row of the selections file: the call's arguments and the lights chosen.
std::expected<TraceCall, Error> selectionRow(std::string_view line, std::size_t number) {
    constexpr std::string_view kFile = "selections";
    const std::vector<std::string_view> f = fieldsOf(line);
    if (f.size() != kCallFields + (graphics::kMaxSelectedLights * kLightFields)) {
        return bad(kFile, number, std::format("{} fields, not the trace's columns", f.size()));
    }
    // The caller, five floats (distSq and the sphere), then five whole numbers (flags to count).
    const std::optional<std::uint32_t> caller = address(f[0]);
    std::array<std::optional<float>, 5> floats{};
    for (std::size_t i = 0; i < floats.size(); ++i) {
        floats.at(i) = decimal(f[1 + i]);
    }
    std::array<std::optional<std::uint32_t>, 5> wholes{};
    for (std::size_t i = 0; i < wholes.size(); ++i) {
        wholes.at(i) = wholeNumber(f[6 + i]);
    }
    const bool numbers = caller && std::ranges::all_of(floats, [](const auto& v) { return v.has_value(); }) &&
                         std::ranges::all_of(wholes, [](const auto& v) { return v.has_value(); });
    if (!numbers) {
        return bad(kFile, number, "a call's field is not a number");
    }
    TraceCall call;
    call.caller = caller.value_or(0);
    call.distSq = floats[0].value_or(0.0F);
    call.sphere =
        graphics::LightSphere{world::Vec3{floats[1].value_or(0.0F), floats[2].value_or(0.0F), floats[3].value_or(0.0F)},
                              floats[4].value_or(0.0F)};
    call.flags = wholes[0].value_or(0);
    call.pointLights = wholes[1].value_or(0) != 0;
    call.pulse = wholes[2].value_or(0) != 0;
    call.human = wholes[3].value_or(0) != 0;
    const std::uint32_t count = wholes[4].value_or(0);
    if (count > graphics::kMaxSelectedLights) {
        return bad(kFile, number, std::format("{} lights chosen, more than {}", count, graphics::kMaxSelectedLights));
    }
    for (std::size_t slot = 0; slot < count; ++slot) {
        const std::optional<TraceLight> light = lightOf(f, kCallFields + (slot * kLightFields));
        if (!light) {
            return bad(kFile, number, std::format("chosen light {} is not a light", slot));
        }
        call.chosen.push_back(*light);
    }
    return call;
}

// Adds the calls file's tick and glow byte to `calls` and returns each call's list lengths.
std::expected<std::vector<std::array<std::size_t, kTraceLists>>, Error> readCalls(std::string_view text,
                                                                                  std::vector<TraceCall>& calls) {
    constexpr std::string_view kFile = "calls";
    const std::vector<std::string_view> lines = linesOf(text);
    if (lines.empty() || lines[0] != kCallsHeader) {
        return bad(kFile, 1, "not the trace's columns");
    }
    if (lines.size() - 1 != calls.size()) {
        return bad(kFile, lines.size(), std::format("{} calls, the selections {}", lines.size() - 1, calls.size()));
    }
    std::vector<std::array<std::size_t, kTraceLists>> lengths(calls.size());
    for (std::size_t i = 0; i < calls.size(); ++i) {
        const std::vector<std::string_view> f = fieldsOf(lines[i + 1]);
        if (f.size() != 4 + kTraceLists) {
            return bad(kFile, i + 2, std::format("{} fields, not the trace's columns", f.size()));
        }
        const std::optional<std::uint32_t> call = wholeNumber(f[0]);
        const std::optional<std::uint32_t> tick = wholeNumber(f[1]);
        const std::optional<std::uint32_t> caller = address(f[2]);
        if (call != i || !tick || caller != calls[i].caller) {
            return bad(kFile, i + 2, "the call, its tick or its caller disagrees with the selections");
        }
        calls[i].tick = tick.value_or(0);
        if (!f[3].empty()) {
            const std::optional<std::uint32_t> glow = wholeNumber(f[3]);
            if (!glow || glow.value_or(0) > 0xff) {
                return bad(kFile, i + 2, "the glow byte is not a byte");
            }
            calls[i].glow = static_cast<std::uint8_t>(glow.value_or(0));
        }
        for (std::size_t list = 0; list < kTraceLists; ++list) {
            const std::optional<std::uint32_t> length = wholeNumber(f[4 + list]);
            if (!length) {
                return bad(kFile, i + 2, "a list's length is not a number");
            }
            lengths[i].at(list) = length.value_or(0);
        }
    }
    return lengths;
}

// Adds the lists file's lights to `calls`, each in its list's slot order, and checks every list's length.
std::expected<void, Error> readLists(std::string_view text, std::vector<TraceCall>& calls,
                                     std::span<const std::array<std::size_t, kTraceLists>> lengths) {
    constexpr std::string_view kFile = "lists";
    const std::vector<std::string_view> lines = linesOf(text);
    if (lines.empty() || lines[0] != kListsHeader) {
        return bad(kFile, 1, "not the trace's columns");
    }
    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::vector<std::string_view> f = fieldsOf(lines[i]);
        if (f.size() != 3 + kLightFields) {
            return bad(kFile, i + 1, std::format("{} fields, not the trace's columns", f.size()));
        }
        const std::optional<std::uint32_t> call = wholeNumber(f[0]);
        const auto named = std::ranges::find(kListNames, f[1]);
        const std::optional<std::uint32_t> slot = wholeNumber(f[2]);
        const std::optional<TraceLight> light = lightOf(f, 3);
        if (!call || call.value_or(0) >= calls.size() || named == kListNames.end() || !slot || !light) {
            return bad(kFile, i + 1, "not a light of a call's list");
        }
        const std::size_t c = call.value_or(0);
        const auto list = static_cast<std::size_t>(std::distance(kListNames.begin(), named));
        std::vector<TraceLight>& lights = calls[c].lists.at(list);
        if (slot.value_or(0) != lights.size() || lights.size() >= lengths[c].at(list)) {
            return bad(kFile, i + 1,
                       std::format("call {} list {} slot {}: out of order or past its length {}", c, f[1],
                                   slot.value_or(0), lengths[c].at(list)));
        }
        lights.push_back(light.value_or(TraceLight{}));
    }
    for (std::size_t c = 0; c < calls.size(); ++c) {
        for (std::size_t list = 0; list < kTraceLists; ++list) {
            if (calls[c].lists.at(list).size() != lengths[c].at(list)) {
                return bad(kFile, lines.size(),
                           std::format("call {} list {}: {} lights, the calls file says {}", c, kListNames.at(list),
                                       calls[c].lists.at(list).size(), lengths[c].at(list)));
            }
        }
    }
    return {};
}

// The whole of the file at `path`.
std::expected<std::string, Error> readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("light trace: cannot open {}", path.string()));
    }
    std::ostringstream text;
    text << file.rdbuf();
    return std::move(text).str();
}

// Coney's light type for RenderWare's `type`: 1 directional, 2 ambient, 0x81 and 0x82 spot, anything else a point.
graphics::LightType lightTypeOf(std::uint32_t type) {
    switch (type) {
    case 1:
        return graphics::LightType::Directional;
    case 2:
        return graphics::LightType::Ambient;
    case 0x81:
    case 0x82:
        return graphics::LightType::Spot;
    default:
        return graphics::LightType::Point;
    }
}

// Whether a and b are within `tolerance`.
bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

// A colour component as the byte a human's glow colour holds: v × 255, rounded.
std::uint8_t colourByte(float v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v * 255.0F), 0L, 255L)); }

// A light's kind as the report names it.
std::string kindName(std::uint32_t type) {
    switch (type) {
    case 1:
        return "directional";
    case 2:
        return "ambient";
    case 0x80:
        return "point";
    case 0x81:
    case 0x82:
        return "spot";
    default:
        return std::format("type {}", type);
    }
}

// `lights` by kind: `[ambient, point]`.
std::string kindsOf(std::span<const TraceLight> lights) {
    std::string text = "[";
    for (std::size_t i = 0; i < lights.size(); ++i) {
        text += (i == 0 ? "" : ", ") + kindName(lights[i].type);
    }
    return text + "]";
}

// The first slot where `a` and `b` differ (the shorter's length when one runs out first).
std::size_t firstDifference(std::span<const TraceLight> a, std::span<const TraceLight> b) {
    std::size_t slot = 0;
    while (slot < a.size() && slot < b.size() && sameLight(a[slot], b[slot])) {
        ++slot;
    }
    return slot;
}

} // namespace

bool TraceCall::glows() const { return human && pointLights && glow.value_or(0) > kGlowAbove; }

std::expected<std::vector<TraceCall>, Error> parseLightTrace(std::string_view selections, std::string_view calls,
                                                             std::string_view lists) {
    // The selections give the calls and the lights chosen; the calls file adds the tick and glow byte and says how
    // long each list is, which the lists file must fill exactly.
    const std::vector<std::string_view> rows = linesOf(selections);
    if (rows.empty() || rows[0] != selectionsHeader()) {
        return bad("selections", 1, "not the trace's columns");
    }
    std::vector<TraceCall> out;
    out.reserve(rows.size() - 1);
    for (std::size_t i = 1; i < rows.size(); ++i) {
        auto call = selectionRow(rows[i], i + 1);
        if (!call) {
            return std::unexpected(std::move(call.error()));
        }
        out.push_back(std::move(*call));
    }
    auto lengths = readCalls(calls, out);
    if (!lengths) {
        return std::unexpected(std::move(lengths.error()));
    }
    if (auto filled = readLists(lists, out, *lengths); !filled) {
        return std::unexpected(std::move(filled.error()));
    }
    return out;
}

std::expected<std::vector<TraceCall>, Error> readLightTrace(const std::filesystem::path& selections) {
    // The inputs files are named after the selections file's stem, beside it (light_trace.py `write_files`).
    const std::string stem = selections.stem().string();
    auto chosen = readText(selections);
    auto calls = readText(selections.parent_path() / (stem + ".calls.csv"));
    auto lists = readText(selections.parent_path() / (stem + ".lists.csv"));
    for (const auto* text : {&chosen, &calls, &lists}) {
        if (!text->has_value()) {
            return std::unexpected(text->error());
        }
    }
    return parseLightTrace(chosen.value_or(""), calls.value_or(""), lists.value_or(""));
}

std::string_view traceCallerName(std::uint32_t address) {
    switch (address) {
    case 0x001746f8:
        return "human";
    case 0x00411aa8:
        return "sector";
    case 0x001803a4:
        return "object";
    case 0x00172ce8:
        return "car";
    case 0x0040d1a8:
        return "background";
    default:
        return "other";
    }
}

const TraceCall* traceCallNear(std::span<const TraceCall> calls, std::uint32_t caller, world::Vec3 centre) {
    const TraceCall* best = nullptr;
    float bestSq = kTraceMatchDistance * kTraceMatchDistance;
    for (const TraceCall& call : calls) {
        if (call.caller != caller) {
            continue;
        }
        const float dx = call.sphere.centre.x - centre.x;
        const float dy = call.sphere.centre.y - centre.y;
        const float dz = call.sphere.centre.z - centre.z;
        const float distSq = (dx * dx) + (dy * dy) + (dz * dz);
        if (distSq <= bestSq && (best == nullptr || distSq < bestSq)) {
            best = &call;
            bestSq = distSq;
        }
    }
    return best;
}

std::string traceLightText(const TraceLight& light) {
    if (light.type < 0x80) {
        return kindName(light.type);
    }
    return std::format("{} ({}, {}, {}) r {}", kindName(light.type), light.position.x, light.position.y,
                       light.position.z, light.radius);
}

std::vector<TraceLight> replayCall(const TraceCall& call) {
    // 1. One record per distinct light of the call's lists, the same light in two lists (B and C share theirs) once.
    graphics::LightManager manager;
    std::vector<std::pair<TraceLight, std::uint16_t>> records;
    const auto recordOf = [&](const TraceLight& light) -> std::optional<std::uint16_t> {
        for (const auto& [known, index] : records) {
            if (sameLight(known, light)) {
                return index;
            }
        }
        graphics::LightDescriptor desc;
        desc.type = lightTypeOf(light.type);
        desc.position = light.position;
        desc.colour = light.colour;
        desc.radius = light.radius;
        desc.lights = graphics::kLightsObjects | graphics::kLightsWorld;
        const graphics::LightHandle handle = manager.addLight(desc);
        if (handle == 0) {
            return std::nullopt; // the pool is full: more distinct lights than a manager holds
        }
        const auto index = static_cast<std::uint16_t>(handle - 1);
        records.emplace_back(light, index);
        return index;
    };
    std::array<std::vector<std::uint16_t>, kTraceLists> lists;
    for (std::size_t list = 0; list < kTraceLists; ++list) {
        for (const TraceLight& light : call.lists.at(list)) {
            const std::optional<std::uint16_t> index = recordOf(light);
            if (!index) {
                return {};
            }
            lists.at(list).push_back(index.value_or(0));
        }
    }

    // 2. The call's lists as the viewport's, and the glow from the original's light after the copied list.
    graphics::ViewportLists viewport;
    viewport.world = lists.at(static_cast<std::size_t>(TraceList::A));
    viewport.objects = lists.at(static_cast<std::size_t>(TraceList::B));
    viewport.pulse = lists.at(static_cast<std::size_t>(TraceList::C));
    viewport.points = lists.at(static_cast<std::size_t>(TraceList::Objects));
    viewport.worldPoints = lists.at(static_cast<std::size_t>(TraceList::World));
    manager.setViewportLists(viewport);
    const bool objects = (call.flags & 1U) != 0;
    std::optional<graphics::GlowRequest> glow;
    const TraceList copied = !objects ? TraceList::A : (call.pulse ? TraceList::C : TraceList::B);
    const std::size_t glowSlot = call.list(copied).size();
    if (call.glows() && glowSlot < call.chosen.size()) {
        const TraceLight& lit = call.chosen[glowSlot];
        glow = graphics::GlowRequest{
            lit.position,
            {colourByte(lit.colour.r), colourByte(lit.colour.g), colourByte(lit.colour.b), colourByte(lit.colour.a)}};
    }

    // 3. Coney's selection, each record turned back into the light it was made from (the glow from its record).
    const graphics::LightSelection selection =
        manager.select(call.distSq, call.sphere, objects, call.pointLights, call.pulse, glow);
    std::vector<TraceLight> chosen;
    for (const std::uint16_t index : selection.lights()) {
        if (index == graphics::LightManager::kGlow) {
            const graphics::LightRecord& record = manager.record(index);
            chosen.push_back(TraceLight{0x80, record.desc.radius, record.current, record.desc.position});
            continue;
        }
        const auto found = std::ranges::find(records, index, &std::pair<TraceLight, std::uint16_t>::second);
        chosen.push_back(found != records.end() ? found->first : TraceLight{});
    }
    return chosen;
}

bool sameLight(const TraceLight& a, const TraceLight& b) {
    return a.type == b.type && near(a.radius, b.radius, kValueTolerance) &&
           near(a.colour.r, b.colour.r, kColourTolerance) && near(a.colour.g, b.colour.g, kColourTolerance) &&
           near(a.colour.b, b.colour.b, kColourTolerance) && near(a.colour.a, b.colour.a, kColourTolerance) &&
           near(a.position.x, b.position.x, kValueTolerance) && near(a.position.y, b.position.y, kValueTolerance) &&
           near(a.position.z, b.position.z, kValueTolerance);
}

LightReplay replayLightTrace(std::span<const TraceCall> calls) {
    LightReplay replay;
    for (std::size_t i = 0; i < calls.size(); ++i) {
        const TraceCall& call = calls[i];
        ++replay.calls;
        replay.glowCalls += call.glows() ? 1 : 0;
        std::vector<TraceLight> coney = replayCall(call);
        if (coney.size() == call.chosen.size() && firstDifference(coney, call.chosen) == call.chosen.size()) {
            ++replay.equal;
            continue;
        }
        replay.differences.push_back(ReplayDifference{i, call.caller, call.tick, call.chosen, std::move(coney)});
    }
    return replay;
}

std::string lightReplayReport(const LightReplay& replay) {
    std::string text = std::format("calls {}, equal {}, different {}, glow calls {}\n", replay.calls, replay.equal,
                                   replay.differences.size(), replay.glowCalls);
    for (const ReplayDifference& d : replay.differences) {
        text += std::format("call {} ({}, tick {}): original {} {}, Coney {} {}; first difference at slot {}\n", d.call,
                            traceCallerName(d.caller), d.tick, d.original.size(), kindsOf(d.original), d.coney.size(),
                            kindsOf(d.coney), firstDifference(d.original, d.coney));
    }
    return text;
}

} // namespace coney::debug
