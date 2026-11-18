// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/import_command.h"

#include <filesystem>
#include <format>
#include <optional>
#include <string>

#include "platform/profile_folder.h"
#include "warriors/disk_profile_store.h"
#include "warriors/game_state.h"
#include "warriors/save_import.h"

namespace coney::platform {

namespace {

// Writes one line to `out`, flushed, as `coney --install` does.
void say(std::FILE* out, const std::string& line) {
    std::fprintf(out, "coney: %s\n", line.c_str());
    std::fflush(out);
}

} // namespace

int runImportCommand(const Options& options, std::FILE* out) {
    const std::optional<std::filesystem::path> folder = profileFolder(options);
    if (!folder) {
        say(out, "--import-save: no profiles folder to import into; name one with --profiles DIR");
        return 1;
    }
    const auto save = readOriginalSave(std::filesystem::path(options.importSave.value_or("")));
    if (!save) {
        say(out, save.error().message);
        return 1;
    }
    say(out, std::format("{}: a {} with {} profile(s)", save->source, describe(save->kind), save->profiles.size()));
    for (const std::string& skipped : save->skipped) {
        say(out, std::format("left out {}", skipped));
    }
    // The store reads the folder as the game would; the game state it would load into is this command's own.
    GameState state;
    DiskProfileStore store(*folder, state);
    std::size_t imported = 0;
    for (const ImportCandidate& candidate : save->profiles) {
        const auto slot = importProfile(candidate.record, store);
        if (!slot) {
            say(out, std::format("{} (card slot {}) not imported: {}", candidate.record.name, candidate.slot + 1,
                                 slot.error().message));
            continue;
        }
        ++imported;
        say(out, std::format("imported {} (card slot {}) as profile {} in {}", candidate.record.name,
                             candidate.slot + 1, *slot + 1, folder->string()));
    }
    say(out, std::format("{} of {} profile(s) imported", imported, save->profiles.size()));
    return imported > 0 ? 0 : 1;
}

} // namespace coney::platform
