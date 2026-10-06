// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_layout.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <span>
#include <system_error>
#include <utility>

#include "core/parse_number.h"

namespace coney::sandbox {

namespace {

// Limits on values, so a typo cannot ask for a kilometre-high box or a million-sided cylinder.
constexpr float kMaxExtent = 2000.0F;
constexpr std::uint32_t kMaxSegments = 128;
constexpr std::uint32_t kMaxSteps = 100;
constexpr std::uint32_t kMaxRepeat = 1000;
constexpr std::uint32_t kDefaultSegments = 24;

// The failure for line `line`: every parse error names its line.
std::unexpected<Error> lineError(std::size_t line, std::string_view message) {
    return fail(ErrorCode::Invalid, std::format("line {}: {}", line, message));
}

// Splits `text` at runs of spaces and tabs.
std::vector<std::string_view> splitWords(std::string_view text) {
    std::vector<std::string_view> words;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t start = text.find_first_not_of(" \t\r", at);
        if (start == std::string_view::npos) {
            break;
        }
        std::size_t end = text.find_first_of(" \t\r", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        words.push_back(text.substr(start, end - start));
        at = end;
    }
    return words;
}

// `text` without spaces at either end.
std::string_view trim(std::string_view text) {
    const std::size_t start = text.find_first_not_of(" \t\r");
    if (start == std::string_view::npos) {
        return {};
    }
    return text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
}

// One statement's `key=value` arguments, consumed as the statement reads them; whatever is left over at the end is an
// unknown argument. Each accessor fails with the line's error.
// Its implicit members can only throw on a failed allocation, which ends the program either way.
// NOLINTNEXTLINE(bugprone-exception-escape)
class Arguments {
  public:
    // Reads the `key=value` words; a word without `=` or a key given twice is an error.
    static std::expected<Arguments, Error> parse(std::size_t line, std::span<const std::string_view> words) {
        Arguments args(line);
        for (const std::string_view word : words) {
            const std::size_t equals = word.find('=');
            if (equals == std::string_view::npos || equals == 0 || equals + 1 == word.size()) {
                return lineError(line, std::format("\"{}\" is not key=value", word));
            }
            const std::string key(word.substr(0, equals));
            if (!args.m_values.emplace(key, word.substr(equals + 1)).second) {
                return lineError(line, std::format("{} given twice", key));
            }
        }
        return args;
    }

    [[nodiscard]] bool has(std::string_view key) const { return m_values.contains(std::string(key)); }

    // A number in [low, high], or `fallback` when the key is absent; a missing key without a fallback is an error.
    std::expected<float, Error> number(std::string_view key, float low, float high,
                                       std::optional<float> fallback = std::nullopt) {
        auto text = take(key);
        if (!text) {
            if (fallback) {
                return *fallback;
            }
            return lineError(m_line, std::format("{}= is missing", key));
        }
        auto value = parseNumber(*text);
        if (!value || !(*value >= low && *value <= high)) {
            return lineError(m_line,
                             std::format("{}= needs a number from {} to {}, got \"{}\"", key, low, high, *text));
        }
        return *value;
    }

    // A whole number in [low, high] (decimal, or hex after 0x), or `fallback` when absent.
    std::expected<std::uint32_t, Error> whole(std::string_view key, std::uint32_t low, std::uint32_t high,
                                              std::optional<std::uint32_t> fallback = std::nullopt) {
        auto text = take(key);
        if (!text) {
            if (fallback) {
                return *fallback;
            }
            return lineError(m_line, std::format("{}= is missing", key));
        }
        std::string_view digits = *text;
        int base = 10;
        if (digits.starts_with("0x") || digits.starts_with("0X")) {
            digits.remove_prefix(2);
            base = 16;
        }
        std::uint32_t value = 0;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value, base);
        if (digits.empty() || parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || value < low ||
            value > high) {
            return lineError(m_line,
                             std::format("{}= needs a whole number from {} to {}, got \"{}\"", key, low, high, *text));
        }
        return value;
    }

