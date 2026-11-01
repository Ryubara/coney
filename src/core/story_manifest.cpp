// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/story_manifest.h"

#include <charconv>
#include <cstddef>
#include <format>
#include <set>
#include <string>
#include <string_view>

#include "core/toml_lite.h"

namespace coney::story {

namespace {

using toml::lineError;
using toml::readString;
using toml::trim;

// The array table each mission is, and the plain table that marks a story mod without missions.
constexpr std::string_view kMissionsTable = "[[story.missions]]";
constexpr std::string_view kStoryTable = "story";

// The string field of `mission` that `key` names; nothing for a key the section does not know.
std::string* stringField(StoryMission& mission, std::string_view key) {
    if (key == "id") {
        return &mission.id;
    }
    if (key == "title") {
        return &mission.title;
    }
    if (key == "level") {
        return &mission.level;
    }
    if (key == "script") {
        return &mission.script;
    }
    if (key == "after") {
        return &mission.after;
    }
    if (key == "replace") {
        return &mission.replace;
    }
    if (key == "before") {
        return &mission.before;
    }
    if (key == "area") {
        return &mission.area;
    }
    if (key == "chapter") {
        return &mission.chapter;
    }
    return nullptr;
}

// Whether what follows a value on its line is only blanks and a comment.
bool onlyComment(std::string_view rest) {
    rest = trim(rest);
    return rest.empty() || rest.front() == '#';
}

// The rules each read mission keeps, checked once the whole section is read; `number` counts missions from 1.
std::expected<void, Error> checkMission(const StoryMission& mission, std::size_t number, std::set<std::string>& ids) {
    if (!validMissionId(mission.id)) {
        return fail(ErrorCode::Invalid,
                    std::format("story mission {}: id \"{}\" is missing or not lower case letters, digits and '-'",
                                number, mission.id));
    }
    const auto failure = [&mission](std::string_view what) {
        return fail(ErrorCode::Invalid, std::format("story mission '{}': {}", mission.id, what));
    };
    if (!ids.insert(mission.id).second) {
        return failure("the id is used twice");
    }
    if (mission.level.empty()) {
        return failure("needs a level");
    }
    if (!mission.after.empty() && !mission.replace.empty()) {
        return failure("names both after and replace; a mission names exactly one");
    }
    if (mission.after.empty() && mission.replace.empty()) {
        return failure("names neither after nor replace; a mission names exactly one");
    }
    if (!mission.chapter.empty() && !mission.after.starts_with("hub:")) {
        return failure("a chapter script needs `after = \"hub:<n>\"`");
    }
    return {};
}

} // namespace

bool validMissionId(std::string_view id) {
    if (id.empty() || id.size() > 64 || id.front() == '-') {
        return false;
    }
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::expected<std::optional<StorySection>, Error> parseStorySection(std::string_view manifestText) {
    std::optional<StorySection> section;
    std::string table;
    std::size_t lineNumber = 0;
    while (!manifestText.empty()) {
        // 1. The next line, trimmed; blanks and comments are skipped.
        const auto end = manifestText.find('\n');
        std::string_view line = trim(manifestText.substr(0, end));
        manifestText = end == std::string_view::npos ? std::string_view{} : manifestText.substr(end + 1);
        ++lineNumber;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        // 2. A table header, `[name]` or `[[name]]`: a mission table starts the next mission.
        if (line.front() == '[') {
            const bool array = line.starts_with("[[");
            const auto close = line.find(array ? "]]" : "]");
            if (close == std::string_view::npos) {
                return lineError(lineNumber, "a table header needs its closing bracket");
            }
            const std::size_t open = array ? 2 : 1;
            const std::string name(trim(line.substr(open, close - open)));
            table = array ? "[[" + name + "]]" : name;
            if (table == "story.missions") {
                return lineError(lineNumber, "story missions are `[[story.missions]]`, one per mission");
            }
            if (table == kMissionsTable || table == kStoryTable) {
                if (!section) {
                    section.emplace();
                }
                if (table == kMissionsTable) {
                    section->missions.emplace_back();
                }
            }
            continue;
        }
        // 3. A key and its value. Only a mission table's keys are read; anything else is left to its own reader, but
        // a line that is no `key = value` at all is refused wherever it is.
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || trim(line.substr(0, equals)).empty()) {
            return lineError(lineNumber, "expected `key = value`");
        }
        if (table != kMissionsTable || !section) {
            continue;
        }
        StoryMission& mission = section->missions.back();
        const std::string_view key = trim(line.substr(0, equals));
        const std::string_view value = trim(line.substr(equals + 1));
        if (key == "label") {
            int label = 0;
            const auto digits = value.substr(0, value.find_first_of(" \t#"));
            const auto [at, error] = std::from_chars(digits.data(), digits.data() + digits.size(), label);
            if (error != std::errc{} || at != digits.data() + digits.size() ||
                !onlyComment(value.substr(digits.size()))) {
                return lineError(lineNumber, "label must be a whole number");
            }
            mission.label = label;
            continue;
        }
        std::string* field = stringField(mission, key);
        if (field == nullptr) {
            continue; // a key a later SDK may add
        }
        if (value.empty() || value.front() != '"') {
            return lineError(lineNumber, std::format("{} must be a quoted string", key));
        }
        const auto read = readString(value);
        if (!read) {
            return lineError(lineNumber, "a string needs its closing quote");
        }
        if (!onlyComment(value.substr(read->second))) {
            return lineError(lineNumber, "only a comment may follow a value");
        }
        *field = read->first;
    }
    // 4. Each mission's own rules, once all its keys are read.
    if (section) {
        std::set<std::string> ids;
        for (std::size_t index = 0; index < section->missions.size(); ++index) {
            if (auto checked = checkMission(section->missions[index], index + 1, ids); !checked) {
                return std::unexpected(checked.error());
            }
        }
    }
    return section;
}

} // namespace coney::story
