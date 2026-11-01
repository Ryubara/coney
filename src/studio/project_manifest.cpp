// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/project_manifest.h"

#include <charconv>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "studio/toml_lite.h"

namespace coney::studio {

namespace {

using toml::lineError;
using toml::quoted;
using toml::readString;
using toml::trim;

} // namespace

bool validProjectName(std::string_view name) {
    if (name.empty() || name.size() > 64 || name.front() == '-') {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::expected<ProjectManifest, Error> parseProjectManifest(std::string_view text) {
    ProjectManifest manifest;
    std::string table;
    std::size_t lineNumber = 0;
    while (!text.empty()) {
        // 1. The next line, without its comment-free blanks.
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        // 2. A table header.
        if (line.front() == '[') {
            const auto close = line.find(']');
            if (close == std::string_view::npos) {
                return lineError(lineNumber, "a table header needs its ']'");
            }
            table = std::string(trim(line.substr(1, close - 1)));
            continue;
        }
        // 3. A key and its value: a string or a whole number is read, anything else (an array) is skipped unread.
        const auto equals = line.find('=');
        if (equals == std::string_view::npos || trim(line.substr(0, equals)).empty()) {
            return lineError(lineNumber, "expected `key = value`");
        }
        const std::string key = table + "." + std::string(trim(line.substr(0, equals)));
        const std::string_view value = trim(line.substr(equals + 1));
        if (!value.empty() && value.front() == '"') {
            const auto read = readString(value);
            if (!read) {
                return lineError(lineNumber, "a string needs its closing quote");
            }
            const std::string& string = read->first;
            if (key == "project.name") {
                manifest.name = string;
            } else if (key == "project.title") {
                manifest.title = string;
            } else if (key == "project.version") {
                manifest.version = string;
            } else if (key == "play.level") {
                manifest.playLevel = string;
            } else if (key == "play.spawn") {
                manifest.spawn = string;
            }
        } else if (key == "project.sdk") {
            int sdk = 0;
            const auto digits = value.substr(0, value.find_first_of(" \t#"));
            const auto [at, error] = std::from_chars(digits.data(), digits.data() + digits.size(), sdk);
            if (error != std::errc{} || at != digits.data() + digits.size()) {
                return lineError(lineNumber, "sdk must be a whole number");
            }
            manifest.sdk = sdk;
        }
    }
    if (!validProjectName(manifest.name)) {
        return fail(ErrorCode::Invalid, std::format("[project] name \"{}\" is missing or not lower case letters, "
                                                    "digits and '-'",
                                                    manifest.name));
    }
    return manifest;
}

std::string formatProjectManifest(const ProjectManifest& manifest) {
    std::string out = "# A Coney Studio project (docs/guides/studio/lua-sdk.md#manifest).\n[project]\n";
    out += std::format("name = {}\ntitle = {}\nversion = {}\nsdk = {}\n\n[play]\nlevel = {}\n", quoted(manifest.name),
                       quoted(manifest.title), quoted(manifest.version), manifest.sdk, quoted(manifest.playLevel));
    if (!manifest.spawn.empty()) {
        out += std::format("spawn = {}\n", quoted(manifest.spawn));
    }
    return out;
}

} // namespace coney::studio