    // Three numbers `a,b,c`, each in [low, high], or `fallback` when absent.
    std::expected<anim::Vec3, Error> vector(std::string_view key, float low, float high,
                                            std::optional<anim::Vec3> fallback = std::nullopt) {
        auto text = take(key);
        if (!text) {
            if (fallback) {
                return *fallback;
            }
            return lineError(m_line, std::format("{}= is missing", key));
        }
        std::array<float, 3> parts{};
        std::string_view rest = *text;
        for (std::size_t i = 0; i < 3; ++i) {
            const std::size_t comma = rest.find(',');
            const bool last = i == 2;
            if (last != (comma == std::string_view::npos)) {
                return lineError(m_line, std::format("{}= needs three numbers x,y,z, got \"{}\"", key, *text));
            }
            auto value = parseNumber(rest.substr(0, comma));
            if (!value || !(*value >= low && *value <= high)) {
                return lineError(
                    m_line, std::format("{}= needs three numbers from {} to {}, got \"{}\"", key, low, high, *text));
            }
            parts.at(i) = *value;
            rest = last ? std::string_view{} : rest.substr(comma + 1);
        }
        return anim::Vec3{parts[0], parts[1], parts[2]};
    }

    // A colour `r,g,b`, each 0 to `high`, or `fallback` when absent.
    std::expected<Colour, Error> colour(std::string_view key, float high, std::optional<Colour> fallback) {
        const std::optional<anim::Vec3> asVector =
            fallback ? std::optional<anim::Vec3>(anim::Vec3{fallback->r, fallback->g, fallback->b}) : std::nullopt;
        auto value = vector(key, 0.0F, high, asVector);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        return Colour{value->x, value->y, value->z};
    }

    // `yes` or `no`, or `fallback` when absent.
    std::expected<bool, Error> flag(std::string_view key, bool fallback) {
        auto text = take(key);
        if (!text) {
            return fallback;
        }
        if (*text == "yes") {
            return true;
        }
        if (*text == "no") {
            return false;
        }
        return lineError(m_line, std::format("{}= needs yes or no, got \"{}\"", key, *text));
    }

    // The raw text of `key`, consumed, or nothing.
    std::optional<std::string_view> take(std::string_view key) {
        const auto found = m_values.find(std::string(key));
        if (found == m_values.end()) {
            return std::nullopt;
        }
        const std::string_view value = found->second;
        m_values.erase(found);
        return value;
    }

    // Fails naming the first argument nothing consumed.
    std::expected<void, Error> finish() const {
        if (!m_values.empty()) {
            return lineError(m_line, std::format("unknown argument {}=", m_values.begin()->first));
        }
        return {};
    }

  private:
    explicit Arguments(std::size_t line) : m_line(line) {}

    // A decimal number such as `-1.25` that fits a float; nothing for anything else (parseDecimal()).
    static std::optional<float> parseNumber(std::string_view text) {
        const std::optional<double> value = parseDecimal(text);
        if (!value || std::abs(*value) > std::numeric_limits<float>::max()) {
            return std::nullopt;
        }
        return static_cast<float>(*value);
    }

    std::size_t m_line;
    std::map<std::string, std::string_view, std::less<>> m_values;
};

// Unwraps a std::expected into `target`, or returns its error from the enclosing function.
#define CONEY_TAKE(target, expression)                                                                                 \
    do {                                                                                                               \
        auto taken_ = (expression);                                                                                    \
        if (!taken_) {                                                                                                 \
            return std::unexpected(std::move(taken_.error()));                                                         \
        }                                                                                                              \
        (target) = *taken_;                                                                                            \
    } while (false)

// The shape a primitive keyword names, or nothing.
std::optional<Shape> shapeFromKeyword(std::string_view word) {
    for (const Shape shape : {Shape::Box, Shape::Ramp, Shape::Stairs, Shape::Cylinder, Shape::Sphere, Shape::Capsule}) {
        if (shapeName(shape) == word) {
            return shape;
        }
    }
    return std::nullopt;
}

// The parser's state across lines: the layout so far and which optional statements have been seen.
class Parser {
  public:
    // Reads every line, then fills in the default spawn and view.
    std::expected<SandboxLayout, Error> run(std::string_view text) {
        std::size_t line = 0;
        while (!text.empty()) {
            ++line;
            const std::size_t newline = text.find('\n');
            std::string_view content = text.substr(0, newline);
            text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1);
            content = content.substr(0, content.find('#'));
            if (auto parsed = statement(line, content); !parsed) {
                return std::unexpected(std::move(parsed.error()));
            }
        }
        addDefaults();
        return std::move(m_layout);
    }

