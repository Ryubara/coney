// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher/release_manifest.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <set>
#include <system_error>
#include <tuple>
#include <utility>

#include "core/json.h"

namespace coney::launcher {

namespace {

// The only manifest layout this launcher reads.
constexpr std::int64_t kFormat = 1;

// A whole number without a sign or a leading zero, as Semantic Versioning writes each part.
std::optional<std::uint32_t> versionPart(std::string_view text) {
    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// Whether every character is a digit.
bool allDigits(std::string_view text) {
    return !text.empty() && text.find_first_not_of("0123456789") == std::string_view::npos;
}

// Compares two pre-release texts part by part: numbers as numbers and below words, words by their bytes, and a
// shorter list first when all its parts are equal.
std::strong_ordering comparePrerelease(std::string_view a, std::string_view b) {
    while (true) {
        const auto aDot = a.find('.');
        const auto bDot = b.find('.');
        const std::string_view aPart = a.substr(0, aDot);
        const std::string_view bPart = b.substr(0, bDot);
        const bool aNumber = allDigits(aPart);
        const bool bNumber = allDigits(bPart);
        std::strong_ordering order = std::strong_ordering::equal;
        if (aNumber && bNumber) {
            order = aPart.size() != bPart.size() ? aPart.size() <=> bPart.size() : aPart.compare(bPart) <=> 0;
        } else if (aNumber != bNumber) {
            order = aNumber ? std::strong_ordering::less : std::strong_ordering::greater;
        } else {
            order = aPart.compare(bPart) <=> 0;
        }
        if (order != 0) {
            return order;
        }
        if (aDot == std::string_view::npos || bDot == std::string_view::npos) {
            return (aDot == std::string_view::npos) == (bDot == std::string_view::npos)
                       ? std::strong_ordering::equal
                       : (aDot == std::string_view::npos ? std::strong_ordering::less : std::strong_ordering::greater);
        }
        a.remove_prefix(aDot + 1);
        b.remove_prefix(bDot + 1);
    }
}

// An error naming the manifest field.
std::unexpected<Error> invalid(std::string_view field, std::string_view what) {
    return fail(ErrorCode::Invalid, std::format("launcher.json: {} {}", field, what));
}

// The string member `key` of `object`, or nullptr.
const std::string* stringField(const json::Value& object, std::string_view key) {
    const json::Value* value = object.find(key);
    return value != nullptr ? value->asString() : nullptr;
}

// Whether every byte is printable ASCII. Coney's program files are named in ASCII, which keeps pathKey's case folding
// complete: Windows and macOS also fold letters outside ASCII, which lowering byte by byte would miss.
bool printableAscii(std::string_view text) {
    return std::ranges::all_of(text, [](char c) { return c >= 0x20 && c < 0x7F; });
}

// Whether `part` names one file or folder the same way on every platform. Refused: empty, `.` and `..`; a `/` or
// `\`, a drive's `:` or another character Windows forbids in a name; a trailing `.` or space, which Windows strips
// (`coney.exe.` is `coney.exe` there); and Windows' device names (`CON`, `nul.txt`, `COM1`: any case, any extension).
bool safePart(std::string_view part) {
    if (part.empty() || part == "." || part == ".." || part.find_first_of("/\\:*?\"<>|") != std::string_view::npos ||
        part.back() == '.' || part.back() == ' ') {
        return false;
    }
    std::string_view stem = part.substr(0, part.find('.'));
    while (!stem.empty() && stem.back() == ' ') {
        stem.remove_suffix(1);
    }
    static constexpr std::array<std::string_view, 24> kDevices{
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",   "com8",
        "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9", "conin$", "conout$"};
    return std::ranges::find(kDevices, pathKey(stem)) == kDevices.end();
}

// Whether `path` is a relative path that stays inside the install folder and names one file on every platform:
// printable ASCII, no root, and every part one that safePart accepts (so no `..`, no empty part, no trailing `/`).
bool safeRelativePath(std::string_view path) {
    if (path.empty() || !printableAscii(path) || path.front() == '/') {
        return false;
    }
    while (!path.empty()) {
        const auto slash = path.find('/');
        if (!safePart(path.substr(0, slash))) {
            return false;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        path.remove_prefix(slash + 1);
        if (path.empty()) {
            return false; // a trailing slash names a folder, not a file
        }
    }
    return true;
}

// The files of one platform's list.
std::expected<std::vector<ReleaseFile>, Error> parseFiles(const json::Value& list, std::string_view platform) {
    const json::Array* array = list.asArray();
    if (array == nullptr) {
        return invalid(std::format("platforms.{}", platform), "is not a list of files");
    }
    std::vector<ReleaseFile> files;
    std::set<std::string, std::less<>> seen;
    for (std::size_t i = 0; i < array->size(); ++i) {
        const json::Value& entry = (*array)[i];
        const std::string where = std::format("platforms.{}[{}]", platform, i);
        const std::string* path = stringField(entry, "path");
        const std::string* sha = stringField(entry, "sha256");
        const std::string* asset = stringField(entry, "asset");
        const json::Value* size = entry.find("size");
        if (path == nullptr || !safeRelativePath(*path)) {
            return invalid(where, "has no path inside the install folder");
        }
        // Windows and macOS ignore case, so `Coney.exe` and `coney.exe` would be one file there.
        if (!seen.insert(pathKey(*path)).second) {
            return invalid(where, std::format("repeats the path {} (case aside)", *path));
        }
        if (size == nullptr || !size->asInteger() || *size->asInteger() < 0) {
            return invalid(where, "has no size in bytes");
        }
        const auto digest = sha != nullptr ? parseSha256Hex(*sha) : std::nullopt;
        if (!digest) {
            return invalid(where, "has no sha256 of 64 hex digits");
        }
        // The asset is downloaded under its own name before it is moved into place, so it is held to the rules of one
        // part of a path: `.` or `..` would name the download folder or its parent.
        if (asset == nullptr || !printableAscii(*asset) || !safePart(*asset)) {
            return invalid(where, "has no asset name (a plain file name, not . or ..)");
        }
        files.push_back({*path, static_cast<std::uint64_t>(*size->asInteger()), *digest, *asset});
    }
    // A file cannot also be a folder: `a` and `a/b` (case aside) cannot both be written. In sorted order the paths
    // under `a/` come at or after `a/` itself, so the first key from there tells.
    for (const std::string& key : seen) {
        const std::string folder = key + "/";
        const auto next = seen.lower_bound(folder);
        if (next != seen.end() && next->starts_with(folder)) {
            return invalid(std::format("platforms.{}", platform),
                           std::format("has the path {} as a file and as the folder of {}", key, *next));
        }
    }
    return files;
}

} // namespace

std::string pathKey(std::string_view path) {
    std::string key(path);
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return key;
}

std::string Version::text() const {
    std::string out = std::format("{}.{}.{}", major, minor, patch);
    if (!prerelease.empty()) {
        out += "-" + prerelease;
    }
    return out;
}

std::strong_ordering Version::operator<=>(const Version& other) const {
    if (const auto order = std::tie(major, minor, patch) <=> std::tie(other.major, other.minor, other.patch);
        order != 0) {
        return order;
    }
    if (prerelease.empty() || other.prerelease.empty()) {
        // A release sorts after its pre-releases.
        return prerelease.empty() == other.prerelease.empty()
                   ? std::strong_ordering::equal
                   : (prerelease.empty() ? std::strong_ordering::greater : std::strong_ordering::less);
    }
    return comparePrerelease(prerelease, other.prerelease);
}

std::optional<Version> parseVersion(std::string_view text) {
    const auto dash = text.find('-');
    const std::string_view core = text.substr(0, dash);
    Version version;
    if (dash != std::string_view::npos) {
        version.prerelease = std::string(text.substr(dash + 1));
        if (version.prerelease.empty() ||
            version.prerelease.find_first_not_of("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-.") !=
                std::string::npos) {
            return std::nullopt;
        }
    }
    const auto first = core.find('.');
    const auto second = first == std::string_view::npos ? first : core.find('.', first + 1);
    if (second == std::string_view::npos) {
        return std::nullopt;
    }
    const auto major = versionPart(core.substr(0, first));
    const auto minor = versionPart(core.substr(first + 1, second - first - 1));
    const auto patch = versionPart(core.substr(second + 1));
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    version.major = *major;
    version.minor = *minor;
    version.patch = *patch;
    return version;
}

std::expected<Release, Error> parseRelease(std::string_view text) {
    auto doc = json::parse(text);
    if (!doc) {
        return fail(ErrorCode::Invalid, std::format("launcher.json: {}", doc.error().message));
    }
    const json::Value* format = doc->find("format");
    if (format == nullptr || format->asInteger() != kFormat) {
        return invalid("format", "is not 1: a newer launcher is needed");
    }
    Release release;
    const std::string* version = stringField(*doc, "version");
    const auto parsed = version != nullptr ? parseVersion(*version) : std::nullopt;
    if (!parsed) {
        return invalid("version", "is not MAJOR.MINOR.PATCH");
    }
    release.version = *parsed;
    const std::string* minLauncher = stringField(*doc, "min_launcher");
    const auto minimum = minLauncher != nullptr ? parseVersion(*minLauncher) : std::nullopt;
    if (!minimum) {
        return invalid("min_launcher", "is not MAJOR.MINOR.PATCH");
    }
    release.minLauncher = *minimum;
    const std::string* channel = stringField(*doc, "channel");
    if (channel == nullptr || (*channel != "stable" && *channel != "preview")) {
        return invalid("channel", "is not stable or preview");
    }
    release.channel = *channel;
    if (const std::string* url = stringField(*doc, "changelog_url"); url != nullptr) {
        release.changelogUrl = *url;
    }
    if (const json::Value* notes = doc->find("notes"); notes != nullptr && notes->asArray() != nullptr) {
        for (const json::Value& note : *notes->asArray()) {
            if (const std::string* line = note.asString(); line != nullptr) {
                release.notes.push_back(*line);
            }
        }
    }
    const json::Value* platforms = doc->find("platforms");
    if (platforms == nullptr || platforms->asObject() == nullptr) {
        return invalid("platforms", "is missing");
    }
    for (const auto& [name, list] : *platforms->asObject()) {
        auto files = parseFiles(list, name);
        if (!files) {
            return std::unexpected(std::move(files.error()));
        }
        release.platforms.emplace(name, std::move(*files));
    }
    return release;
}

} // namespace coney::launcher
