// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/install_manifest.h"

#include <format>
#include <map>

#include "core/sha1.h"

namespace coney::install {

namespace {

// The manifest's layout version; a reader refuses one it does not know.
constexpr int kManifestFormat = 1;

// The JSON name of a platform, which is also its folder below `game/`.
std::string_view platformName(Platform platform) { return platform == Platform::Ps2 ? "ps2" : "xbox"; }

// `text` as a JSON string, with its quotes: paths on Windows hold backslashes.
std::string quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
            } else {
                out.push_back(c); // UTF-8 passes through as it is
            }
        }
    }
    out.push_back('"');
    return out;
}

// Whether `name` starts with the folder `folder` (any case), as `IOP/` or `bik/`.
bool inFolder(std::string_view name, std::string_view folder) {
    if (name.size() <= folder.size() || name[folder.size()] != '/') {
        return false;
    }
    for (std::size_t i = 0; i < folder.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
        if (lower(name[i]) != lower(folder[i])) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view fileType(Platform platform, std::string_view name) {
    if (platform == Platform::Ps2) {
        if (name == "WARRIORS.WAD" || name == "WARRIORS.DIR") {
            return "archive";
        }
        if (inFolder(name, "IOP")) {
            return "sound";
        }
        if (inFolder(name, "PSS")) {
            return "movies";
        }
        if (inFolder(name, "MODULES")) {
            return "modules";
        }
        if (name == "SLUS_212.15") {
            return "executable";
        }
        return name == "SYSTEM.CNF" ? "system" : "other";
    }
    return "other"; // the Xbox disc's types come with its install (docs/research/discs.md)
}

std::string listDigest(std::span<const ManifestFile> files) {
    Sha1 sha;
    for (const ManifestFile& file : files) {
        sha.update(std::format("{} {} {}\n", file.name, file.size, file.sha1));
    }
    return toHex(sha.finish());
}

std::string manifestJson(const Manifest& manifest) {
    std::string out =
        std::format("{{\n  \"format\": {},\n  \"coney_version\": {},\n", kManifestFormat, quoted(manifest.version));

    // A platform's files, in copy order, for its source's digest and its types.
    const auto filesOf = [&](Platform platform) {
        std::vector<ManifestFile> files;
        for (const ManifestFile& file : manifest.files) {
            if (file.platform == platform) {
                files.push_back(file);
            }
        }
        return files;
    };

    out += "  \"sources\": [";
    for (std::size_t i = 0; i < manifest.sources.size(); ++i) {
        const ManifestSource& source = manifest.sources[i];
        const Platform platform = source.disc->platform;
        const auto files = filesOf(platform);
        out += std::format("{}\n    {{\"platform\": {}, \"disc\": {}, \"title\": {}, \"path\": {}, \"kind\": {}, "
                           "\"folder\": {}, \"files\": {}, \"digest\": {}}}",
                           i == 0 ? "" : ",", quoted(platformName(platform)), quoted(source.disc->id),
                           quoted(source.disc->title), quoted(source.path), quoted(source.image ? "image" : "folder"),
                           quoted(std::format("game/{}", platformName(platform))), files.size(),
                           quoted(listDigest(files)));
    }
    out += "\n  ],\n";

    // Per platform and type: count, bytes and digest, the types sorted by name.
    out += "  \"types\": {";
    bool firstType = true;
    for (const ManifestSource& source : manifest.sources) {
        const Platform platform = source.disc->platform;
        std::map<std::string_view, std::vector<ManifestFile>> byType;
        for (const ManifestFile& file : filesOf(platform)) {
            byType[fileType(platform, file.name)].push_back(file);
        }
        for (const auto& [type, files] : byType) {
            std::uint64_t bytes = 0;
            for (const ManifestFile& file : files) {
                bytes += file.size;
            }
            out += std::format("{}\n    {}: {{\"files\": {}, \"bytes\": {}, \"digest\": {}}}", firstType ? "" : ",",
                               quoted(std::format("{}/{}", platformName(platform), type)), files.size(), bytes,
                               quoted(listDigest(files)));
            firstType = false;
        }
    }
    out += "\n  },\n";

    out += "  \"files\": [";
    for (std::size_t i = 0; i < manifest.files.size(); ++i) {
        const ManifestFile& file = manifest.files[i];
        out += std::format("{}\n    {{\"path\": {}, \"size\": {}, \"sha1\": {}}}", i == 0 ? "" : ",",
                           quoted(std::format("game/{}/{}", platformName(file.platform), file.name)), file.size,
                           quoted(file.sha1));
    }
    out += "\n  ],\n";

    out += "  \"program\": [";
    for (std::size_t i = 0; i < manifest.program.size(); ++i) {
        const ProgramFile& file = manifest.program[i];
        out += std::format("{}\n    {{\"path\": {}, \"size\": {}, \"sha1\": {}}}", i == 0 ? "" : ",", quoted(file.name),
                           file.size, quoted(file.sha1));
    }
    out += manifest.program.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

} // namespace coney::install
