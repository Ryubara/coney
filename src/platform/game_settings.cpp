// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_settings.h"

#include <utility>

// @coney-only the console's settings (docs/guides/console.md#settings); the original has no options to change.

namespace coney::platform {

namespace {

// A stub setting: it reads as empty and takes no value, as a setting did not exist before.
GameSetting stub(std::string name, std::string help) {
    GameSetting setting;
    setting.name = std::move(name);
    setting.help = std::move(help);
    setting.get = [] { return std::string(); };
    setting.set = [](std::string_view) -> std::expected<std::string, std::string> {
        return std::unexpected(std::string());
    };
    return setting;
}

} // namespace

GameSetting boolSetting(std::string name, std::string help, std::function<bool()>, std::function<void(bool)>) {
    return stub(std::move(name), std::move(help));
}

GameSetting wholeSetting(std::string name, std::string help, int, int, std::function<int()>, std::function<void(int)>) {
    return stub(std::move(name), std::move(help));
}

GameSetting numberSetting(std::string name, std::string help, double, double, std::function<double()>,
                          std::function<void(double)>) {
    return stub(std::move(name), std::move(help));
}

GameSetting choiceSetting(std::string name, std::string help, std::vector<std::string>, std::function<std::string()>,
                          std::function<void(std::string_view)>) {
    return stub(std::move(name), std::move(help));
}

} // namespace coney::platform
