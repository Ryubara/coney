// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/log_record.h"

#include <array>
#include <format>

namespace coney::logging {

namespace {

// The categories' names, in the enum's order.
constexpr std::array<std::string_view, kCategoryCount> kCategoryNames{"player", "ai",    "combat",
                                                                      "script", "world", "engine"};
// The levels' names, in the enum's order.
constexpr std::array<std::string_view, 4> kLevelNames{"error", "warn", "info", "detail"};

// The valid names, for an error message.
std::string validNames() {
    std::string names;
    for (const std::string_view name : kCategoryNames) {
        names += std::format("{}{}", names.empty() ? "" : ", ", name);
    }
    return names + " or all";
}

} // namespace

std::string_view categoryName(Category category) { return kCategoryNames.at(static_cast<std::size_t>(category)); }

std::string_view levelName(Level level) { return kLevelNames.at(static_cast<std::size_t>(level)); }

std::optional<Category> parseCategory(std::string_view name) {
    for (std::size_t i = 0; i < kCategoryNames.size(); ++i) {
        if (kCategoryNames.at(i) == name) {
            return static_cast<Category>(i);
        }
    }
    return std::nullopt;
}

std::expected<std::vector<Category>, Error> parseCategoryList(std::string_view list) {
    if (list == "all") {
        std::vector<Category> all;
        for (std::size_t i = 0; i < kCategoryCount; ++i) {
            all.push_back(static_cast<Category>(i));
        }
        return all;
    }
    if (list.empty()) {
        return fail(ErrorCode::InvalidArgument, std::format("no log category given: name {}", validNames()));
    }
    std::vector<Category> categories;
    while (true) {
        const std::size_t comma = list.find(',');
        const std::string_view name = list.substr(0, comma);
        const std::optional<Category> category = parseCategory(name);
        if (!category) {
            return fail(ErrorCode::InvalidArgument,
                        std::format("`{}` is not a log category: name {}", name, validNames()));
        }
        categories.push_back(*category);
        if (comma == std::string_view::npos) {
            return categories;
        }
        list.remove_prefix(comma + 1);
    }
}

std::string playTime(std::uint64_t step) {
    // Tenths of a second: three steps each.
    const std::uint64_t tenths = step * 10 / kStepsPerSecond;
    return std::format("{:02}:{:02}.{}", tenths / 600, (tenths / 10) % 60, tenths % 10);
}

std::string stamp(std::uint64_t step, Level level, int checkpoint) {
    const std::string cp = checkpoint == kNoCheckpoint ? std::string("-") : std::to_string(checkpoint);
    return std::format("[step {} | {} | {} cp{}]", step, playTime(step), levelName(level), cp);
}

std::string formatLine(const Record& record) {
    return std::format("{} {}: {}", stamp(record.step, record.level, record.checkpoint), categoryName(record.category),
                       record.text);
}

Record withheld(const Record& record) {
    if (record.privacy == Privacy::Public) {
        return record;
    }
    constexpr std::string_view kSeparator = ": ";
    const std::size_t split = record.text.find(kSeparator);
    const std::size_t keep = split == std::string::npos ? 0 : split + kSeparator.size();
    Record out = record;
    out.text =
        std::format("{}({} characters, kept out of log files)", record.text.substr(0, keep), record.text.size() - keep);
    return out;
}

} // namespace coney::logging
