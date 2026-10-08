// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/pad_pipe.h"

#include <charconv>
#include <cmath>
#include <format>
#include <set>
#include <utility>

#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "core/event_log.h"
#include "core/pad.h"
#include "gamemodes/level_pickups.h"
#include "human/human.h"
#include "platform/play_level_mode.h"
#include "world_objects/cars.h"
#include "world_objects/spawn_records.h"

namespace coney::platform {

namespace {

// A number for JSON: finite, short.
std::string number(float value) { return std::isfinite(value) ? std::format("{:.3f}", value) : std::string("null"); }

// A string for JSON: quoted, with quotes, backslashes and control characters escaped.
std::string jsonString(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
            out += c;
        }
    }
    return out + '"';
}

// The humans other than player 1, as observationJson() lists them.
std::string humansJson(const PlayLevelMode& mode) {
    const ai::AiHumans& fighters = mode.fighters();
    const ai::Gang* own = fighters.playerBrain().gang();
    const human::Human* player = &mode.player().human();
    std::string out = "[";
    for (const ai::AiHuman& entry : fighters.humans()) {
        if (entry.removed || !entry.human || entry.human.get() == player) {
            continue;
        }
        const human::Human& human = *entry.human;
        const ai::Brain* brain = fighters.brainOf(human);
        const bool enemy = brain != nullptr && ai::Gangs::enemies(own, brain->gang());
        const anim::Vec3 at = human.position();
        if (out.size() > 1) {
            out += ',';
        }
        out += std::format("[{},{},{},{},{}]", number(at.x), number(at.y), human.alive() ? 1 : 0,
                           human.state() == human::TargetState::Standing ? 1 : 0, enemy ? 1 : 0);
    }
    return out + ']';
}

// The world objects of the chosen types, as observationJson() lists them.
std::string objectsJson(const world_objects::SpawnRecords& records, const LevelPickups* pickups,
                        const std::vector<std::string>& prefixes) {
    std::string out = "[";
    for (const world_objects::SpawnRecord& record : records.all()) {
        bool wanted = false;
        for (const std::string& prefix : prefixes) {
            wanted = wanted || record.typeName.starts_with(prefix);
        }
        const bool held = pickups != nullptr && pickups->inHand(record.handle);
        if (!wanted || record.removed || record.hidden || held) {
            continue;
        }
        if (out.size() > 1) {
            out += ',';
        }
        const int shown = record.shownMessage ? (*record.shownMessage ? 1 : 0) : -1;
        out += std::format("[{},{},{},{}]", jsonString(record.typeName), number(record.position[0]),
                           number(record.position[1]), shown);
    }
    return out + ']';
}

// The car stereos still in or at their cars, as objectsJson() lists objects (without the brackets; `comma` when
// objects came before), when a prefix wants `dyn_carstereo`; then the closing bracket.
std::string stereosJson(const world_objects::Cars& cars, const std::vector<std::string>& prefixes, bool comma) {
    bool wanted = false;
    for (const std::string& prefix : prefixes) {
        wanted = wanted || world_objects::kCarStereoType.starts_with(prefix);
    }
    std::string out;
    for (const world_objects::StereoDraw& stereo :
         wanted ? world_objects::stereoDraws(cars) : std::vector<world_objects::StereoDraw>{}) {
        out += std::format("{}[{},{},{},-1]", comma ? "," : "", jsonString(world_objects::kCarStereoType),
                           number(stereo.position.x), number(stereo.position.y));
        comma = true;
    }
    return out + ']';
}

} // namespace

