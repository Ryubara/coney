// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/game_settings.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <optional>
#include <system_error>
#include <utility>

#include "core/parse_number.h"

// @coney-only the console's settings (docs/guides/console.md#settings); the original has no options to change.

namespace coney::platform {

namespace {

using Applied = std::expected<std::string, std::string>;

// The words of a list for a refusal: `a, b, c`.
std::string joined(const std::vector<std::string>& words) {
    std::string text;
    for (const std::string& word : words) {
        text += text.empty() ? "" : ", ";
        text += word;
    }
    return text;
}

} // namespace

GameSetting boolSetting(std::string name, std::string help, std::function<bool()> get, std::function<void(bool)> set) {
    GameSetting setting;
    setting.name = std::move(name);
    setting.help = std::move(help);
    setting.get = [get] { return std::string(get() ? "on" : "off"); };
    setting.set = [name = setting.name, get = std::move(get), set = std::move(set)](std::string_view value) -> Applied {
        if (value != "on" && value != "off") {
            return std::unexpected(std::format(R"({} takes on or off, not "{}")", name, value));
        }
        set(value == "on");
        return std::string(get() ? "on" : "off");
    };
    return setting;
}

GameSetting wholeSetting(std::string name, std::string help, int min, int max, std::function<int()> get,
                         std::function<void(int)> set) {
    GameSetting setting;
    setting.name = std::move(name);
    setting.help = std::move(help);
    setting.get = [get] { return std::to_string(get()); };
    setting.set = [name = setting.name, min, max, get = std::move(get),
                   set = std::move(set)](std::string_view value) -> Applied {
        int parsed = 0;
        const auto read = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (value.empty() || read.ec != std::errc{} || read.ptr != value.data() + value.size()) {
            return std::unexpected(std::format(R"({} takes a whole number, not "{}")", name, value));
        }
        if (parsed < min || parsed > max) {
            return std::unexpected(std::format("{} takes a number from {} to {}, not {}", name, min, max, parsed));
        }
        set(parsed);
        return std::to_string(get());
    };
    return setting;
}

GameSetting numberSetting(std::string name, std::string help, double min, double max, std::function<double()> get,
                          std::function<void(double)> set) {
    GameSetting setting;
    setting.name = std::move(name);
    setting.help = std::move(help);
    setting.get = [get] { return std::format("{}", get()); };
    setting.set = [name = setting.name, min, max, get = std::move(get),
                   set = std::move(set)](std::string_view value) -> Applied {
        const std::optional<double> parsed = parseDecimal(value);
        if (!parsed) {
            return std::unexpected(std::format(R"({} takes a number, not "{}")", name, value));
        }
        if (*parsed < min || *parsed > max) {
            return std::unexpected(std::format("{} takes a number from {} to {}, not {}", name, min, max, *parsed));
        }
        set(*parsed);
        return std::format("{}", get());
    };
    return setting;
}

GameSetting choiceSetting(std::string name, std::string help, std::vector<std::string> choices,
                          std::function<std::string()> get, std::function<void(std::string_view)> set) {
    GameSetting setting;
    setting.name = std::move(name);
    setting.help = std::move(help);
    setting.get = get;
    setting.set = [name = setting.name, choices = std::move(choices), get = std::move(get),
                   set = std::move(set)](std::string_view value) -> Applied {
        if (std::ranges::find(choices, value) == choices.end()) {
            return std::unexpected(std::format(R"({} takes one of {}, not "{}")", name, joined(choices), value));
        }
        set(value);
        return get();
    };
    return setting;
}

} // namespace coney::platform
