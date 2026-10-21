// SPDX-License-Identifier: GPL-3.0-or-later
#include "install/installer.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include "core/sha1.h"
#include "fileio/file_stream.h"
#include "install/install_manifest.h"

#ifndef CONEY_VERSION
#define CONEY_VERSION "unknown"
#endif

namespace coney::install {

namespace {

// Bytes read and written at a time, and between two progress reports.
constexpr std::size_t kPiece = std::size_t{8} << 20;
// Space kept free beyond the files, for the manifest and the file system's own needs.
constexpr std::uint64_t kSpaceMargin = std::uint64_t{64} << 20;

// Lower case, for comparing names the way a disc compares them.
std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// A byte count for people: "4.27 GB" (decimal units, as drives are sold).
std::string bytesText(std::uint64_t bytes) { return std::format("{:.2f} GB", static_cast<double>(bytes) / 1e9); }

// The titles of the known discs of `platform`, for the "Coney installs from" line of a refusal.
std::string knownTitles(std::span<const KnownDisc> known, Platform platform) {
    std::string titles;
    for (const KnownDisc& disc : known) {
        if (disc.platform == platform) {
            titles += titles.empty() ? std::string(disc.title) : std::format("; {}", disc.title);
        }
    }
    return titles;
}

// The folder part of a path (`game/ps2/IOP` of `game/ps2/IOP/BFW.SND`); empty for a name in the root.
std::string_view parentOf(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? std::string_view{} : path.substr(0, slash);
}

// What one copy or check reports to the progress function, and whether it was cancelled.
struct Tracker {
    const ProgressFunction& progress;
    InstallProgress state;
    // Reports the current state; false when the player cancelled.
    bool report() const { return !progress || progress(state); }
};

// Reads `source` to its end in pieces, giving each piece to `sink` and the SHA-1; reports progress after each piece.
// Returns the digest, nullopt when cancelled, or the first read or write failure.
template <typename Sink>
std::expected<std::optional<Sha1Digest>, Error> stream(io::Stream& source, Tracker& tracker, Sink&& sink) {
    Sha1 sha;
    std::vector<std::byte> buffer(kPiece);
    const std::uint64_t start = tracker.state.done;
    while (source.remaining() > 0) {
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(source.remaining(), buffer.size()));
        const std::span<std::byte> piece(buffer.data(), take);
        if (auto read = source.read(piece); !read) {
            return std::unexpected(std::move(read.error()));
        }
        sha.update(piece);
        if (auto written = sink(std::span<const std::byte>(piece)); !written) {
            return std::unexpected(std::move(written.error()));
        }
        tracker.state.fileDone += take;
        tracker.state.done = start + tracker.state.fileDone;
        if (!tracker.report()) {
            return std::optional<Sha1Digest>{};
        }
    }
    return std::optional<Sha1Digest>{sha.finish()};
}

// Copies `source` to `dest` through `dest.part`, renamed once `expected` (when given) matches. Returns the digest,
// nullopt when cancelled (the part file is removed), or the failure.
std::expected<std::optional<Sha1Digest>, Error> copyFile(InstallFolder& folder, io::Stream& source,
                                                         std::string_view dest,
                                                         const std::optional<Sha1Digest>& expected, Tracker& tracker) {
    const std::string part = std::format("{}{}", dest, kPartSuffix);
    auto out = folder.create(part);
    if (!out) {
        return std::unexpected(std::move(out.error()));
    }
    auto digest = stream(source, tracker, [&](std::span<const std::byte> piece) { return (*out)->write(piece); });
    auto closed = (*out)->close();
    if (!digest || !*digest || !closed) {
        // Nothing half-copied keeps a name; a failed removal only leaves a part file the next run overwrites.
        (void)folder.remove(part);
        if (!digest) {
            return std::unexpected(std::move(digest.error()));
        }
        if (!*digest) {
            return std::optional<Sha1Digest>{};
        }
        return std::unexpected(std::move(closed.error()));
    }
    if (expected && **digest != *expected) {
        (void)folder.remove(part);
        return fail(ErrorCode::Invalid,
                    std::format("{}: SHA-1 {} is not the known disc's {}: a read error or a modified disc; the file "
                                "was not kept. Run the install again to retry",
                                dest, toHex(**digest), toHex(*expected)));
    }
    if (auto renamed = folder.rename(part, dest); !renamed) {
        return std::unexpected(std::move(renamed.error()));
    }
    return digest;
}

// Writes `text` to `path` through a part file, so a reader never sees half of it.
std::expected<void, Error> writeText(InstallFolder& folder, std::string_view path, std::string_view text) {
    const std::string part = std::format("{}{}", path, kPartSuffix);
    auto out = folder.create(part);
    if (!out) {
        return std::unexpected(std::move(out.error()));
    }
    auto written = (*out)->write(std::as_bytes(std::span(text.data(), text.size())));
    auto closed = (*out)->close();
    if (!written || !closed) {
        (void)folder.remove(part);
        return std::unexpected(std::move(!written ? written.error() : closed.error()));
    }
    return folder.rename(part, path);
}

} // namespace