std::string observationJson(std::uint64_t frame, const PipeView& view, const std::vector<std::string>& objectPrefixes) {
    if (view.mode == nullptr) {
        return std::format("{{\"frame\":{},\"play\":false}}", frame);
    }
    const PlayLevelMode& mode = *view.mode;
    const anim::Vec3 feet = mode.playerFeet();
    const anim::Vec3 eye = mode.cameraEye();
    const anim::Vec3 target = mode.cameraTarget();
    std::string objects = "[]";
    if (view.records != nullptr && !objectPrefixes.empty()) {
        objects = objectsJson(*view.records, view.pickups, objectPrefixes);
    }
    if (view.cars != nullptr) {
        objects.pop_back();
        objects += stereosJson(*view.cars, objectPrefixes, objects.size() > 1);
    }
    return std::format(
        "{{\"frame\":{},\"play\":true,\"player\":[{},{},{},{}],\"camera\":[{},{},{},{}],\"humans\":{},\"objects\":{}}}",
        frame, number(feet.x), number(feet.y), number(feet.z), number(mode.playerHeadingDegrees()), number(eye.x),
        number(eye.y), number(target.x), number(target.y), humansJson(mode), objects);
}

PadPipe::PadPipe(std::istream& in, std::ostream& out, Viewer viewer)
    : m_in(in), m_out(out), m_viewer(std::move(viewer)) {}

bool PadPipe::parsePad(std::string_view fields, PadSample& pad) {
    // Five numbers: the buttons in hexadecimal, then four stick bytes.
    std::array<unsigned, 5> values{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        while (at < fields.size() && fields[at] == ' ') {
            ++at;
        }
        const char* first = fields.data() + at;
        const char* last = fields.data() + fields.size();
        const auto [end, error] = std::from_chars(first, last, values.at(i), i == 0 ? 16 : 10);
        if (error != std::errc{} || (i == 0 ? values.at(i) > 0xFFFF : values.at(i) > 0xFF)) {
            return false;
        }
        at = static_cast<std::size_t>(end - fields.data());
    }
    pad.connected = true;
    pad.buttons = static_cast<std::uint16_t>(values[0]);
    for (std::size_t i = 0; i < 4; ++i) {
        pad.sticks.at(i) = static_cast<std::uint8_t>(values.at(i + 1));
    }
    for (std::size_t i = 0; i < pad::kPressureCount; ++i) {
        pad.pressure.at(i) = (pad.buttons & pad::kPressureButtons.at(i)) != 0 ? 255 : 0;
    }
    return true;
}

PortSamples PadPipe::sample(std::uint64_t frame) {
    PortSamples samples{};
    samples[0].connected = true;
    if (m_ended) {
        return samples;
    }
    // 1. What the frame before left on screen, flushed so the driver sees it before it answers.
    m_out << "@obs " << observationJson(frame, m_viewer(), m_prefixes) << '\n';
    m_out.flush();
    // 2. The driver's lines, up to its pad for this frame.
    std::string line;
    while (std::getline(m_in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string_view text(line);
        if (text.starts_with("pad ")) {
            if (!parsePad(text.substr(4), samples[0])) {
                m_out << "@error bad pad line: " << line << '\n';
            }
            return samples;
        }
        if (text.starts_with("observe")) {
            m_prefixes.clear();
            std::size_t at = 7;
            while (at < text.size()) {
                const std::size_t start = text.find_first_not_of(' ', at);
                if (start == std::string_view::npos) {
                    break;
                }
                const std::size_t end = std::min(text.find(' ', start), text.size());
                m_prefixes.emplace_back(text.substr(start, end - start));
                at = end;
            }
        }
    }
    m_ended = true;
    return samples;
}

void HumanWatch::update(const PlayLevelMode* mode) {
    std::set<const human::Human*> present;
    if (mode != nullptr) {
        const human::Human* player = &mode->player().human();
        for (const ai::AiHuman& entry : mode->fighters().humans()) {
            if (entry.removed || !entry.human || entry.human.get() == player) {
                continue;
            }
            const human::Human* human = entry.human.get();
            present.insert(human);
            const bool out = human->health().depleted();
            const auto [seen, isNew] = m_out.try_emplace(human, out);
            if (isNew) {
                events::emit("human_in", "");
            } else if (out && !seen->second) {
                events::emit("human_out", "");
            }
            seen->second = out;
        }
    }
    // Humans no longer listed: deleted, or their level ended.
    for (auto it = m_out.begin(); it != m_out.end();) {
        if (present.contains(it->first)) {
            ++it;
            continue;
        }
        events::emit("human_gone", "");
        it = m_out.erase(it);
    }
}

} // namespace coney::platform
