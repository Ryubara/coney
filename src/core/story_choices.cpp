// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the player's story merge choices in enabled.toml; the original has no mods.
#include "core/story_choices.h"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/toml_lite.h"

namespace coney::story {

namespace {

// The header that starts one choice's table.
constexpr std::string_view kChoiceTable = "[[story.choice]]";

// Whether `line` is a table header (`[name]` or `[[name]]`).
bool isHeader(std::string_view line) { return toml::trim(line).starts_with('['); }

// Whether `line` is the header of a story choice: the table, then nothing or a comment.
bool isChoiceHeader(std::string_view line) {
    std::string_view trimmed = toml::trim(line);
    if (!trimmed.starts_with(kChoiceTable)) {
        return false;
    }
    trimmed = toml::trim(trimmed.substr(kChoiceTable.size()));
    return trimmed.empty() || trimmed.front() == '#';
}

// Whether `line` holds a key and a value (not blank, not a comment, not a header).
bool isKeyLine(std::string_view line) {
    const std::string_view trimmed = toml::trim(line);
    return !trimmed.empty() && trimmed.front() != '#' && trimmed.front() != '[';
}

// The lines of `text`, without their '\n'; a last line with nothing after its '\n' is not a line.
std::vector<std::string_view> linesOf(std::string_view text) {
    std::vector<std::string_view> lines;
    while (!text.empty()) {
        const auto end = text.find('\n');
        lines.push_back(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
    }
    return lines;
}

// An array of quoted strings on one line, `["a", "b"]`; nothing when `value` is anything else.
std::optional<std::vector<std::string>> readArray(std::string_view value) {
    value = toml::trim(value);
    if (!value.starts_with('[')) {
        return std::nullopt;
    }
    value = toml::trim(value.substr(1));
    std::vector<std::string> items;
    while (!value.empty() && value.front() != ']') {
        if (value.front() != '"') {
            return std::nullopt;
        }
        const auto read = toml::readString(value);
        if (!read) {
            return std::nullopt;
        }
        items.push_back(read->first);
        value = toml::trim(value.substr(read->second));
        if (value.starts_with(',')) {
            value = toml::trim(value.substr(1));
        } else if (!value.starts_with(']')) {
            return std::nullopt;
        }
    }
    if (!value.starts_with(']')) {
        return std::nullopt;
    }
    value = toml::trim(value.substr(1));
    if (!value.empty() && value.front() != '#') {
        return std::nullopt;
    }
    return items;
}

// The keys of one `[[story.choice]]` table as they are read, checked when the table ends.
struct ChoiceBuilder {
    std::size_t line = 0; ///< The table header's line, for the messages.
    std::optional<std::string> kind{};
    std::optional<std::string> anchor{};
    std::optional<std::vector<std::string>> missions{};
    std::optional<std::vector<std::string>> packages{};

    // The choice these keys make, or why they do not.
    [[nodiscard]] std::expected<StoryChoice, Error> finish() const {
        StoryChoice made;
        if (!kind || (*kind != "after" && *kind != "replace")) {
            return toml::lineError(line, std::format("story choice: kind must be \"after\" or \"replace\", not \"{}\"",
                                                     kind.value_or("")));
        }
        made.kind = *kind == "after" ? GroupKind::Anchor : GroupKind::Replacement;
        if (!anchor || anchor->empty()) {
            return toml::lineError(line, "story choice: it needs an anchor");
        }
        made.anchor = *anchor;
        if (!missions || missions->size() < 2) {
            return toml::lineError(line, "story choice: it needs at least two missions");
        }
        if (!packages || packages->size() != missions->size()) {
            return toml::lineError(line, "story choice: missions and packages must be arrays of one length");
        }
        for (std::size_t i = 0; i < missions->size(); ++i) {
            made.members.push_back({.mission = (*missions)[i], .packageHash = (*packages)[i]});
        }
        return made;
    }
};

// `choice` as the lines of its table, with a blank line before it.
std::string tableOf(const StoryChoice& choice) {
    std::string missions;
    std::string packages;
    for (const StoryChoiceMember& member : choice.members) {
        missions += (missions.empty() ? "" : ", ") + toml::quoted(member.mission);
        packages += (packages.empty() ? "" : ", ") + toml::quoted(member.packageHash);
    }
    return std::format("\n{}\nkind = {}\nanchor = {}\nmissions = [{}]\npackages = [{}]\n", kChoiceTable,
                       choice.kind == GroupKind::Anchor ? "\"after\"" : "\"replace\"", toml::quoted(choice.anchor),
                       missions, packages);
}

// The text of the file `file`; nothing when it cannot be read.
std::optional<std::string> readFile(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

std::expected<std::vector<StoryChoice>, Error> parseStoryChoices(std::string_view enabledText) {
    std::vector<StoryChoice> choices;
    std::optional<ChoiceBuilder> open; // the choice table being read
    std::size_t lineNumber = 0;
    for (const std::string_view raw : linesOf(enabledText)) {
        ++lineNumber;
        const std::string_view line = toml::trim(raw);
        // 1. A header ends the table being read and may start the next choice.
        if (isHeader(line)) {
            if (open) {
                auto made = open->finish();
                if (!made) {
                    return std::unexpected(made.error());
                }
                choices.push_back(std::move(*made));
                open.reset();
            }
            if (isChoiceHeader(line)) {
                open = ChoiceBuilder{.line = lineNumber};
            }
            continue;
        }
        // 2. Any other line outside a choice belongs to the file's other readers; inside one it is a key and a value.
        if (!open || line.empty() || line.front() == '#') {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || toml::trim(line.substr(0, equals)).empty()) {
            return toml::lineError(lineNumber, "expected `key = value`");
        }
        const std::string_view key = toml::trim(line.substr(0, equals));
        const std::string_view value = toml::trim(line.substr(equals + 1));
        if (key == "kind" || key == "anchor") {
            const auto read = value.starts_with('"') ? toml::readString(value) : std::nullopt;
            if (!read) {
                return toml::lineError(lineNumber, std::format("`{}` needs a quoted string", key));
            }
            (key == "kind" ? open->kind : open->anchor) = read->first;
        } else if (key == "missions" || key == "packages") {
            auto read = readArray(value);
            if (!read) {
                return toml::lineError(lineNumber, std::format("`{}` needs an array of quoted strings", key));
            }
            (key == "missions" ? open->missions : open->packages) = std::move(*read);
        }
    }
    if (open) {
        auto made = open->finish();
        if (!made) {
            return std::unexpected(made.error());
        }
        choices.push_back(std::move(*made));
    }
    return choices;
}

std::string withStoryChoices(std::string_view enabledText, std::span<const StoryChoice> choices) {
    // 1. The file without its choice tables: a table runs from its header to its last key line, and the one blank
    // line that set it apart goes with it (the one after it, else the one before it).
    const std::vector<std::string_view> lines = linesOf(enabledText);
    std::vector<std::string_view> kept;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (!isChoiceHeader(lines[i])) {
            kept.push_back(lines[i]);
            continue;
        }
        std::size_t end = i + 1;
        for (std::size_t next = i + 1; next < lines.size() && !isHeader(lines[next]); ++next) {
            if (isKeyLine(lines[next])) {
                end = next + 1;
            }
        }
        if (end < lines.size() && toml::trim(lines[end]).empty()) {
            ++end;
        } else if (!kept.empty() && toml::trim(kept.back()).empty()) {
            kept.pop_back();
        }
        i = end - 1;
    }
    // 2. What is kept, then each choice's table after a blank line.
    std::string out;
    for (const std::string_view line : kept) {
        out += line;
        out += '\n';
    }
    for (const StoryChoice& choice : choices) {
        const bool blank = out.empty() || out.ends_with("\n\n");
        const std::string table = tableOf(choice);
        out += blank ? std::string_view(table).substr(1) : std::string_view(table);
    }
    return out;
}

std::expected<std::vector<StoryChoice>, Error> loadStoryChoices(const std::filesystem::path& enabledFile) {
    std::error_code error;
    if (!std::filesystem::exists(enabledFile, error)) {
        return std::vector<StoryChoice>{};
    }
    const std::optional<std::string> text = readFile(enabledFile);
    if (!text) {
        return fail(ErrorCode::Io, std::format("could not read {}", enabledFile.string()));
    }
    auto parsed = parseStoryChoices(*text);
    if (!parsed) {
        return fail(parsed.error().code, std::format("{}: {}", enabledFile.string(), parsed.error().message));
    }
    return parsed;
}

std::expected<void, Error> saveStoryChoices(const std::filesystem::path& enabledFile,
                                            std::span<const StoryChoice> choices) {
    // 1. What the file says now: the choices go into it, and everything else stays.
    std::error_code error;
    std::string current;
    if (std::filesystem::exists(enabledFile, error)) {
        const std::optional<std::string> text = readFile(enabledFile);
        if (!text) {
            return fail(ErrorCode::Io, std::format("could not read {}", enabledFile.string()));
        }
        current = *text;
    } else if (choices.empty()) {
        return {}; // no file and nothing to say: nothing to make
    }
    const std::string text = withStoryChoices(current, choices);
    // 2. Written beside the file and renamed over it, so a crash leaves the old file or the new one, never half of one.
    if (enabledFile.has_parent_path()) {
        std::filesystem::create_directories(enabledFile.parent_path(), error);
    }
    std::filesystem::path temporary = enabledFile;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out.good()) {
            return fail(ErrorCode::Io, std::format("could not write {}", temporary.string()));
        }
    }
    std::filesystem::rename(temporary, enabledFile, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return fail(ErrorCode::Io, std::format("could not replace {}", enabledFile.string()));
    }
    return {};
}

} // namespace coney::story