std::string_view coneyVersion() { return CONEY_VERSION; }

std::vector<SourceFile> listFiles(const io::Disc& disc) {
    std::vector<SourceFile> files;
    for (std::string& name : disc.names()) {
        const auto size = disc.fileSize(name);
        files.push_back({std::move(name), size ? *size : 0});
    }
    return files;
}

std::expected<const KnownDisc*, Error> identifyDisc(std::span<const SourceFile> files, Platform platform,
                                                    std::span<const KnownDisc> known, std::string_view where) {
    std::map<std::string, std::uint64_t, std::less<>> sizes;
    for (const SourceFile& file : files) {
        sizes.emplace(lower(file.name), file.size);
    }
    const std::string supported = knownTitles(known, platform);
    // The release is named by its key file: find the known disc whose key file is there with its size.
    const KnownDisc* match = nullptr;
    const KnownDisc* sameKeyName = nullptr;
    std::uint64_t keySize = 0;
    std::uint64_t knownKeySize = 0;
    for (const KnownDisc& disc : known) {
        if (disc.platform != platform) {
            continue;
        }
        const auto key = sizes.find(lower(disc.keyFile));
        const KnownFile* keyFile = nullptr;
        for (const KnownFile& file : disc.files) {
            if (lower(file.name) == lower(disc.keyFile)) {
                keyFile = &file;
            }
        }
        if (key == sizes.end() || keyFile == nullptr) {
            continue;
        }
        if (key->second == keyFile->size) {
            match = &disc;
            break;
        }
        sameKeyName = &disc;
        keySize = key->second;
        knownKeySize = keyFile->size;
    }
    if (match == nullptr) {
        if (sameKeyName != nullptr) {
            return fail(ErrorCode::Invalid,
                        std::format("{}: {} is {} bytes, not the {} of {}: another release of the game or a modified "
                                    "disc. Coney installs from: {}",
                                    where, sameKeyName->keyFile, keySize, knownKeySize, sameKeyName->title, supported));
        }
        const bool hasArchive = sizes.contains("warriors.wad");
        return fail(ErrorCode::Invalid,
                    hasArchive ? std::format("{}: holds WARRIORS.WAD but not the boot file Coney knows: another "
                                             "release of the game? Coney installs from: {}",
                                             where, supported)
                               : std::format("{}: not a disc of The Warriors (no WARRIORS.WAD and no boot file Coney "
                                             "knows). Coney installs from: {}",
                                             where, supported));
    }
    // Every file of that release must be there, with its size, before anything is copied.
    for (const KnownFile& file : match->files) {
        const auto it = sizes.find(lower(file.name));
        if (it == sizes.end()) {
            return fail(ErrorCode::Invalid,
                        std::format("{}: {} is missing: an incomplete copy of {}?", where, file.name, match->title));
        }
        if (it->second != file.size) {
            return fail(ErrorCode::Invalid, std::format("{}: {} is {} bytes, not {}: a damaged or modified copy of {}",
                                                        where, file.name, it->second, file.size, match->title));
        }
    }
    return match;
}

