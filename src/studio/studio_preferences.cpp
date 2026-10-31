// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only the editor's per-user preferences; the original has no editor.
#include "studio/studio_preferences.h"

namespace coney::studio {

void addRecentProject(StudioPreferences& preferences, RecentProject project) {
    // Stand-in for the real list, which the commit after this one makes.
    static_cast<void>(preferences);
    static_cast<void>(project);
}

bool removeRecentProject(StudioPreferences& preferences, const std::filesystem::path& root) {
    static_cast<void>(preferences);
    static_cast<void>(root);
    return false;
}

bool projectExists(const std::filesystem::path& root) {
    static_cast<void>(root);
    return true;
}

std::string formatPreferences(const StudioPreferences& preferences) {
    static_cast<void>(preferences);
    return {};
}

std::expected<StudioPreferences, Error> parsePreferences(std::string_view text) {
    static_cast<void>(text);
    return StudioPreferences{};
}

std::expected<StudioPreferences, Error> loadPreferences(const std::filesystem::path& file) {
    static_cast<void>(file);
    return StudioPreferences{};
}

std::expected<void, Error> savePreferences(const std::filesystem::path& file, const StudioPreferences& preferences) {
    static_cast<void>(file);
    static_cast<void>(preferences);
    return {};
}

} // namespace coney::studio
