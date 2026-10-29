// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/script_patch_command.h"

namespace coney::platform {

int runScriptPatchCommand(std::span<const std::string_view> /*args*/, std::FILE* /*out*/, std::FILE* /*err*/) {
    return 1;
}

} // namespace coney::platform
