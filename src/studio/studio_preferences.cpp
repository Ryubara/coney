// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the editor's per-user preferences; the original has no editor.
#include "studio/studio_preferences.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

#include "studio/project_manifest.h"
#include "studio/toml_lite.h"

namespace coney::studio {

namespace {

// `root` in its normal form with no trailing separator, so two ways of writing a folder compare equal.
std::filesystem::path cleaned(const std::filesystem::path& root) {
    std::filesystem::path normal = root.lexically_normal();
    if (!normal.empty() && !normal.has_filename()) {
        normal = normal.parent_path();
    }
    return normal;
}

// `path` as UTF-8 text with '/' separators: how the file holds it, whatever the platform.
std::string pathText(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// The path the UTF-8 `text` names.
std::filesystem::path pathFrom(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// Whether the list already holds the folder `root` (normal form).
bool holds(const std::vector<RecentProject>& list, const std::filesystem::path& root) {
    return std::ranges::any_of(list, [&](const RecentProject& project) { return cleaned(project.root) == root; });
}

} // namespace

void addRecentProject(StudioPreferences& preferences, RecentProject project) {
    project.root = cleaned(project.root);
    removeRecentProject(preferences, project.root);
    preferences.recent.insert(preferences.recent.begin(), std::move(project));
    if (preferences.recent.size() > kMaxRecentProjects) {
        preferences.recent.resize(kMaxRecentProjects);
    }
}

bool removeRecentProject(StudioPreferences& preferences, const std::filesystem::path& root) {
    const std::filesystem::path wanted = cleaned(root);
    return std::erase_if(preferences.recent,
                         [&](const RecentProject& project) { return cleaned(project.root) == wanted; }) > 0;
}

bool projectExists(const std::filesystem::path& root) {
    std::error_code error;
    return std::filesystem::is_regular_file(root / kManifestFile, error);
}

std::string formatPreferences(const StudioPreferences& preferences) {
    std::string out =
        "# Coney Studio's preferences, per user (docs/guides/studio/editor-experience.md#projects-folder).\n";
    if (preferences.projectsFolder) {
        out += std::format("\n[projects]\nfolder = {}\n", toml::quoted(pathText(*preferences.projectsFolder)));
    }
    for (const RecentProject& project : preferences.recent) {
        out += std::format("\n[[recent]]\nname = {}\npath = {}\n", toml::quoted(project.name),
                           toml::quoted(pathText(project.root)));
    }
    return out;
}

std::expected<StudioPreferences, Error> parsePreferences(std::string_view text) {
    StudioPreferences preferences;
    std::vector<RecentProject> entries; // every [[recent]] table, in file order; one with no path is dropped below
    std::vector<bool> hasPath;
    std::string table;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        // 1. The next line.
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++lineNumber;
        line = toml::trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        // 2. A table header: `[name]`, or `[[name]]` for one more of an array (only `recent` is read).
        if (line.front() == '[') {
            const bool array = line.starts_with("[[");
            const auto close = line.find(array ? "]]" : "]");
            if (close == std::string_view::npos) {
                return toml::lineError(lineNumber, "a table header needs its closing bracket");
            }
            const std::size_t open = array ? 2 : 1;
            const std::string name(toml::trim(line.substr(open, close - open)));
            table = array ? "[[" + name + "]]" : name;
            if (table == "[[recent]]") {
                entries.emplace_back();
                hasPath.push_back(false);
            }
            continue;
        }
        // 3. A key and its value: a string is read, anything else is skipped unread.
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || toml::trim(line.substr(0, equals)).empty()) {
            return toml::lineError(lineNumber, "expected `key = value`");
        }
        const std::string key = table + "." + std::string(toml::trim(line.substr(0, equals)));
        const std::string_view value = toml::trim(line.substr(equals + 1));
        if (value.empty() || value.front() != '"') {
            continue;
        }
        const auto read = toml::readString(value);
        if (!read) {
            return toml::lineError(lineNumber, "a string needs its closing quote");
        }
        if (key == "projects.folder") {
            if (!read->first.empty()) {
                preferences.projectsFolder = pathFrom(read->first);
            }
        } else if (key == "[[recent]].name") {
            entries.back().name = read->first;
        } else if (key == "[[recent]].path" && !read->first.empty()) {
            entries.back().root = cleaned(pathFrom(read->first));
            hasPath.back() = true;
        }
    }
    // 4. The list: the file's order is the list's, a folder written twice keeps its first (newest) entry, and the list
    // stops at the limit.
    for (std::size_t i = 0; i < entries.size() && preferences.recent.size() < kMaxRecentProjects; ++i) {
        if (hasPath[i] && !holds(preferences.recent, entries[i].root)) {
            if (entries[i].name.empty()) {
                entries[i].name = entries[i].root.filename().string();
            }
            preferences.recent.push_back(std::move(entries[i]));
        }
    }
    return preferences;
}

std::expected<StudioPreferences, Error> loadPreferences(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return StudioPreferences{};
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return fail(ErrorCode::Io, std::format("could not read {}", file.string()));
    }
    std::ostringstream text;
    text << in.rdbuf();
    auto parsed = parsePreferences(text.str());
    if (!parsed) {
        return fail(parsed.error().code, std::format("{}: {}", file.string(), parsed.error().message));
    }
    return parsed;
}

std::expected<void, Error> savePreferences(const std::filesystem::path& file, const StudioPreferences& preferences) {
    std::error_code error;
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path(), error);
        if (error) {
            return fail(ErrorCode::Io,
                        std::format("could not make {}: {}", file.parent_path().string(), error.message()));
        }
    }
    // Written beside the file and renamed over it, so a crash leaves the old file or the new one, never half of one.
    std::filesystem::path temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        const std::string text = formatPreferences(preferences);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out.good()) {
            return fail(ErrorCode::Io, std::format("could not write {}", temporary.string()));
        }
    }
    std::filesystem::rename(temporary, file, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return fail(ErrorCode::Io, std::format("could not replace {}", file.string()));
    }
    return {};
}

} // namespace coney::studio
