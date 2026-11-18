// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/import_command.h"

namespace coney::platform {

// Stub for the test commit: imports nothing yet.
int runImportCommand(const Options& /*options*/, std::FILE* /*out*/) { return 1; }

} // namespace coney::platform
