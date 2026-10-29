// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the debug tools' world edits; the original has none (see world_split.h). The edit file's parser
// (parseWorldEdits()), apart from the geometry helpers in world_split.cpp so neither file passes the size guideline.
#include "debug/world_split.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <map>
#include <utility>

#include "core/parse_number.h"

namespace coney::debug {

namespace {

// The words of one line, split at spaces and tabs.
std::vector<std::string_view> wordsOf(std::string_view line) {
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') {
            ++i;
        }
        if (i > start) {
            words.push_back(line.substr(start, i - start));
        }
    }
    return words;
}

// The numbers of a comma-separated list; nothing when one does not read or the count is not `count`.
std::optional<std::vector<float>> numbersOf(std::string_view text, std::size_t count) {
    std::vector<float> numbers;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = std::min(text.find(',', start), text.size());
        const std::optional<double> value = parseDecimal(text.substr(start, comma - start));
        if (!value || !std::isfinite(*value)) {
            return std::nullopt;
        }
        numbers.push_back(static_cast<float>(*value));
        start = comma + 1;
    }
    if (numbers.size() != count) {
        return std::nullopt;
    }
    return numbers;
}

// A whole number of `text`.
std::optional<std::uint64_t> wholeOf(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// One statement's `key=value` arguments, by key, after its leading words.
using Arguments = std::map<std::string_view, std::string_view, std::less<>>;

// Reads `words` as `key=value` arguments (a key given again replaces it); the message names a word that is not one.
std::expected<Arguments, std::string> argumentsOf(std::span<const std::string_view> words) {
    Arguments arguments;
    for (const std::string_view word : words) {
        const std::size_t equals = word.find('=');
        if (equals == std::string_view::npos || equals == 0) {
            return std::unexpected(std::format("'{}' is not key=value", word));
        }
        arguments[word.substr(0, equals)] = word.substr(equals + 1);
    }
    return arguments;
}

// Reads a box argument: six numbers, the corners in either order.
std::optional<GameBox> boxOf(std::string_view text) {
    const auto n = numbersOf(text, 6);
    if (!n) {
        return std::nullopt;
    }
    const std::vector<float>& v = *n;
    return GameBox{Vec3{std::min(v[0], v[3]), std::min(v[1], v[4]), std::min(v[2], v[5])},
                   Vec3{std::max(v[0], v[3]), std::max(v[1], v[4]), std::max(v[2], v[5])}};
}

// Reads a point argument: three numbers.
std::optional<Vec3> pointOf(std::string_view text) {
    const auto n = numbersOf(text, 3);
    if (!n) {
        return std::nullopt;
    }
    return Vec3{(*n)[0], (*n)[1], (*n)[2]};
}

// Reads one number.
std::optional<float> numberOf(std::string_view text) {
    const auto n = numbersOf(text, 1);
    return n ? std::optional<float>((*n)[0]) : std::nullopt;
}

// Checks that every argument of `arguments` is one of `known`.
std::expected<void, std::string> onlyKnown(const Arguments& arguments, std::initializer_list<std::string_view> known) {
    for (const auto& [key, value] : arguments) {
        if (std::find(known.begin(), known.end(), key) == known.end()) {
            return std::unexpected(std::format("unknown argument '{}'", key));
        }
    }
    return {};
}

// Applies one statement (its words) to `edits`; the error says what is wrong.
std::expected<void, std::string> applyStatement(std::span<const std::string_view> words, WorldEdits& edits) {
    const std::string_view verb = words[0];
    if (verb == "hide") {
        if (words.size() != 2 || (words[1] != "s" && words[1] != "d")) {
            return std::unexpected("hide takes s or d");
        }
        edits.hidden.at(words[1] == "s" ? 0 : 1) = true;
        return {};
    }
    if (verb == "debugdraw") {
        if (words.size() < 2 || words[1] != "collision") {
            return std::unexpected("debugdraw takes collision");
        }
        auto arguments = argumentsOf(words.subspan(2));
        if (!arguments) {
            return std::unexpected(arguments.error());
        }
        if (auto known = onlyKnown(*arguments, {"radius"}); !known) {
            return known;
        }
        float radius = 8.0F;
        if (const auto it = arguments->find("radius"); it != arguments->end()) {
            const std::optional<float> r = numberOf(it->second);
            if (!r || *r <= 0.0F) {
                return std::unexpected("radius must be a positive number");
            }
            radius = *r;
        }
        edits.collisionWire = radius;
        return {};
    }
    // The statements that name something first: cut NAME, move NAME.
    const bool named = verb == "cut" || verb == "move";
    if (named && words.size() < 2) {
        return std::unexpected(std::format("{} needs a name", verb));
    }
    auto arguments = argumentsOf(words.subspan(named ? 2 : 1));
    if (!arguments) {
        return std::unexpected(arguments.error());
    }
    const auto get = [&arguments](std::string_view key) -> std::optional<std::string_view> {
        const auto it = arguments->find(key);
        return it == arguments->end() ? std::nullopt : std::optional<std::string_view>(it->second);
    };
    if (verb == "spread") {
        if (auto known = onlyKnown(*arguments, {"columns", "gap", "scale", "cell", "at"}); !known) {
            return known;
        }
        WorldEdits::Spread spread;
        if (const auto v = get("columns"); v) {
            const auto columns = wholeOf(*v);
            if (!columns || *columns == 0 || *columns > 1000) {
                return std::unexpected("columns must be 1 to 1000");
            }
            spread.columns = static_cast<std::uint32_t>(*columns);
        }
        if (const auto v = get("gap"); v) {
            const auto gap = numberOf(*v);
            if (!gap || *gap < 0.0F) {
                return std::unexpected("gap must be a number of metres, 0 or more");
            }
            spread.gap = *gap;
        }
        if (const auto v = get("scale"); v) {
            const auto scale = numberOf(*v);
            if (!scale || *scale <= 0.0F) {
                return std::unexpected("scale must be a positive number");
            }
            spread.scale = *scale;
        }
        if (const auto v = get("cell"); v) {
            const auto cell = numberOf(*v);
            if (!cell || *cell <= 0.0F || get("scale")) {
                return std::unexpected("cell must be a positive number of metres, and not given with scale");
            }
            spread.cell = cell;
        }
        if (const auto v = get("at"); v) {
            const auto at = pointOf(*v);
            if (!at) {
                return std::unexpected("at takes X,Y,Z");
            }
            spread.origin = *at;
        }
        edits.spread = spread;
        return {};
    }
    if (verb == "camera") {
        if (auto known = onlyKnown(*arguments, {"at", "heading", "pitch"}); !known) {
            return known;
        }
        WorldEdits::Camera camera;
        const auto at = get("at");
        const std::optional<Vec3> point = at ? pointOf(*at) : std::nullopt;
        if (!point) {
            return std::unexpected("camera needs at=X,Y,Z");
        }
        camera.at = *point;
        if (const auto v = get("heading"); v) {
            const auto heading = numberOf(*v);
            if (!heading) {
                return std::unexpected("heading must be a number of degrees");
            }
            camera.heading = *heading;
        }
        if (const auto v = get("pitch"); v) {
            const auto pitch = numberOf(*v);
            if (!pitch) {
                return std::unexpected("pitch must be a number of degrees");
            }
            camera.pitch = *pitch;
        }
        edits.camera = camera;
        return {};
    }
    if (verb == "cut") {
        if (auto known = onlyKnown(*arguments, {"box", "by", "collision"}); !known) {
            return known;
        }
        WorldEdits::Cut cut;
        cut.name = std::string(words[1]);
        for (const WorldEdits::Cut& other : edits.cuts) {
            if (other.name == cut.name) {
                return std::unexpected(std::format("a cut named '{}' is already made", cut.name));
            }
        }
        const auto box = get("box");
        const std::optional<GameBox> parsed = box ? boxOf(*box) : std::nullopt;
        if (!parsed) {
            return std::unexpected("cut needs box=X0,Y0,Z0,X1,Y1,Z1");
        }
        cut.box = *parsed;
        if (const auto v = get("by"); v) {
            if (*v == "centroid") {
                cut.rule = CutRule::Centroid;
            } else if (*v == "corners") {
                cut.rule = CutRule::AllVertices;
            } else if (*v == "pieces") {
                cut.rule = CutRule::Pieces;
            } else {
                return std::unexpected("by takes centroid, corners or pieces");
            }
        }
        if (const auto v = get("collision"); v) {
            if (*v != "yes" && *v != "no") {
                return std::unexpected("collision takes yes or no");
            }
            cut.collision = *v == "yes";
        }
        edits.cuts.push_back(std::move(cut));
        return {};
    }
    if (verb == "move") {
        if (auto known = onlyKnown(*arguments, {"step", "offset"}); !known) {
            return known;
        }
        const auto found = std::find_if(edits.cuts.begin(), edits.cuts.end(),
                                        [&words](const WorldEdits::Cut& cut) { return cut.name == words[1]; });
        if (found == edits.cuts.end()) {
            return std::unexpected(std::format("no cut named '{}' above", words[1]));
        }
        const auto step = get("step");
        const std::optional<std::uint64_t> at = step ? wholeOf(*step) : std::nullopt;
        const auto offsetText = get("offset");
        const std::optional<Vec3> offset = offsetText ? pointOf(*offsetText) : std::nullopt;
        if (!at || !offset) {
            return std::unexpected("move needs step=N and offset=DX,DY,DZ");
        }
        if (!found->keys.empty() && found->keys.back().step > *at) {
            return std::unexpected("a move's steps must not go back");
        }
        found->keys.push_back(OffsetKey{*at, *offset});
        return {};
    }
    if (verb == "roof") {
        if (auto known = onlyKnown(*arguments, {"box", "material"}); !known) {
            return known;
        }
        WorldEdits::Roof roof;
        const auto box = get("box");
        const std::optional<GameBox> parsed = box ? boxOf(*box) : std::nullopt;
        if (!parsed) {
            return std::unexpected("roof needs box=X0,Y0,Z0,X1,Y1,Z1");
        }
        roof.box = *parsed;
        if (const auto v = get("material"); v) {
            // The game names 191 materials, 0 to 190 (CollisionMaterial_GetName's jump table, 0x0034fc08,
            // docs/research/collision.md#materials).
            const auto material = wholeOf(*v);
            if (!material || *material > 190) {
                return std::unexpected("material must be a MATERIAL_* id, 0 to 190");
            }
            roof.material = static_cast<std::uint8_t>(*material);
        }
        edits.roofs.push_back(roof);
        return {};
    }
    return std::unexpected(std::format("unknown statement '{}'", verb));
}

} // namespace

std::expected<WorldEdits, Error> parseWorldEdits(std::string_view text) {
    WorldEdits edits;
    std::size_t lineNumber = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = std::min(text.find('\n', start), text.size());
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++lineNumber;
        if (const std::size_t hash = line.find('#'); hash != std::string_view::npos) {
            line = line.substr(0, hash);
        }
        const std::vector<std::string_view> words = wordsOf(line);
        if (words.empty()) {
            continue;
        }
        if (auto applied = applyStatement(words, edits); !applied) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("world edits, line {}: {}", lineNumber, applied.error()));
        }
    }
    return edits;
}

} // namespace coney::debug
