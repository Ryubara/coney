// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher/update_plan.h"

#include <map>
#include <set>
#include <string>

namespace coney::launcher {

UpdatePlan planUpdate(const Release& release, std::string_view platform, std::span<const InstalledFile> installed,
                      const Version& installedVersion, const Version& launcherVersion, PathCase pathCase, bool repair) {
    UpdatePlan plan;
    const auto files = release.platforms.find(platform);
    if (files == release.platforms.end()) {
        plan.verdict = UpdatePlan::Verdict::NoPlatform;
        return plan;
    }
    if (launcherVersion < release.minLauncher) {
        plan.verdict = UpdatePlan::Verdict::LauncherTooOld;
        return plan;
    }
    // A repair restores the installed version's files; an update needs a newer version.
    if (repair ? release.version != installedVersion : !(installedVersion < release.version)) {
        plan.verdict = UpdatePlan::Verdict::NotNewer;
        return plan;
    }

    // Where the file system folds case, `Coney.exe` and `coney.exe` are one file, and removing the old spelling after
    // downloading the new one would delete the file just downloaded; where it does not, they are two files.
    const auto key = [pathCase](std::string_view path) {
        return pathCase == PathCase::Folded ? pathKey(path) : std::string(path);
    };
    std::map<std::string, const InstalledFile*, std::less<>> byPath;
    for (const InstalledFile& file : installed) {
        byPath.emplace(key(file.path), &file);
    }
    std::set<std::string, std::less<>> wanted;
    for (const ReleaseFile& file : files->second) {
        wanted.insert(key(file.path));
        const auto it = byPath.find(key(file.path));
        const bool same = it != byPath.end() && it->second->size == file.size && it->second->sha256 == file.sha256;
        if (!same) {
            plan.download.push_back(file);
            plan.downloadBytes += file.size;
        }
    }
    for (const InstalledFile& file : installed) {
        if (!wanted.contains(key(file.path))) {
            plan.remove.push_back(file.path);
        }
    }
    plan.verdict =
        plan.download.empty() && plan.remove.empty() ? UpdatePlan::Verdict::UpToDate : UpdatePlan::Verdict::Ready;
    return plan;
}

} // namespace coney::launcher
