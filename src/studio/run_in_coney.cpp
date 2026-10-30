// SPDX-License-Identifier: GPL-3.0-or-later
#include "studio/run_in_coney.h"

namespace coney::studio {

std::vector<std::string> runInConeyCommand(const RunRequest& request) {
    std::vector<std::string> command{request.coney.string()};
    // With a disc, the play mode on the layout (`--play-level sandbox:PATH`, which takes a path as `--sandbox` does);
    // without one, the sandbox viewer, which needs no game data.
    if (request.disc) {
        command.insert(command.end(), {"--disc", *request.disc, "--play-level", "sandbox:" + request.level.string()});
        if (request.spawn) {
            command.insert(command.end(), {"--spawn", *request.spawn});
        }
    } else {
        command.insert(command.end(), {"--sandbox", request.level.string()});
    }
    if (request.assets) {
        command.insert(command.end(), {"--assets", request.assets->string()});
    }
    return command;
}

std::filesystem::path coneyBeside(const std::filesystem::path& studio) {
    return studio.parent_path() / (studio.extension() == ".exe" ? "coney.exe" : "coney");
}

} // namespace coney::studio