  private:
    // One line, comment already removed.
    std::expected<void, Error> statement(std::size_t line, std::string_view content) {
        const std::vector<std::string_view> words = splitWords(content);
        if (words.empty()) {
            return {};
        }
        const std::string_view keyword = words.front();
        const std::span<const std::string_view> rest = std::span(words).subspan(1);
        if (keyword == "title") {
            return once(line, "title", [&]() -> std::expected<void, Error> {
                m_layout.title = std::string(trim(trim(content).substr(keyword.size())));
                return {};
            });
        }
        if (keyword == "texture") {
            return texture(line, rest);
        }
        if (keyword == "spawn" || keyword == "view" || keyword == "target" || keyword == "fighter") {
            return place(line, keyword, rest);
        }
        if (keyword == "occlusion" || keyword == "shadows") {
            return switchable(line, keyword, rest);
        }
        if (auto shape = shapeFromKeyword(keyword)) {
            return primitive(line, *shape, rest);
        }
        auto args = Arguments::parse(line, rest);
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        Lighting& light = m_layout.lighting;
        if (keyword == "sky") {
            return once(line, "sky", [&]() -> std::expected<void, Error> {
                CONEY_TAKE(light.sky, args->colour("colour", 1.0F, std::nullopt));
                return args->finish();
            });
        }
        if (keyword == "sun") {
            return once(line, "sun", [&]() -> std::expected<void, Error> {
                CONEY_TAKE(light.sunDirection, args->vector("direction", -1.0F, 1.0F, light.sunDirection));
                CONEY_TAKE(light.sun, args->colour("colour", 2.0F, light.sun));
                if (anim::length(light.sunDirection) < 0.01F) {
                    return lineError(line, "the sun's direction= cannot be 0,0,0");
                }
                return args->finish();
            });
        }
        if (keyword == "ambient") {
            return once(line, "ambient", [&]() -> std::expected<void, Error> {
                CONEY_TAKE(light.skyAmbient, args->colour("sky", 2.0F, light.skyAmbient));
                CONEY_TAKE(light.groundAmbient, args->colour("ground", 2.0F, light.groundAmbient));
                return args->finish();
            });
        }
        if (keyword == "fog") {
            return once(line, "fog", [&]() -> std::expected<void, Error> {
                CONEY_TAKE(light.fogStart, args->number("start", 0.0F, kMaxExtent));
                CONEY_TAKE(light.fogEnd, args->number("end", 1.0F, kMaxExtent));
                if (light.fogEnd <= light.fogStart) {
                    return lineError(line, "the fog's end= must be beyond its start=");
                }
                return args->finish();
            });
        }
        if (keyword == "tessellate") {
            return once(line, "tessellate", [&]() -> std::expected<void, Error> {
                CONEY_TAKE(m_layout.tessellation, args->number("edge", 0.1F, 100.0F));
                return args->finish();
            });
        }
        return lineError(line, std::format("unknown statement \"{}\"", keyword));
    }

    // Runs `body` for a statement that may appear only once.
    template <typename Body> std::expected<void, Error> once(std::size_t line, const std::string& keyword, Body body) {
        if (!m_seen.emplace(keyword).second) {
            return lineError(line, std::format("{} given twice", keyword));
        }
        return body();
    }

    // `texture NAME FILE`.
    std::expected<void, Error> texture(std::size_t line, std::span<const std::string_view> words) {
        if (words.size() != 2) {
            return lineError(line, "texture needs a name and a file: texture NAME FILE");
        }
        if (findTexture(words[0])) {
            return lineError(line, std::format("texture {} given twice", words[0]));
        }
        m_layout.textures.push_back(TextureRef{std::string(words[0]), std::string(words[1])});
        return {};
    }