std::expected<InstallReport, Error> install(InstallFolder& folder, const InstallRequest& request,
                                            const ProgressFunction& progress) {
    if (request.ps2 == nullptr) {
        return fail(ErrorCode::InvalidArgument, "an install needs the PS2 disc");
    }
    const io::Disc& disc = *request.ps2;
    Tracker tracker{progress, {}};
    if (!tracker.report()) {
        InstallReport cancelled;
        cancelled.cancelled = true;
        return cancelled;
    }

    // Identify the disc before writing anything.
    const auto files = listFiles(disc);
    auto known = identifyDisc(files, Platform::Ps2, request.known, disc.path().string());
    if (!known) {
        return std::unexpected(std::move(known.error()));
    }
    const KnownDisc& release = **known;
    InstallReport report;
    report.ps2Disc = std::string(release.id);

    // The install is not finished until the manifest is written again, last.
    if (auto removed = folder.remove(kManifestPath); !removed) {
        return std::unexpected(std::move(removed.error()));
    }

    // Which files are already there, and whether the rest fits.
    std::uint64_t total = 0;
    std::uint64_t needed = 0;
    for (const KnownFile& file : release.files) {
        total += file.size;
        const auto existing = folder.fileSize(std::format("{}/{}", kPs2Folder, file.name));
        if (!existing || *existing != file.size) {
            needed += file.size;
        }
    }
    const auto freeSpace = folder.freeBytes();
    if (freeSpace && *freeSpace < needed + kSpaceMargin) {
        return fail(ErrorCode::Io, std::format("the install folder's drive has {} free; the disc's files need {}",
                                               bytesText(*freeSpace), bytesText(needed + kSpaceMargin)));
    }

    tracker.state.fileCount = release.files.size();
    tracker.state.total = total;
    Manifest manifest{request.version, {{&release, disc.path().string(), disc.isImage()}}, {}, {}};
    for (const KnownFile& file : release.files) {
        const std::string dest = std::format("{}/{}", kPs2Folder, file.name);
        const auto expected = parseSha1Hex(file.sha1);
        if (!expected) {
            return fail(ErrorCode::Invalid,
                        std::format("{}: the known SHA-1 '{}' is not 40 hex digits", file.name, file.sha1));
        }
        ++tracker.state.fileNumber;
        tracker.state.file = dest;
        tracker.state.fileDone = 0;
        tracker.state.fileSize = file.size;
        const std::uint64_t before = tracker.state.done;
        manifest.files.push_back({Platform::Ps2, std::string(file.name), file.size, std::string(file.sha1)});

        // A file of the right size has been checked by an earlier run, unless the player asks to check it again.
        if (folder.fileSize(dest) == file.size) {
            bool keep = !request.verifyInstalled;
            if (!keep) {
                tracker.state.step = InstallStep::Verifying;
                auto installed = folder.open(dest);
                if (!installed) {
                    return std::unexpected(std::move(installed.error()));
                }
                auto digest = stream(**installed, tracker,
                                     [](std::span<const std::byte>) { return std::expected<void, Error>{}; });
                if (!digest) {
                    return std::unexpected(std::move(digest.error()));
                }
                if (!*digest) {
                    report.cancelled = true;
                    return report;
                }
                keep = **digest == *expected;
                tracker.state.fileDone = 0;
                tracker.state.done = before;
            }
            if (keep) {
                ++report.kept;
                tracker.state.done = before + file.size;
                tracker.state.fileDone = file.size;
                if (!tracker.report()) {
                    report.cancelled = true;
                    return report;
                }
                continue;
            }
        }

        // Copy it: through a part file, checked against the known SHA-1.
        tracker.state.step = InstallStep::Copying;
        if (auto made = folder.makeFolders(parentOf(dest)); !made) {
            return std::unexpected(std::move(made.error()));
        }
        auto source = disc.openFile(file.name);
        if (!source) {
            return std::unexpected(std::move(source.error()));
        }
        auto copied = copyFile(folder, *source, dest, expected, tracker);
        if (!copied) {
            return std::unexpected(std::move(copied.error()));
        }
        if (!*copied) {
            report.cancelled = true;
            return report;
        }
        ++report.copied;
        report.bytesCopied += file.size;
    }

    // Coney's own files, into the root as they are; their SHA-1s go into the manifest.
    tracker.state.step = InstallStep::Program;
    for (const std::filesystem::path& path : request.programFiles) {
        std::error_code ec;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec) {
            return fail(ErrorCode::NotFound, std::format("{}: cannot be read ({})", path.string(), ec.message()));
        }
        auto source = io::FileStream::open(path, 0, size);
        if (!source) {
            return std::unexpected(std::move(source.error()));
        }
        const std::u8string leaf = path.filename().u8string();
        const std::string name(leaf.begin(), leaf.end());
        tracker.state.file = name;
        tracker.state.fileDone = 0;
        tracker.state.fileSize = size;
        const std::uint64_t before = tracker.state.done;
        auto copied = copyFile(folder, *source, name, std::nullopt, tracker);
        tracker.state.done = before; // the total counts the disc's files only
        if (!copied) {
            return std::unexpected(std::move(copied.error()));
        }
        if (!*copied) {
            report.cancelled = true;
            return report;
        }
        manifest.program.push_back({name, size, toHex(**copied)});
    }

    // The empty override folders, then the manifest, last.
    tracker.state.step = InstallStep::Index;
    tracker.state.file = kManifestPath;
    for (const std::string_view overrides : kOverrideFolders) {
        if (auto made = folder.makeFolders(overrides); !made) {
            return std::unexpected(std::move(made.error()));
        }
    }
    if (auto made = folder.makeFolders(kIndexFolder); !made) {
        return std::unexpected(std::move(made.error()));
    }
    std::string text = manifestJson(manifest);
    report.ps2Digest = listDigest(manifest.files);
    if (auto written = writeText(folder, kManifestPath, text); !written) {
        return std::unexpected(std::move(written.error()));
    }
    report.manifest = std::move(text);
    (void)tracker.report();
    return report;
}

} // namespace coney::install
