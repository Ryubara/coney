// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/placed_objects_file.h"

#include "core/parse_number.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <system_error>

namespace coney::world_objects {

namespace {

// The fields of a line: name, three position values, four rotation values, the unused -1, zone, flags, tint and
// flag name.
constexpr std::size_t kFields = 13;

// The next line of `text` from `at`, without its end (a `\r` before the `\n` included); `at` moves past it.
std::string_view nextLine(std::string_view text, std::size_t& at) {
    const std::size_t end = text.find('\n', at);
    std::string_view line = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
    at = end == std::string_view::npos ? text.size() : end + 1;
    if (line.ends_with('\r')) {
        line.remove_suffix(1);
    }
    return line;
}

// The line split at blanks and the separators `{`, `}`, `,` and `)`.
std::vector<std::string_view> fieldsOf(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = std::string_view::npos;
    for (std::size_t i = 0; i <= line.size(); ++i) {
        const bool separator = i == line.size() || line[i] == ' ' || line[i] == '\t' || line[i] == '{' ||
                               line[i] == '}' || line[i] == ',' || line[i] == ')';
        if (separator && start != std::string_view::npos) {
            fields.push_back(line.substr(start, i - start));
            start = std::string_view::npos;
        } else if (!separator && start == std::string_view::npos) {
            start = i;
        }
    }
    return fields;
}

// A field as a float; nothing when it does not read whole.
std::optional<float> floatOf(std::string_view field) {
    // Through parseDecimal: Apple's libc++ has no floating-point std::from_chars. A value beyond a float's range
    // fails, as std::from_chars into a float would.
    const std::optional<double> value = parseDecimal(field);
    if (!value || std::fabs(*value) > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::nullopt;
    }
    return static_cast<float>(*value);
}

// A field as an unsigned number in `base` (a negative decimal wraps, as `%d` into an unsigned does); nothing when it
// does not read whole.
std::optional<std::uint32_t> unsignedOf(std::string_view field, int base) {
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(field.data(), field.data() + field.size(), value, base);
    if (error != std::errc{} || end != field.data() + field.size()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

std::expected<std::vector<PlacedObject>, Error> parsePlacedObjects(std::string_view text) {
    std::size_t at = 0;
    const std::vector<std::string_view> first = fieldsOf(nextLine(text, at));
    const std::optional<std::uint32_t> count = first.size() == 1 ? unsignedOf(first.front(), 10) : std::nullopt;
    if (!count) {
        return fail(ErrorCode::Invalid, "the placed objects' count does not read");
    }
    std::vector<PlacedObject> objects;
    objects.reserve(*count);
    for (std::uint32_t n = 0; n < *count; ++n) {
        if (at >= text.size()) {
            return fail(ErrorCode::Invalid, std::format("the placed objects end after {} of {} lines", n, *count));
        }
        const std::vector<std::string_view> fields = fieldsOf(nextLine(text, at));
        if (fields.size() != kFields) {
            return fail(ErrorCode::Invalid,
                        std::format("placed object {} has {} fields, not {}", n, fields.size(), kFields));
        }
        PlacedObject object;
        object.name = std::string(fields[0]);
        bool read = true;
        for (std::size_t c = 0; c < 3; ++c) {
            const std::optional<float> value = floatOf(fields[1 + c]);
            read = read && value.has_value();
            object.position.at(c) = value.value_or(0.0F);
        }
        for (std::size_t c = 0; c < 4; ++c) {
            const std::optional<float> value = floatOf(fields[4 + c]);
            read = read && value.has_value();
            object.rotation.at(c) = value.value_or(0.0F);
        }
        const std::optional<std::uint32_t> zone = unsignedOf(fields[9], 10);
        const std::optional<std::uint32_t> flags = unsignedOf(fields[10], 10);
        const std::optional<std::uint32_t> tint = unsignedOf(fields[11], 16);
        if (!read || !zone || !flags || !tint) {
            return fail(ErrorCode::Invalid, std::format("placed object {} ({}) does not read", n, object.name));
        }
        object.zone = *zone;
        object.flags = *flags;
        object.tint = *tint;
        object.flagName = fields[12] == "nil" ? std::string{} : std::string(fields[12]);
        objects.push_back(std::move(object));
    }
    return objects;
}

std::size_t addPlacedObjects(std::span<const PlacedObject> objects, SpawnRecords& records,
                             const std::function<double()>& nextHandle) {
    std::size_t added = 0;
    for (const PlacedObject& object : objects) {
        if (object.emitter()) {
            continue;
        }
        SpawnRecord record;
        record.handle = nextHandle();
        record.typeName = object.name;
        record.position = object.position;
        record.rotation = object.rotation;
        record.zone = object.zone;
        record.flags = object.flags;
        record.tint = object.tint;
        record.flagName = object.flagName;
        if (records.add(std::move(record)) != nullptr) {
            ++added;
        }
    }
    return added;
}

} // namespace coney::world_objects
