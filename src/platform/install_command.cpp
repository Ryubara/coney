// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/install_command.h"

#include <format>
#include <string>
#include <system_error>

#include <SDL3/SDL_filesystem.h>

#include "fileio/disc.h"
#include "install/installer.h"
#include "platform/install_folder_fs.h"

namespace coney::platform {

namespace {

// A byte count for people, in decimal gigabytes as drives are sold.
std::string gigabytes(std::uint64_t bytes) { return std::format("{:.2f} GB", static_cast<double>(bytes) / 1e9); }

// Writes one line to `out`, flushed, so a launcher or a log reading the stream sees it at once.
void say(std::FILE* out, const std::string& line) {
    std::fprintf(out, "coney: %s\n", line.c_str());
    std::fflush(out);
}

} // namespace

int runInstallCommand(const Options& options, std::span<const install::KnownDisc> known,
                      const std::vector<std::filesystem::path>& programFiles, std::FILE* out) {
    // Both are required by parseOptions() whenever installFrom is set.
    const std::filesystem::path discPath(options.installFrom.value_or(""));
    const std::filesystem::path dataPath(options.dataPath.value_or(""));
    auto disc = io::Disc::open(discPath);
    if (!disc) {
        say(out, disc.error().message);
        return 1;
    }
    auto folder = HostInstallFolder::openRoot(dataPath);
    if (!folder) {
        say(out, folder.error().message);
        return 1;
    }

    // One line when a file starts, and one each time another tenth of the whole is done.
    std::size_t lastFile = 0;
    std::uint64_t lastTenth = 0;
    const auto progress = [&](const install::InstallProgress& p) {
        if (p.fileNumber != lastFile && p.fileNumber > 0 && p.step != install::InstallStep::Index) {
            lastFile = p.fileNumber;
            const char* verb = p.step == install::InstallStep::Verifying ? "checking" : "copying";
            say(out, std::format("[{}/{}] {} {} ({})", p.fileNumber, p.fileCount, verb, p.file, gigabytes(p.fileSize)));
        }
        const std::uint64_t tenth = p.total == 0 ? 0 : (p.done * 10) / p.total;
        if (tenth > lastTenth) {
            lastTenth = tenth;
            say(out, std::format("{}% ({} of {})", tenth * 10, gigabytes(p.done), gigabytes(p.total)));
        }
        return true;
    };

    install::InstallRequest request;
    request.ps2 = &*disc;
    request.programFiles = programFiles;
    request.verifyInstalled = options.verifyInstall;
    request.known = known;
    const auto report = install::install(**folder, request, progress);
    if (!report) {
        say(out, report.error().message);
        say(out, "nothing half-copied was kept; run the same command again to go on");
        return 1;
    }
    const install::KnownDisc* release = nullptr;
    for (const install::KnownDisc& candidate : known) {
        if (candidate.id == report->ps2Disc) {
            release = &candidate;
        }
    }
    say(out, std::format("installed {} into {}: {} files (copied {}, kept {}), {} copied; disc digest {}",
                         release != nullptr ? release->title : std::string_view(report->ps2Disc), dataPath.string(),
                         report->copied + report->kept, report->copied, report->kept, gigabytes(report->bytesCopied),
                         report->ps2Digest));
    return 0;
}

std::vector<std::filesystem::path> installProgramFiles(const std::filesystem::path& installRoot, const char* argv0) {
    const char* base = SDL_GetBasePath(); // owned by SDL; null when the platform cannot tell
    if (base == nullptr || argv0 == nullptr) {
        return {};
    }
    const std::filesystem::path folder(base);
    std::filesystem::path name = std::filesystem::path(argv0).filename();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(folder / name, ec)) {
        name += ".exe"; // started as `coney` on Windows
    }
    const std::filesystem::path executable = folder / name;
    if (!std::filesystem::is_regular_file(executable, ec)) {
        return {};
    }
    if (std::filesystem::exists(installRoot, ec) && std::filesystem::equivalent(folder, installRoot, ec)) {
        return {};
    }
    return {executable};
}

} // namespace coney::platform