    // `spawn NAME at=X,Y,Z [heading=DEG]`, `target NAME at=X,Y,Z [heading=DEG] [health=N]`,
    // `fighter NAME at=X,Y,Z [heading=DEG]` and `view NAME at=X,Y,Z [yaw=DEG] [pitch=DEG]`.
    std::expected<void, Error> place(std::size_t line, std::string_view keyword,
                                     std::span<const std::string_view> words) {
        if (words.empty() || words.front().find('=') != std::string_view::npos) {
            return lineError(line, std::format("{} needs a name first: {} NAME at=X,Y,Z ...", keyword, keyword));
        }
        const std::string name(words.front());
        auto args = Arguments::parse(line, words.subspan(1));
        if (!args) {
            return std::unexpected(std::move(args.error()));
        }
        anim::Vec3 at;
        CONEY_TAKE(at, args->vector("at", -kMaxExtent, kMaxExtent));
        if (keyword == "spawn") {
            if (std::ranges::any_of(m_layout.spawns, [&name](const SpawnPoint& s) { return s.name == name; })) {
                return lineError(line, std::format("spawn {} given twice", name));
            }
            SpawnPoint spawn{.name = name, .position = at, .headingDegrees = 0.0F};
            CONEY_TAKE(spawn.headingDegrees, args->number("heading", -360.0F, 360.0F, 0.0F));
            m_layout.spawns.push_back(spawn);
        } else if (keyword == "target") {
            if (std::ranges::any_of(m_layout.targets, [&name](const TargetPoint& t) { return t.name == name; })) {
                return lineError(line, std::format("target {} given twice", name));
            }
            if (m_layout.targets.size() >= kMaxTargets) {
                return lineError(line, std::format("at most {} targets", kMaxTargets));
            }
            TargetPoint target{.name = name, .position = at, .headingDegrees = 0.0F, .health = 600};
            CONEY_TAKE(target.headingDegrees, args->number("heading", -360.0F, 360.0F, 0.0F));
            std::uint32_t health = 0;
            CONEY_TAKE(health, args->whole("health", 1, 30000, 600));
            target.health = static_cast<int>(health);
            m_layout.targets.push_back(target);
        } else if (keyword == "fighter") {
            if (std::ranges::any_of(m_layout.fighters, [&name](const FighterPoint& f) { return f.name == name; })) {
                return lineError(line, std::format("fighter {} given twice", name));
            }
            if (m_layout.fighters.size() >= kMaxFighters) {
                return lineError(line, std::format("at most {} fighters", kMaxFighters));
            }
            FighterPoint fighter{.name = name, .position = at, .headingDegrees = 0.0F};
            CONEY_TAKE(fighter.headingDegrees, args->number("heading", -360.0F, 360.0F, 0.0F));
            m_layout.fighters.push_back(fighter);
        } else {
            if (std::ranges::any_of(m_layout.views, [&name](const Viewpoint& v) { return v.name == name; })) {
                return lineError(line, std::format("view {} given twice", name));
            }
            Viewpoint view{.name = name, .position = at, .yawDegrees = 0.0F, .pitchDegrees = 0.0F};
            CONEY_TAKE(view.yawDegrees, args->number("yaw", -360.0F, 360.0F, 0.0F));
            CONEY_TAKE(view.pitchDegrees, args->number("pitch", -85.0F, 85.0F, 0.0F));
            m_layout.views.push_back(view);
        }
        return args->finish();
    }

    // `occlusion off`, `occlusion [radius=M] [strength=S]`, `shadows on|off`.
    std::expected<void, Error> switchable(std::size_t line, std::string_view keyword,
                                          std::span<const std::string_view> words) {
        Lighting& light = m_layout.lighting;
        return once(line, std::string(keyword), [&]() -> std::expected<void, Error> {
            if (keyword == "shadows") {
                if (words.size() != 1 || (words[0] != "on" && words[0] != "off")) {
                    return lineError(line, "shadows needs on or off");
                }
                light.shadows = words[0] == "on";
                return {};
            }
            if (words.size() == 1 && words[0] == "off") {
                light.occlusion = false;
                return {};
            }
            auto args = Arguments::parse(line, words);
            if (!args) {
                return std::unexpected(std::move(args.error()));
            }
            light.occlusion = true;
            CONEY_TAKE(light.occlusionRadius, args->number("radius", 0.1F, 20.0F, light.occlusionRadius));
            CONEY_TAKE(light.occlusionStrength, args->number("strength", 0.0F, 1.0F, light.occlusionStrength));
            return args->finish();
        });
    }

