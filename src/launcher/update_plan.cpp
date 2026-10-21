// SPDX-License-Identifier: GPL-3.0-or-later
#include "launcher/update_plan.h"

#include <map>
#include <set>

namespace coney::launcher {

UpdatePlan planUpdate(const Release& release, std::string_view platform, std::span<const InstalledFile> installed,
                      const Version& installedVersion, const Version& launcherVersion, bool repair) {
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

    std::map<std::string_view, const InstalledFile*> byPath;
    for (const InstalledFile& file : installed) {
        byPath.emplace(file.path, &file);
    }
    std::set<std::string_view> wanted;
    for (const ReleaseFile& file : files->second) {
        wanted.insert(file.path);
        const auto it = byPath.find(file.path);
        const bool same = it != byPath.end() && it->second->size == file.size && it->second->sha256 == file.sha256;
        if (!same) {
            plan.download.push_back(file);
            plan.downloadBytes += file.size;
        }
    }
    for (const InstalledFile& file : installed) {
        if (!wanted.contains(file.path)) {
            plan.remove.push_back(file.path);
        }
    }
    plan.verdict =
        plan.download.empty() && plan.remove.empty() ? UpdatePlan::Verdict::UpToDate : UpdatePlan::Verdict::Ready;
    return plan;
}

} // namespace coney::launcher