    // A primitive statement: the shape's own arguments, the shared ones, then the copies a `repeat` asks for.
    std::expected<void, Error> primitive(std::size_t line, Shape shape, std::span<const std::string_view> words) {
        auto parsed = Arguments::parse(line, words);
        if (!parsed) {
            return std::unexpected(std::move(parsed.error()));
        }
        Arguments& args = *parsed;
        Primitive p;
        p.shape = shape;
        p.line = line;
        CONEY_TAKE(p.base, args.vector("at", -kMaxExtent, kMaxExtent));
        CONEY_TAKE(p.yawDegrees, args.number("yaw", -360.0F, 360.0F, 0.0F));
        if (auto shaped = shapeArguments(line, args, p); !shaped) {
            return shaped;
        }
        if (auto shared = sharedArguments(line, args, p); !shared) {
            return shared;
        }

        // The copies: `repeat=N` places N, each `step=X,Y,Z` further on.
        std::uint32_t repeat = 1;
        CONEY_TAKE(repeat, args.whole("repeat", 1, kMaxRepeat, 1U));
        anim::Vec3 step;
        if (repeat > 1 || args.has("step")) {
            CONEY_TAKE(step, args.vector("step", -kMaxExtent, kMaxExtent));
        }
        if (auto finished = args.finish(); !finished) {
            return finished;
        }
        if (m_layout.primitives.size() + repeat > kMaxPrimitives) {
            return lineError(line, std::format("a layout holds at most {} primitives", kMaxPrimitives));
        }
        for (std::uint32_t i = 0; i < repeat; ++i) {
            Primitive copy = p;
            copy.base = anim::add(p.base, anim::scale(step, static_cast<float>(i)));
            m_layout.primitives.push_back(copy);
        }
        return {};
    }

    // The arguments that size each shape.
    static std::expected<void, Error> shapeArguments(std::size_t line, Arguments& args, Primitive& p) {
        constexpr float kSmall = 0.01F;
        switch (p.shape) {
        case Shape::Box:
            CONEY_TAKE(p.size, args.vector("size", kSmall, kMaxExtent));
            break;
        case Shape::Ramp: {
            CONEY_TAKE(p.size.x, args.number("width", kSmall, kMaxExtent));
            CONEY_TAKE(p.size.z, args.number("height", kSmall, kMaxExtent));
            if (args.has("angle") == args.has("length")) {
                return lineError(line, "a ramp needs one of angle= (degrees) or length= (metres)");
            }
            if (args.has("angle")) {
                float angle = 0.0F;
                CONEY_TAKE(angle, args.number("angle", 1.0F, 89.0F));
                p.size.y = p.size.z / std::tan(angle * std::numbers::pi_v<float> / 180.0F);
            } else {
                CONEY_TAKE(p.size.y, args.number("length", kSmall, kMaxExtent));
            }
            break;
        }
        case Shape::Stairs: {
            float rise = 0.0F;
            float run = 0.0F;
            CONEY_TAKE(p.size.x, args.number("width", kSmall, kMaxExtent));
            CONEY_TAKE(p.steps, args.whole("steps", 1, kMaxSteps));
            CONEY_TAKE(rise, args.number("rise", kSmall, 10.0F));
            CONEY_TAKE(run, args.number("run", kSmall, 10.0F));
            p.size.y = run * static_cast<float>(p.steps);
            p.size.z = rise * static_cast<float>(p.steps);
            break;
        }
        case Shape::Cylinder:
        case Shape::Capsule:
            CONEY_TAKE(p.radius, args.number("radius", kSmall, kMaxExtent));
            CONEY_TAKE(p.size.z, args.number("height", kSmall, kMaxExtent));
            CONEY_TAKE(p.segments, args.whole("segments", 3, kMaxSegments, kDefaultSegments));
            if (p.shape == Shape::Capsule && p.size.z < 2.0F * p.radius) {
                return lineError(line, "a capsule's height= must be at least twice its radius=");
            }
            break;
        case Shape::Sphere:
            CONEY_TAKE(p.radius, args.number("radius", kSmall, kMaxExtent));
            CONEY_TAKE(p.segments, args.whole("segments", 4, kMaxSegments, kDefaultSegments));
            p.size.z = 2.0F * p.radius;
            break;
        }
        if (p.shape == Shape::Cylinder || p.shape == Shape::Sphere || p.shape == Shape::Capsule) {
            p.size.x = 2.0F * p.radius;
            p.size.y = 2.0F * p.radius;
        }
        return {};
    }

    // The arguments every primitive takes: texture, uv, tint and the collision tags.
    std::expected<void, Error> sharedArguments(std::size_t line, Arguments& args, Primitive& p) const {
        if (const std::optional<std::string_view> name = args.take("texture")) {
            if (*name != "none") {
                p.texture = findTexture(*name);
                if (!p.texture) {
                    return lineError(line, std::format("no texture called {} (declare it first with texture NAME "
                                                       "FILE)",
                                                       *name));
                }
            }
        } else if (!m_layout.textures.empty()) {
            p.texture = 0; // the first texture declared
        }
        CONEY_TAKE(p.uvScale, args.number("uv", 0.01F, 100.0F, 1.0F));
        CONEY_TAKE(p.tint, args.colour("tint", 2.0F, p.tint));
        std::uint32_t value = 0;
        CONEY_TAKE(value, args.whole("flags", 0, 0xFFFF, p.surface.flags));
        p.surface.flags = static_cast<std::uint16_t>(value);
        CONEY_TAKE(value, args.whole("material", 0, 0xFF, p.surface.material));
        p.surface.material = static_cast<std::uint8_t>(value);
        CONEY_TAKE(value, args.whole("area", 0, 0xFF, p.surface.area));
        p.surface.area = static_cast<std::uint8_t>(value);
        CONEY_TAKE(p.surface.solid, args.flag("solid", true));
        return {};
    }

    // The index of the texture called `name`, or nothing.
    [[nodiscard]] std::optional<std::size_t> findTexture(std::string_view name) const {
        for (std::size_t i = 0; i < m_layout.textures.size(); ++i) {
            if (m_layout.textures[i].name == name) {
                return i;
            }
        }
        return std::nullopt;
    }

    // A spawn at the origin and a view behind the first spawn, when the layout gives none.
    void addDefaults() {
        if (m_layout.spawns.empty()) {
            m_layout.spawns.push_back(SpawnPoint{.name = "origin", .position = {}, .headingDegrees = 0.0F});
        }
        if (m_layout.views.empty()) {
            // 6 m behind the spawn and 3 m up, looking where it faces and a little down (Coney's choice).
            const SpawnPoint& spawn = m_layout.spawns.front();
            const float heading = spawn.headingDegrees * std::numbers::pi_v<float> / 180.0F;
            const anim::Vec3 forward{-std::sin(heading), std::cos(heading), 0.0F};
            m_layout.views.push_back(
                Viewpoint{.name = "start",
                          .position = anim::add(anim::add(spawn.position, anim::scale(forward, -6.0F)),
                                                anim::Vec3{0.0F, 0.0F, 3.0F}),
                          .yawDegrees = spawn.headingDegrees,
                          .pitchDegrees = -15.0F});
        }
    }

    SandboxLayout m_layout;
    std::set<std::string, std::less<>> m_seen; // the once-only statements given so far
};

#undef CONEY_TAKE

} // namespace

std::string_view shapeName(Shape shape) {
    switch (shape) {
    case Shape::Box:
        return "box";
    case Shape::Ramp:
        return "ramp";
    case Shape::Stairs:
        return "stairs";
    case Shape::Cylinder:
        return "cylinder";
    case Shape::Sphere:
        return "sphere";
    case Shape::Capsule:
        return "capsule";
    }
    return "?";
}

std::expected<SandboxLayout, Error> parseSandboxLayout(std::string_view text) { return Parser{}.run(text); }

std::expected<SandboxLayout, Error> loadSandboxLayout(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("cannot open the sandbox layout {}", path.string()));
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) {
        return fail(ErrorCode::Io, std::format("cannot read the sandbox layout {}", path.string()));
    }
    auto layout = parseSandboxLayout(text);
    if (!layout) {
        return fail(layout.error().code, std::format("{}: {}", path.string(), layout.error().message));
    }
    return layout;
}

std::vector<std::string> listSandboxLayouts(const std::filesystem::path& folder) {
    std::vector<std::string> names;
    std::error_code error;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        const std::filesystem::path& path = it->path();
        if (path.extension() == kLayoutExtension && it->is_regular_file(error)) {
            names.push_back(path.stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

std::expected<std::filesystem::path, Error> findSandboxLayout(const std::filesystem::path& folder,
                                                              std::string_view nameOrPath) {
    std::error_code error;
    std::filesystem::path given{std::string(nameOrPath)};
    if (given.has_extension() && std::filesystem::is_regular_file(given, error)) {
        return given;
    }
    std::filesystem::path named = folder / (std::string(nameOrPath) + std::string(kLayoutExtension));
    if (std::filesystem::is_regular_file(named, error)) {
        return named;
    }
    std::string known;
    for (const std::string& name : listSandboxLayouts(folder)) {
        known += (known.empty() ? "" : ", ") + name;
    }
    return fail(ErrorCode::NotFound, std::format("no sandbox layout \"{}\" in {} (there are: {})", nameOrPath,
                                                 folder.string(), known.empty() ? "none" : known));
}

} // namespace coney::sandbox
